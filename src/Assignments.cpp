// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants   - the assignment key, the player's reference, co-save ids
//   Records     - one assignment per actor; g_managed mirrors the NPCs for the event sinks
//   Commands    - copy what someone wears, assign, unassign, release everyone (HUD messages)
//   Keys        - the modifier held with F10 picks the command, Run picks the target
//   Load, death - pieces put back on when an assigned NPC loads; locks released when one dies
//   Preview     - Livery's locks off one NPC while the preview dresses them, back on after
//   Co-save     - save, load, revert
//   Event sinks - TESObjectLoadedEvent, TESDeathEvent
//   API         - RegisterCoSave / RegisterSinks / HandleKey / SuspendLocks / ResumeLocks
#include "PCH.h"

#include "Assignments.h"

#include "Dressing.h"
#include "Settings.h"

#include <algorithm>
#include <format>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace
{
	// --- Constants ------------------------------------------------------------

	using Key = RE::BSKeyboardDevice::Keys::Key;
	// Stand in for the outfit window until it exists (docs/design.md §7). F9 is the game's
	// Quick Load. F8 is also the capture hotkey's default; Plugin.cpp checks these keys first.
	constexpr std::uint32_t kAssignKey = Key::kF10;
	constexpr std::uint32_t kReleaseEveryoneKey = Key::kF8;

	// Skyrim.esm always loads at index 00, so the player's reference is always 0x14.
	constexpr RE::FormID kPlayerRef = 0x14;

	constexpr std::uint32_t kCoSaveID = 'LVRY';
	constexpr std::uint32_t kAssignmentsRecord = 'ASGN';
	constexpr std::uint32_t kAssignmentsVersion = 1;

	// --- Records --------------------------------------------------------------

	// An actor Livery dresses and the pieces it put on them. NPCs wear them locked; the
	// player does not, since a locked piece cannot be taken off in the inventory menu.
	struct Assignment
	{
		RE::FormID                   actor = 0;
		std::vector<Dressing::Piece> pieces;
	};

	// Contract: game thread only, both. The clipboard holds FormIDs, looked up at use:
	// a form created at runtime can go away on a load.
	std::vector<Assignment> g_assignments;
	std::vector<RE::FormID> g_clipboard;

	// Contract: the assigned NPCs, for the event sinks. The thread that sends
	// TESObjectLoadedEvent and TESDeathEvent is not documented, so the sinks read only
	// this, under its lock, and queue the work as SKSE tasks.
	std::mutex              g_managedLock;
	std::vector<RE::FormID> g_managed;

	// Contract: game thread only. The NPC whose locks are off for the preview, or 0.
	RE::FormID g_suspended = 0;

	Assignment* FindAssignment(RE::FormID a_actor)
	{
		const auto found = std::ranges::find(g_assignments, a_actor, &Assignment::actor);
		return found != g_assignments.end() ? &*found : nullptr;
	}

	// Contract: game thread, after every change of g_assignments. The player is left
	// out: nothing is put back on them when they load.
	void SyncManaged()
	{
		std::lock_guard lock{ g_managedLock };
		g_managed.clear();
		for (const auto& assignment : g_assignments) {
			if (assignment.actor != kPlayerRef) {
				g_managed.push_back(assignment.actor);
			}
		}
	}

	bool IsManaged(RE::FormID a_actor)
	{
		std::lock_guard lock{ g_managedLock };
		return std::ranges::find(g_managed, a_actor) != g_managed.end();
	}

	// Takes off what Livery did to them and forgets them; `removeCopies` also takes back
	// the copies it added. Contract: game thread.
	void ReleaseActor(RE::Actor& actor, bool removeCopies)
	{
		const auto id = actor.GetFormID();
		if (const auto* assignment = FindAssignment(id)) {
			Dressing::Release(actor, assignment->pieces, removeCopies);
			std::erase_if(g_assignments, [id](const Assignment& entry) { return entry.actor == id; });
			SyncManaged();
		}
	}

	void Notify(const std::string& text)
	{
		RE::SendHUDMessage::ShowHUDMessage(text.c_str());
		logger::info("{}", text);
	}

	std::string NameOf(RE::Actor& actor)
	{
		const auto* name = actor.GetDisplayFullName();
		return name && *name ? name : std::format("{:08X}", actor.GetFormID());
	}

	std::string JoinNames(const std::vector<RE::TESObjectARMO*>& armors)
	{
		std::string joined;
		for (const auto* armor : armors) {
			const auto* name = armor->GetName();
			joined += (joined.empty() ? "" : ", ") + (name && *name ? std::string{ name } : std::format("{:08X}", armor->GetFormID()));
		}
		return joined;
	}

	// --- Commands -------------------------------------------------------------
	//
	// Contract: game thread (SKSE task from HandleKey), all four.

	void CopyWorn(RE::Actor& actor)
	{
		const auto worn = Dressing::WornArmor(actor);
		if (worn.empty()) {
			Notify(std::format("Livery: {} wears no armor to copy.", NameOf(actor)));
			return;
		}
		g_clipboard.clear();
		for (const auto* armor : worn) {
			g_clipboard.push_back(armor->GetFormID());
		}
		Notify(std::format("Livery: copied {} piece(s) from {}.", worn.size(), NameOf(actor)));
	}

	// An assignment replaces the one they had. NPCs get the copied pieces they lack;
	// the player only when the settings allow it, since it creates items.
	void Assign(RE::Actor& actor)
	{
		if (actor.IsChild()) {
			Notify("Livery: children cannot be dressed.");
			return;
		}
		if (actor.IsDead()) {
			Notify(std::format("Livery: {} is dead.", NameOf(actor)));
			return;
		}
		std::vector<RE::TESObjectARMO*> outfit;
		for (const auto id : g_clipboard) {
			if (auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(id)) {
				outfit.push_back(armor);
			}
		}
		if (outfit.empty()) {
			Notify("Livery: nothing copied yet. Shift+F10 copies what someone wears.");
			return;
		}

		ReleaseActor(actor, true);
		const bool isPlayer = actor.IsPlayerRef();
		auto       result = Dressing::Dress(actor, outfit, !isPlayer, !isPlayer || Settings::PlayerAddsMissingPieces());
		const auto worn = result.worn.size();
		const auto added = std::ranges::count_if(result.worn, &Dressing::Piece::added);
		if (worn > 0) {
			g_assignments.push_back({ actor.GetFormID(), std::move(result.worn) });
			SyncManaged();
		}

		auto text = isPlayer ? std::format("Livery: you now wear {} piece(s)", worn) : std::format("Livery: {} now wears {} piece(s)", NameOf(actor), worn);
		if (added > 0) {
			text += std::format(", {} of them added", added);
		}
		if (!result.notCarried.empty()) {
			text += "; not carried: " + JoinNames(result.notCarried);
		}
		if (!result.refused.empty()) {
			text += "; could not be put on: " + JoinNames(result.refused);
		}
		Notify(text + ".");
	}

	void Unassign(RE::Actor& actor)
	{
		if (!FindAssignment(actor.GetFormID())) {
			Notify(std::format("Livery: {} has no outfit from Livery.", NameOf(actor)));
			return;
		}
		ReleaseActor(actor, true);
		Notify(std::format("Livery: released {}.", NameOf(actor)));
	}

	// The uninstall step. An actor not in memory (a non-persistent NPC whose cell is not
	// loaded) cannot be reached: their locks and copies stay, which never breaks a save.
	void ReleaseEveryone()
	{
		const auto  total = g_assignments.size();
		std::size_t released = 0;
		for (const auto& assignment : std::exchange(g_assignments, {})) {
			if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(assignment.actor)) {
				Dressing::Release(*actor, assignment.pieces, true);
				++released;
			}
		}
		SyncManaged();
		Notify(released == total ? std::format("Livery: released {} character(s).", total) :
								   std::format("Livery: released {} of {} character(s); the others are out of reach and keep their pieces.", released, total));
	}

	// --- Keys -----------------------------------------------------------------

	enum class Command
	{
		kAssign,
		kCopy,
		kUnassign,
		kReleaseEveryone
	};

	// Contract: reads the keyboard's current DirectInput state, as Settings.cpp does.
	bool Held(std::uint32_t a_left, std::uint32_t a_right)
	{
		auto* devices = RE::BSInputDeviceManager::GetSingleton();
		auto* keyboard = devices ? devices->GetKeyboard() : nullptr;
		if (!keyboard) {
			return false;
		}
		const auto& state = keyboard->GetRuntimeData().curState;
		return (state[a_left] & 0x80) != 0 || (state[a_right] & 0x80) != 0;
	}

	// Contract: game thread (SKSE task from HandleKey). Without an actor under the
	// crosshair, the command is for the player.
	void Run(Command a_command, RE::ObjectRefHandle a_target)
	{
		if (a_command == Command::kReleaseEveryone) {
			ReleaseEveryone();
			return;
		}
		const auto ref = a_target.get();
		auto*      actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (!actor) {
			actor = RE::PlayerCharacter::GetSingleton();
		}
		if (!actor) {
			return;
		}
		switch (a_command) {
		case Command::kAssign:
			Assign(*actor);
			break;
		case Command::kCopy:
			CopyWorn(*actor);
			break;
		case Command::kUnassign:
			Unassign(*actor);
			break;
		default:
			break;
		}
	}

	// --- Load, death ----------------------------------------------------------
	//
	// Contract: game thread (SKSE tasks from the event sinks), both.

	void PutPiecesBackOn(RE::FormID a_actor)
	{
		if (a_actor == g_suspended) {
			return;  // the preview has them off on purpose
		}
		auto* assignment = FindAssignment(a_actor);
		auto* actor = assignment ? RE::TESForm::LookupByID<RE::Actor>(a_actor) : nullptr;
		if (!actor) {
			return;
		}
		if (const auto count = Dressing::PutBackOn(*actor, assignment->pieces); count > 0) {
			logger::info("Put {} piece(s) back on or locked again on {:08X}", count, a_actor);
		}
	}

	// The corpse behaves like vanilla: the locks go, the copies stay lootable.
	void ReleaseDead(RE::FormID a_actor)
	{
		auto* actor = FindAssignment(a_actor) ? RE::TESForm::LookupByID<RE::Actor>(a_actor) : nullptr;
		if (!actor) {
			return;
		}
		ReleaseActor(*actor, false);
		logger::info("Released {:08X}, who died", a_actor);
	}

	// --- Co-save --------------------------------------------------------------
	//
	// Record ASGN v1: assignment count; per assignment the actor FormID and piece count;
	// per piece the armor FormID, the ExtraUniqueID's baseID and uniqueID, added, locked.
	// Contract: SKSE calls the three callbacks from the game's save, load and revert, on
	// the game thread.

	template <class T>
	bool Write(const SKSE::SerializationInterface& a_intfc, const T& a_value)
	{
		return a_intfc.WriteRecordData(a_value);
	}

	template <class T>
	bool Read(const SKSE::SerializationInterface& a_intfc, T& a_value)
	{
		return a_intfc.ReadRecordData(a_value) == sizeof(T);
	}

	void OnSave(SKSE::SerializationInterface* a_intfc)
	{
		if (g_assignments.empty()) {
			return;
		}
		if (!a_intfc->OpenRecord(kAssignmentsRecord, kAssignmentsVersion)) {
			logger::error("Could not open the co-save record");
			return;
		}
		bool written = Write(*a_intfc, static_cast<std::uint32_t>(g_assignments.size()));
		for (const auto& [actor, pieces] : g_assignments) {
			written = written && Write(*a_intfc, actor) && Write(*a_intfc, static_cast<std::uint32_t>(pieces.size()));
			for (const auto& [key, added, locked] : pieces) {
				written = written && Write(*a_intfc, key.armor) && Write(*a_intfc, key.baseID) && Write(*a_intfc, key.uniqueID) &&
				          Write(*a_intfc, static_cast<std::uint8_t>(added)) && Write(*a_intfc, static_cast<std::uint8_t>(locked));
			}
		}
		if (!written) {
			logger::error("Could not write the co-save record");
		}
	}

	// Reads one piece; its FormIDs are fixed for the current load order. Returns false
	// when the record ends early; `resolved` is false when a form's plugin is gone.
	bool ReadPiece(const SKSE::SerializationInterface& a_intfc, Dressing::Piece& a_piece, bool& a_resolved)
	{
		std::uint8_t added = 0;
		std::uint8_t locked = 0;
		if (!Read(a_intfc, a_piece.key.armor) || !Read(a_intfc, a_piece.key.baseID) || !Read(a_intfc, a_piece.key.uniqueID) || !Read(a_intfc, added) ||
			!Read(a_intfc, locked)) {
			return false;
		}
		a_piece.added = added != 0;
		a_piece.locked = locked != 0;
		a_resolved = a_intfc.ResolveFormID(a_piece.key.armor, a_piece.key.armor) && a_intfc.ResolveFormID(a_piece.key.baseID, a_piece.key.baseID);
		return true;
	}

	// A piece whose plugin is gone is dropped, and so is an assignment left without
	// pieces or whose actor's plugin is gone.
	std::vector<Assignment> ReadAssignments(const SKSE::SerializationInterface& a_intfc)
	{
		std::vector<Assignment> assignments;
		std::uint32_t           count = 0;
		if (!Read(a_intfc, count)) {
			logger::error("The co-save record is cut short");
			return assignments;
		}
		for (; count > 0; --count) {
			Assignment    assignment;
			std::uint32_t pieces = 0;
			if (!Read(a_intfc, assignment.actor) || !Read(a_intfc, pieces)) {
				logger::error("The co-save record is cut short");
				return assignments;
			}
			const bool actorResolved = a_intfc.ResolveFormID(assignment.actor, assignment.actor);
			for (; pieces > 0; --pieces) {
				Dressing::Piece piece;
				bool            resolved = false;
				if (!ReadPiece(a_intfc, piece, resolved)) {
					logger::error("The co-save record is cut short");
					return assignments;
				}
				if (resolved) {
					assignment.pieces.push_back(piece);
				}
			}
			if (actorResolved && !assignment.pieces.empty()) {
				assignments.push_back(std::move(assignment));
			}
		}
		return assignments;
	}

	void OnLoad(SKSE::SerializationInterface* a_intfc)
	{
		std::uint32_t type = 0;
		std::uint32_t version = 0;
		std::uint32_t length = 0;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type != kAssignmentsRecord) {
				continue;
			}
			if (version != kAssignmentsVersion) {
				logger::warn("Co-save record version {} is unknown; no assignments loaded", version);
				continue;
			}
			g_assignments = ReadAssignments(*a_intfc);
		}
		SyncManaged();
		logger::info("Loaded {} assignment(s) from the co-save", g_assignments.size());
	}

	void OnRevert(SKSE::SerializationInterface*)
	{
		g_assignments.clear();
		g_suspended = 0;
		SyncManaged();
	}

	// --- Event sinks ----------------------------------------------------------

	class LoadedSink final : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
		{
			if (a_event && a_event->loaded && IsManaged(a_event->formID)) {
				const auto id = a_event->formID;
				SKSE::GetTaskInterface()->AddTask([id] { PutPiecesBackOn(id); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		static LoadedSink& Get()
		{
			static LoadedSink sink;
			return sink;
		}
	};

	class DeathSink final : public RE::BSTEventSink<RE::TESDeathEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
		{
			const auto* dying = a_event ? a_event->actorDying.get() : nullptr;
			if (dying && IsManaged(dying->GetFormID())) {
				const auto id = dying->GetFormID();
				SKSE::GetTaskInterface()->AddTask([id] { ReleaseDead(id); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		static DeathSink& Get()
		{
			static DeathSink sink;
			return sink;
		}
	};
}

namespace Assignments
{
	void RegisterCoSave()
	{
		const auto* serialization = SKSE::GetSerializationInterface();
		if (!serialization) {
			logger::error("SKSE serialization interface missing: assignments are not saved");
			return;
		}
		serialization->SetUniqueID(kCoSaveID);
		serialization->SetSaveCallback(OnSave);
		serialization->SetLoadCallback(OnLoad);
		serialization->SetRevertCallback(OnRevert);
	}

	void RegisterSinks()
	{
		auto* events = RE::ScriptEventSourceHolder::GetSingleton();
		if (!events) {
			logger::error("Script event source missing: assigned outfits are not kept on");
			return;
		}
		events->AddEventSink<RE::TESObjectLoadedEvent>(&LoadedSink::Get());
		events->AddEventSink<RE::TESDeathEvent>(&DeathSink::Get());
	}

	bool HandleKey(std::uint32_t a_scanCode, RE::ObjectRefHandle a_target)
	{
		auto command = Command::kAssign;
		if (a_scanCode == kReleaseEveryoneKey) {
			command = Command::kReleaseEveryone;
		} else if (a_scanCode != kAssignKey) {
			return false;
		} else if (Held(Key::kLeftAlt, Key::kRightAlt)) {  // with F10: Alt releases the target, Shift copies, the key alone assigns
			command = Command::kUnassign;
		} else if (Held(Key::kLeftShift, Key::kRightShift)) {
			command = Command::kCopy;
		}
		SKSE::GetTaskInterface()->AddTask([a_target, command] { Run(command, a_target); });
		return true;
	}

	// Release without removing copies takes exactly Livery's locks off (another mod's stay);
	// the pieces then read as unlocked, which PutBackOn sets right again at ResumeLocks.
	void SuspendLocks(RE::Actor& a_actor)
	{
		const auto id = a_actor.GetFormID();
		auto*      assignment = id == kPlayerRef ? nullptr : FindAssignment(id);
		if (!assignment) {
			return;
		}
		g_suspended = id;
		Dressing::Release(a_actor, assignment->pieces, false);
		for (auto& piece : assignment->pieces) {
			piece.locked = false;
		}
	}

	void ResumeLocks(RE::FormID a_actor)
	{
		if (std::exchange(g_suspended, 0) != a_actor) {
			return;
		}
		PutPiecesBackOn(a_actor);
	}
}
