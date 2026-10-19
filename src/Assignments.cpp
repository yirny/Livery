// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants   - the release-everyone key, the player's reference, co-save ids
//   Records     - one assignment per actor; g_managed mirrors the NPCs for the event sinks
//   Dressing    - which outfit a situation resolves to, and putting it on in place of the
//                 one worn
//   Release     - Livery's locks and copies off, the actor's own pieces back on; everyone
//   Load, death - pieces put back on when an assigned NPC loads; locks released when one dies
//   Co-save     - save, load (versions 1 to 3), revert
//   Event sinks - TESObjectLoadedEvent, TESDeathEvent
//   API         - RegisterCoSave / RegisterSinks / HandleKey / Assign / SetSituation /
//                 ClearSituations / WearFor / ActorsWithSituations / Unassign / ChoicesOf / SuspendLocks / ResumeLocks /
//                 MarkAddedCopies / UnmarkAddedCopies
#include "PCH.h"

#include "Assignments.h"

#include "Dressing.h"
#include "Library.h"
#include "Settings.h"

#include <algorithm>
#include <array>
#include <format>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using Situations::Situation;

	// --- Constants ------------------------------------------------------------

	// The uninstall step. Plugin.cpp checks it before the capture hotkey, so it wins even
	// when the capture hotkey is set to F8 too. F9 is the game's Quick Load.
	constexpr std::uint32_t kReleaseEveryoneKey = RE::BSKeyboardDevice::Keys::Key::kF8;

	// Skyrim.esm always loads at index 00, so the player's reference is always 0x14.
	constexpr RE::FormID kPlayerRef = 0x14;

	constexpr std::uint32_t kCoSaveID = 'LVRY';
	constexpr std::uint32_t kAssignmentsRecord = 'ASGN';
	constexpr std::uint32_t kAssignmentsVersion = 3;
	constexpr std::uint32_t kSecondAssignmentsVersion = 2;  // still read: no default armor, no situations
	constexpr std::uint32_t kFirstAssignmentsVersion = 1;   // still read: as v2 without the outfit id and the pieces taken off

	// --- Records --------------------------------------------------------------

	// An actor Livery dresses: the pieces it has on them, their own pieces it took off, the
	// default outfit and the outfits chosen for their situations. NPCs wear the pieces
	// locked; the player does not, since a locked piece cannot be taken off in the
	// inventory menu.
	struct Assignment
	{
		RE::FormID                                               actor = 0;
		std::vector<Dressing::Piece>                             pieces;                      // on them now, for `worn`
		std::vector<Dressing::InstanceKey>                       takenOff;                    // their own copies, put back on at release
		std::uint32_t                                            defaultOutfit = 0;           // the library outfit the default pieces came from; 0 for none
		std::vector<RE::FormID>                                  defaultArmor;                // the default outfit's armor; empty for none (their own clothes)
		std::array<std::uint32_t, Situations::kChoosable.size()> situations{};                // library outfit per situation (Situations::Index); 0 for none
		Situation                                                worn = Situation::kDefault;  // what `pieces` are for
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

	// Contract: the pointer goes stale at the next change of g_assignments.
	Assignment& FindOrAddAssignment(RE::FormID a_actor)
	{
		if (auto* found = FindAssignment(a_actor)) {
			return *found;
		}
		return g_assignments.emplace_back(Assignment{ .actor = a_actor });
	}

	bool HasSituations(const Assignment& a_assignment)
	{
		return std::ranges::any_of(a_assignment.situations, [](std::uint32_t a_outfit) { return a_outfit != 0; });
	}

	// An assignment with nothing on the actor and no situation chosen is no assignment:
	// they keep what they wear, as a default outfit that could not be put on leaves them.
	void DropIfEmpty(RE::FormID a_actor)
	{
		std::erase_if(g_assignments, [a_actor](const Assignment& a_assignment) {
			return a_assignment.actor == a_actor && a_assignment.pieces.empty() && !HasSituations(a_assignment);
		});
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

	std::string QuotedOutfitName(std::uint32_t a_outfit)
	{
		const auto* stored = Library::Find(a_outfit);
		return stored ? std::format("\"{}\"", stored->name) : "an outfit no longer in the library";
	}

	// --- Dressing -------------------------------------------------------------
	//
	// Contract: game thread, all.

	// The first situation they are in that they have an outfit for, else the default.
	Situation Resolve(const Assignment& a_assignment, const Situations::Applicable& a_now)
	{
		for (std::size_t i = 0; i < a_now.count; ++i) {
			if (a_assignment.situations[Situations::Index(a_now.items[i])] != 0) {
				return a_now.items[i];
			}
		}
		return Situation::kDefault;
	}

	// The armor they wear for `a_situation`: the default's forms, or the chosen library
	// outfit's loaded pieces. Empty for no default or an outfit gone from the library.
	std::vector<RE::TESObjectARMO*> ArmorFor(const Assignment& a_assignment, Situation a_situation)
	{
		if (a_situation != Situation::kDefault) {
			const auto* outfit = Library::Find(a_assignment.situations[Situations::Index(a_situation)]);
			return outfit ? Library::LoadedPieces(*outfit) : std::vector<RE::TESObjectARMO*>{};
		}
		std::vector<RE::TESObjectARMO*> armor;
		for (const auto id : a_assignment.defaultArmor) {
			if (auto* loaded = RE::TESForm::LookupByID<RE::TESObjectARMO>(id)) {
				armor.push_back(loaded);
			}
		}
		return armor;
	}

	// Puts the outfit for `a_situation` on them in place of what Livery has on them. Their
	// own pieces from before go back on first, so they are what this dressing takes off
	// and notes. With no armor for the situation they end up in their own clothes.
	Dressing::Result Wear(Assignment& a_assignment, RE::Actor& a_actor, Situation a_situation)
	{
		Dressing::Release(a_actor, a_assignment.pieces, true);
		Dressing::PutBack(a_actor, a_assignment.takenOff);
		const bool isPlayer = a_actor.IsPlayerRef();
		auto       result = Dressing::Dress(a_actor, ArmorFor(a_assignment, a_situation), !isPlayer, !isPlayer || Settings::PlayerAddsMissingPieces());
		a_assignment.pieces = result.worn;
		a_assignment.takenOff = result.takenOff;
		a_assignment.worn = a_situation;
		return result;
	}

	// Dresses them for `a_now` unless they already wear what it resolves to (`a_force`
	// dresses them anyway). A situation outfit that puts nothing on falls back to the
	// default. Returns the dressing's result, or nullopt when nothing changed; the
	// assignment's `worn` tells what went on.
	std::optional<Dressing::Result> Redress(Assignment& a_assignment, RE::Actor& a_actor, const Situations::Applicable& a_now, bool a_force)
	{
		const auto situation = Resolve(a_assignment, a_now);
		if (!a_force && situation == a_assignment.worn) {
			return std::nullopt;
		}
		auto result = Wear(a_assignment, a_actor, situation);
		if (result.worn.empty() && situation != Situation::kDefault) {
			logger::info("No piece of the {} outfit went on {:08X}; the default outfit instead", Situations::Name(situation), a_actor.GetFormID());
			result = Wear(a_assignment, a_actor, Situation::kDefault);
		}
		return result;
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
	// Record ASGN v3: assignment count; per assignment the actor FormID, the default outfit
	// id, the piece count, per piece its instance key (armor FormID, the ExtraUniqueID's
	// baseID and uniqueID), added and locked; the count of their own copies taken off and
	// their instance keys; the default armor count and its FormIDs; one outfit id per
	// situation in Situations::kChoosable order; the situation worn (one byte). v2 ends
	// after the copies taken off, v1 after the pieces and has no outfit id: both get the
	// pieces' armor as the default.
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

	bool WriteAssignment(const SKSE::SerializationInterface& a_intfc, const Assignment& a_assignment)
	{
		bool written = Write(a_intfc, a_assignment.actor) && Write(a_intfc, a_assignment.defaultOutfit) &&
		               Write(a_intfc, static_cast<std::uint32_t>(a_assignment.pieces.size()));
		for (const auto& [key, added, locked] : a_assignment.pieces) {
			written = written && WriteKey(a_intfc, key) && Write(a_intfc, static_cast<std::uint8_t>(added)) && Write(a_intfc, static_cast<std::uint8_t>(locked));
		}
		written = written && Write(a_intfc, static_cast<std::uint32_t>(a_assignment.takenOff.size()));
		for (const auto& key : a_assignment.takenOff) {
			written = written && WriteKey(a_intfc, key);
		}
		written = written && Write(a_intfc, static_cast<std::uint32_t>(a_assignment.defaultArmor.size()));
		for (const auto armor : a_assignment.defaultArmor) {
			written = written && Write(a_intfc, armor);
		}
		for (const auto outfit : a_assignment.situations) {
			written = written && Write(a_intfc, outfit);
		}
		return written && Write(a_intfc, static_cast<std::uint8_t>(a_assignment.worn));
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
		for (const auto& assignment : g_assignments) {
			written = written && WriteAssignment(*a_intfc, assignment);
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

	// Reads the pieces and, past v1, the copies taken off. Returns false when the record
	// ends early. A piece or copy whose plugin is gone is dropped.
	bool ReadWorn(const SKSE::SerializationInterface& a_intfc, std::uint32_t a_version, Assignment& a_assignment)
	{
		std::uint32_t pieces = 0;
		if (!Read(a_intfc, pieces)) {
			return false;
		}
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

	// Reads the default armor, the situation outfits and the situation worn (v3). A
	// default armor whose plugin is gone is dropped. Returns false when the record ends
	// early or names a situation this version does not have.
	bool ReadChoices(const SKSE::SerializationInterface& a_intfc, Assignment& a_assignment)
	{
		std::uint32_t armors = 0;
		if (!Read(a_intfc, armors)) {
			return false;
		}
		for (; armors > 0; --armors) {
			RE::FormID armor = 0;
			if (!Read(a_intfc, armor)) {
				return false;
			}
			if (a_intfc.ResolveFormID(armor, armor)) {
				a_assignment.defaultArmor.push_back(armor);
			}
		}
		for (auto& outfit : a_assignment.situations) {
			if (!Read(a_intfc, outfit)) {
				return false;
			}
		}
		std::uint8_t worn = 0;
		if (!Read(a_intfc, worn) || worn > Situations::kChoosable.size()) {
			return false;
		}
		a_assignment.worn = static_cast<Situation>(worn);
		return true;
	}

	// Reads one assignment of a `a_version` record. Returns false when the record ends early.
	bool ReadAssignment(const SKSE::SerializationInterface& a_intfc, std::uint32_t a_version, Assignment& a_assignment, bool& a_actorResolved)
	{
		if (!Read(a_intfc, a_assignment.actor) || (a_version != kFirstAssignmentsVersion && !Read(a_intfc, a_assignment.defaultOutfit))) {
			return false;
		}
		a_actorResolved = a_intfc.ResolveFormID(a_assignment.actor, a_assignment.actor);
		if (!ReadWorn(a_intfc, a_version, a_assignment)) {
			return false;
		}
		if (a_version == kAssignmentsVersion) {
			return ReadChoices(a_intfc, a_assignment);
		}
		// Older records know only what went on: those pieces make the default.
		for (const auto& piece : a_assignment.pieces) {
			a_assignment.defaultArmor.push_back(piece.key.armor);
		}
		return true;
	}

	// An assignment with nothing on the actor and no situation chosen, or whose actor's
	// plugin is gone, is dropped.
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
			if (actorResolved && (!assignment.pieces.empty() || HasSituations(assignment))) {
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
			if (version != kAssignmentsVersion && version != kSecondAssignmentsVersion && version != kFirstAssignmentsVersion) {
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

		const auto id = a_actor.GetFormID();
		auto&      assignment = FindOrAddAssignment(id);
		assignment.defaultOutfit = a_outfit;
		assignment.defaultArmor.clear();
		for (const auto* armor : a_armor) {
			assignment.defaultArmor.push_back(armor->GetFormID());
		}
		// The new default goes on even when they already wore the default; a situation
		// outfit they wear stays on and the default waits for its turn.
		const auto now = Situations::Evaluate(a_actor);
		const auto result = Redress(assignment, a_actor, now, Resolve(assignment, now) == Situation::kDefault);
		const auto wornFor = assignment.worn;  // read before DropIfEmpty, which may end the record
		DropIfEmpty(id);
		SyncManaged();
		if (!result) {
			Notify(std::format("Livery: {} will wear {} piece(s) by default; dressed for {} now.", NameOf(a_actor), a_armor.size(), Situations::Name(wornFor)));
			return;
		}

		const auto worn = result->worn.size();
		const auto added = std::ranges::count_if(result->worn, &Dressing::Piece::added);
		auto       text = a_actor.IsPlayerRef() ? std::format("Livery: you now wear {} piece(s)", worn) : std::format("Livery: {} now wears {} piece(s)", NameOf(a_actor), worn);
		if (wornFor != Situation::kDefault) {
			text += std::format(" for {}", Situations::Name(wornFor));
		}
		if (added > 0) {
			text += std::format(", {} of them added", added);
		}
		if (!result->notCarried.empty()) {
			text += "; not carried: " + JoinNames(result->notCarried);
		}
		if (!result->refused.empty()) {
			text += "; could not be put on: " + JoinNames(result->refused);
		}
		Notify(text + ".");
	}

	void SetSituation(RE::Actor& a_actor, Situations::Situation a_situation, std::uint32_t a_outfit)
	{
		if (a_actor.IsChild()) {
			Notify("Livery: children cannot be dressed.");
			return;
		}
		if (a_actor.IsDead()) {
			Notify(std::format("Livery: {} is dead.", NameOf(a_actor)));
			return;
		}

		const auto id = a_actor.GetFormID();
		auto&      assignment = FindOrAddAssignment(id);
		assignment.situations[Situations::Index(a_situation)] = a_outfit;
		// Another outfit for the situation they wear goes on in its place.
		const bool replacing = a_outfit != 0 && assignment.worn == a_situation;
		Redress(assignment, a_actor, Situations::Evaluate(a_actor), replacing);
		const auto worn = assignment.worn;  // read before DropIfEmpty, which may end the record
		DropIfEmpty(id);
		SyncManaged();

		const auto name = Situations::Name(a_situation);
		if (a_outfit == 0) {
			Notify(std::format("Livery: {} has no outfit for {} any more.", NameOf(a_actor), name));
		} else if (worn == a_situation) {
			Notify(std::format("Livery: {} now wears {} for {}.", NameOf(a_actor), QuotedOutfitName(a_outfit), name));
		} else {
			Notify(std::format("Livery: {} will wear {} for {}.", NameOf(a_actor), QuotedOutfitName(a_outfit), name));
		}
	}

	void ClearSituations(RE::Actor& a_actor, const std::vector<Situations::Situation>& a_situations)
	{
		const auto id = a_actor.GetFormID();
		auto*      assignment = FindAssignment(id);
		if (!assignment || a_situations.empty()) {
			return;
		}
		if (a_actor.IsDead()) {
			Notify(std::format("Livery: {} is dead.", NameOf(a_actor)));
			return;
		}

		std::string names;
		for (const auto situation : a_situations) {
			assignment->situations[Situations::Index(situation)] = 0;
			names += std::format("{}{}", names.empty() ? "" : ", ", Situations::Name(situation));
		}
		// Only a cleared situation they wear changes the outfit they resolve to.
		Redress(*assignment, a_actor, Situations::Evaluate(a_actor), false);
		DropIfEmpty(id);
		SyncManaged();
		Notify(std::format("Livery: {} has no outfit for {} any more.", NameOf(a_actor), names));
	}

	bool WearFor(RE::Actor& a_actor, const Situations::Applicable& a_now)
	{
		auto* assignment = FindAssignment(a_actor.GetFormID());
		if (!assignment) {
			return false;
		}
		if (!Redress(*assignment, a_actor, a_now, false)) {
			return false;
		}
		logger::info("Dressed {:08X} for {}: {} piece(s) on", a_actor.GetFormID(), Situations::Name(assignment->worn), assignment->pieces.size());
		return true;
	}

	std::vector<RE::FormID> ActorsWithSituations()
	{
		std::vector<RE::FormID> actors;
		for (const auto& assignment : g_assignments) {
			if (HasSituations(assignment)) {
				actors.push_back(assignment.actor);
			}
		}
		return actors;
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

	std::optional<Choices> ChoicesOf(RE::FormID a_actor)
	{
		const auto* assignment = FindAssignment(a_actor);
		if (!assignment) {
			return std::nullopt;
		}
		return Choices{
			.dressed = !assignment->pieces.empty(),
			.hasDefault = !assignment->defaultArmor.empty(),
			.defaultOutfit = assignment->defaultOutfit,
			.situations = assignment->situations,
			.worn = assignment->worn,
		};
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

	// The player's own list is never filtered: a copy added to the player is the player's item.
	std::vector<Dressing::InstanceKey> MarkAddedCopies(RE::Actor& a_actor)
	{
		const auto  id = a_actor.GetFormID();
		const auto* assignment = id == kPlayerRef ? nullptr : FindAssignment(id);
		const auto* base = assignment ? a_actor.GetActorBase() : nullptr;
		if (!base || !base->defaultOutfit) {
			return {};
		}
		return Dressing::MarkAsOutfitItems(a_actor, assignment->pieces, base->defaultOutfit->GetFormID());
	}

	void UnmarkAddedCopies(RE::Actor& a_actor, const std::vector<Dressing::InstanceKey>& a_keys)
	{
		Dressing::UnmarkOutfitItems(a_actor, a_keys);
	}
}
