// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants   - the release-everyone key, the player's reference, co-save ids
//   Records     - one assignment per actor; g_managed mirrors the NPCs for the event sinks
//   Release     - Livery's locks and copies off, the actor's own pieces back on; everyone
//   Load, death - pieces put back on when an assigned NPC loads; locks released when one dies
//   Co-save     - save, load (versions 1 and 2), revert
//   Event sinks - TESObjectLoadedEvent, TESDeathEvent
//   API         - RegisterCoSave / RegisterSinks / HandleKey / Assign / Unassign / OutfitOf /
//                 SuspendLocks / ResumeLocks
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

	// The uninstall step. Plugin.cpp checks it before the capture hotkey, so it wins even
	// when the capture hotkey is set to F8 too. F9 is the game's Quick Load.
	constexpr std::uint32_t kReleaseEveryoneKey = RE::BSKeyboardDevice::Keys::Key::kF8;

	// Skyrim.esm always loads at index 00, so the player's reference is always 0x14.
	constexpr RE::FormID kPlayerRef = 0x14;

	constexpr std::uint32_t kCoSaveID = 'LVRY';
	constexpr std::uint32_t kAssignmentsRecord = 'ASGN';
	constexpr std::uint32_t kAssignmentsVersion = 2;
	constexpr std::uint32_t kFirstAssignmentsVersion = 1;  // still read: no outfit id, nothing taken off

	// --- Records --------------------------------------------------------------

	// An actor Livery dresses: the pieces it put on them and their own pieces it took off.
	// NPCs wear the pieces locked; the player does not, since a locked piece cannot be
	// taken off in the inventory menu.
	struct Assignment
	{
		RE::FormID                         actor = 0;
		std::uint32_t                      outfit = 0;  // the library outfit the pieces came from; 0 for none
		std::vector<Dressing::Piece>       pieces;
		std::vector<Dressing::InstanceKey> takenOff;  // their own copies, put back on at release
	};

	// Contract: game thread only.
	std::vector<Assignment> g_assignments;

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

	// --- Release --------------------------------------------------------------

	// Takes off what Livery did to them and forgets them. `removeCopies` also takes back
	// the copies it added; `putBack` puts their own pieces it took off on again, which a
	// corpse does not get: it behaves like vanilla. Returns how many went back on.
	// Contract: game thread.
	std::size_t ReleaseActor(RE::Actor& actor, bool removeCopies, bool putBack)
	{
		const auto found = std::ranges::find(g_assignments, actor.GetFormID(), &Assignment::actor);
		if (found == g_assignments.end()) {
			return 0;
		}
		const auto assignment = std::move(*found);
		g_assignments.erase(found);
		SyncManaged();
		Dressing::Release(actor, assignment.pieces, removeCopies);
		return putBack ? Dressing::PutBack(actor, assignment.takenOff) : 0;
	}

	// The uninstall step. An actor not in memory (a non-persistent NPC whose cell is not
	// loaded) cannot be reached: their locks and copies stay, which never breaks a save.
	// Contract: game thread (SKSE task from HandleKey).
	void ReleaseEveryone()
	{
		const auto  total = g_assignments.size();
		std::size_t released = 0;
		for (const auto& assignment : std::exchange(g_assignments, {})) {
			if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(assignment.actor)) {
				Dressing::Release(*actor, assignment.pieces, true);
				Dressing::PutBack(*actor, assignment.takenOff);
				++released;
			}
		}
		SyncManaged();
		Notify(released == total ? std::format("Livery: released {} character(s).", total) :
								   std::format("Livery: released {} of {} character(s); the others are out of reach and keep their pieces.", released, total));
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

	// The corpse behaves like vanilla: the locks go, the copies stay lootable, and their
	// own pieces stay in the inventory.
	void ReleaseDead(RE::FormID a_actor)
	{
		auto* actor = FindAssignment(a_actor) ? RE::TESForm::LookupByID<RE::Actor>(a_actor) : nullptr;
		if (!actor) {
			return;
		}
		ReleaseActor(*actor, false, false);
		logger::info("Released {:08X}, who died", a_actor);
	}

	// --- Co-save --------------------------------------------------------------
	//
	// Record ASGN v2: assignment count; per assignment the actor FormID, the outfit id,
	// the piece count, per piece its instance key (armor FormID, the ExtraUniqueID's baseID
	// and uniqueID), added and locked; then the count of their own copies taken off and
	// their instance keys. v1 has no outfit id and no taken-off copies.
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

	bool WriteKey(const SKSE::SerializationInterface& a_intfc, const Dressing::InstanceKey& a_key)
	{
		return Write(a_intfc, a_key.armor) && Write(a_intfc, a_key.baseID) && Write(a_intfc, a_key.uniqueID);
	}

	// Reads one instance key; its FormIDs are fixed for the current load order. Returns
	// false when the record ends early; `resolved` is false when a form's plugin is gone.
	bool ReadKey(const SKSE::SerializationInterface& a_intfc, Dressing::InstanceKey& a_key, bool& a_resolved)
	{
		if (!Read(a_intfc, a_key.armor) || !Read(a_intfc, a_key.baseID) || !Read(a_intfc, a_key.uniqueID)) {
			return false;
		}
		a_resolved = a_intfc.ResolveFormID(a_key.armor, a_key.armor) && a_intfc.ResolveFormID(a_key.baseID, a_key.baseID);
		return true;
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
		for (const auto& [actor, outfit, pieces, takenOff] : g_assignments) {
			written = written && Write(*a_intfc, actor) && Write(*a_intfc, outfit) && Write(*a_intfc, static_cast<std::uint32_t>(pieces.size()));
			for (const auto& [key, added, locked] : pieces) {
				written = written && WriteKey(*a_intfc, key) && Write(*a_intfc, static_cast<std::uint8_t>(added)) &&
				          Write(*a_intfc, static_cast<std::uint8_t>(locked));
			}
			written = written && Write(*a_intfc, static_cast<std::uint32_t>(takenOff.size()));
			for (const auto& key : takenOff) {
				written = written && WriteKey(*a_intfc, key);
			}
		}
		if (!written) {
			logger::error("Could not write the co-save record");
		}
	}

	bool ReadPiece(const SKSE::SerializationInterface& a_intfc, Dressing::Piece& a_piece, bool& a_resolved)
	{
		std::uint8_t added = 0;
		std::uint8_t locked = 0;
		if (!ReadKey(a_intfc, a_piece.key, a_resolved) || !Read(a_intfc, added) || !Read(a_intfc, locked)) {
			return false;
		}
		a_piece.added = added != 0;
		a_piece.locked = locked != 0;
		return true;
	}

	// Reads one assignment of a `a_version` record. Returns false when the record ends early.
	// A piece or taken-off copy whose plugin is gone is dropped.
	bool ReadAssignment(const SKSE::SerializationInterface& a_intfc, std::uint32_t a_version, Assignment& a_assignment, bool& a_actorResolved)
	{
		std::uint32_t pieces = 0;
		if (!Read(a_intfc, a_assignment.actor) || (a_version != kFirstAssignmentsVersion && !Read(a_intfc, a_assignment.outfit)) ||
			!Read(a_intfc, pieces)) {
			return false;
		}
		a_actorResolved = a_intfc.ResolveFormID(a_assignment.actor, a_assignment.actor);
		for (; pieces > 0; --pieces) {
			Dressing::Piece piece;
			bool            resolved = false;
			if (!ReadPiece(a_intfc, piece, resolved)) {
				return false;
			}
			if (resolved) {
				a_assignment.pieces.push_back(piece);
			}
		}
		if (a_version == kFirstAssignmentsVersion) {
			return true;
		}
		std::uint32_t takenOff = 0;
		if (!Read(a_intfc, takenOff)) {
			return false;
		}
		for (; takenOff > 0; --takenOff) {
			Dressing::InstanceKey key;
			bool                  resolved = false;
			if (!ReadKey(a_intfc, key, resolved)) {
				return false;
			}
			if (resolved) {
				a_assignment.takenOff.push_back(key);
			}
		}
		return true;
	}

	// An assignment left without pieces, or whose actor's plugin is gone, is dropped.
	std::vector<Assignment> ReadAssignments(const SKSE::SerializationInterface& a_intfc, std::uint32_t a_version)
	{
		std::vector<Assignment> assignments;
		std::uint32_t           count = 0;
		if (!Read(a_intfc, count)) {
			logger::error("The co-save record is cut short");
			return assignments;
		}
		for (; count > 0; --count) {
			Assignment assignment;
			bool       actorResolved = false;
			if (!ReadAssignment(a_intfc, a_version, assignment, actorResolved)) {
				logger::error("The co-save record is cut short");
				return assignments;
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
			if (version != kAssignmentsVersion && version != kFirstAssignmentsVersion) {
				logger::warn("Co-save record version {} is unknown; no assignments loaded", version);
				continue;
			}
			g_assignments = ReadAssignments(*a_intfc, version);
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

	bool HandleKey(std::uint32_t a_scanCode)
	{
		if (a_scanCode != kReleaseEveryoneKey) {
			return false;
		}
		SKSE::GetTaskInterface()->AddTask([] { ReleaseEveryone(); });
		return true;
	}

	void Assign(RE::Actor& a_actor, const std::vector<RE::TESObjectARMO*>& a_armor, std::uint32_t a_outfit)
	{
		if (a_actor.IsChild()) {
			Notify("Livery: children cannot be dressed.");
			return;
		}
		if (a_actor.IsDead()) {
			Notify(std::format("Livery: {} is dead.", NameOf(a_actor)));
			return;
		}
		if (a_armor.empty()) {
			return;
		}

		// Their own pieces from an earlier assignment go back on first, so they are what
		// this one takes off and notes.
		ReleaseActor(a_actor, true, true);
		const bool isPlayer = a_actor.IsPlayerRef();
		auto       result = Dressing::Dress(a_actor, a_armor, !isPlayer, !isPlayer || Settings::PlayerAddsMissingPieces());
		const auto worn = result.worn.size();
		const auto added = std::ranges::count_if(result.worn, &Dressing::Piece::added);
		if (worn > 0) {
			g_assignments.push_back({ a_actor.GetFormID(), a_outfit, std::move(result.worn), std::move(result.takenOff) });
			SyncManaged();
		}

		auto text = isPlayer ? std::format("Livery: you now wear {} piece(s)", worn) : std::format("Livery: {} now wears {} piece(s)", NameOf(a_actor), worn);
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

	void Unassign(RE::Actor& a_actor)
	{
		if (!FindAssignment(a_actor.GetFormID())) {
			return;
		}
		const auto putBack = ReleaseActor(a_actor, true, true);
		logger::info("Put {} of their own piece(s) back on {:08X}", putBack, a_actor.GetFormID());
		Notify(std::format("Livery: released {}.", NameOf(a_actor)));
	}

	std::optional<std::uint32_t> OutfitOf(RE::FormID a_actor)
	{
		const auto* assignment = FindAssignment(a_actor);
		return assignment ? std::make_optional(assignment->outfit) : std::nullopt;
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
