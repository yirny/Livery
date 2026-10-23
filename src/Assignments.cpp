// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants   - the player's reference, co-save ids
//   Records     - one assignment per actor, with its rows (OutfitRows); g_managed mirrors the
//                 NPCs for the event sinks
//   Dressing    - which row a situation resolves to and the outfit it wears (a random row's
//                 pick), and putting it on in place of the one worn
//   Release     - Livery's locks and copies off, the actor's own pieces back on
//   Load, death - pieces put back on when an assigned NPC loads; locks released when one dies
//   Co-save     - save, load (versions 1 to 4; older ones turned into rows; the rows of
//                 situations removed in situations.json dropped), revert
//   Event sinks - TESObjectLoadedEvent, TESDeathEvent
//   API         - RegisterCoSave / RegisterSinks / ReleaseEveryone / Assign / Change / SetRows / Refresh / WearFor /
//                 ActorsWithSituations / UsesOf / HoldersOf / ForgetOutfit / ForgetSituation / Unassign / ChoicesOf /
//                 SuspendLocks / ResumeLocks / MarkAddedCopies / UnmarkAddedCopies
#include "PCH.h"

#include "Assignments.h"

#include "Dressing.h"
#include "Library.h"
#include "Settings.h"
#include "SituationStore.h"

#include <algorithm>
#include <array>
#include <format>
#include <mutex>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using OutfitRows::RowOf;
	using Situations::Situation;

	// --- Constants ------------------------------------------------------------

	// Skyrim.esm always loads at index 00, so the player's reference is always 0x14.
	constexpr RE::FormID kPlayerRef = 0x14;

	constexpr std::uint32_t kCoSaveID = 'LVRY';
	constexpr std::uint32_t kAssignmentsRecord = 'ASGN';
	constexpr std::uint32_t kAssignmentsVersion = 4;
	constexpr std::uint32_t kThirdAssignmentsVersion = 3;   // still read: one outfit per situation, the default's armor, no rows
	constexpr std::uint32_t kSecondAssignmentsVersion = 2;  // still read: as v3 without the default armor and the situations
	constexpr std::uint32_t kFirstAssignmentsVersion = 1;   // still read: as v2 without the outfit id and the pieces taken off

	// --- Records --------------------------------------------------------------

	// An actor Livery dresses: the pieces it has on them, their own pieces it took off, and
	// their rows, the Default and the situations. NPCs wear the pieces locked; the player
	// does not, since a locked piece cannot be taken off in the inventory menu.
	struct Assignment
	{
		RE::FormID                         actor = 0;
		std::vector<Dressing::Piece>       pieces;    // on them now, for `worn`
		std::vector<Dressing::InstanceKey> takenOff;  // their own copies, put back on at release
		OutfitRows::Rows                   rows;      // the Default, then the situations
		// The Default's armor not in the wardrobe, worn while its row wears no outfit; empty
		// for none (their own clothes).
		std::vector<RE::FormID> defaultPieces;
		Situation               worn = Situation::kDefault;  // the row `pieces` are for
		std::uint32_t           wornOutfit = 0;              // the library outfit `pieces` are from; 0 for none or the Default's pieces
	};

	// Contract: game thread only.
	std::vector<Assignment> g_assignments;

	// Contract: game thread only. Draws the random rows' picks.
	std::mt19937 g_random{ std::random_device{}() };

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
		return std::ranges::any_of(Situations::kChoosable, [&a_assignment](Situation a_situation) { return OutfitRows::HoldsAny(a_assignment.rows[RowOf(a_situation)]); });
	}

	// An assignment with nothing on the actor and no situation row holding an outfit is no
	// assignment: they keep what they wear, as a default outfit that could not be put on
	// leaves them.
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

	// What the HUD adds about a dressing: the pieces left out and those the engine refused.
	std::string DressingNotes(const Dressing::Result& a_result)
	{
		std::string notes;
		if (!a_result.notCarried.empty()) {
			notes += "; not carried: " + JoinNames(a_result.notCarried);
		}
		if (!a_result.refused.empty()) {
			notes += "; could not be put on: " + JoinNames(a_result.refused);
		}
		return notes;
	}

	std::string JoinSituations(const std::vector<Situation>& a_situations)
	{
		std::string joined;
		for (const auto situation : a_situations) {
			joined += std::format("{}{}", joined.empty() ? "" : ", ", SituationStore::Name(situation));
		}
		return joined;
	}

	std::string QuotedOutfitName(std::uint32_t a_outfit)
	{
		const auto* stored = Library::Find(a_outfit);
		return stored ? std::format("\"{}\"", stored->name) : "an outfit no longer in the wardrobe";
	}

	// The HUD line for a redress because a situation began or ended (WearFor): what they wear
	// now, ending in the description of the situation it is for.
	std::string SituationChangeText(RE::Actor& a_actor, const Assignment& a_assignment)
	{
		const bool player = a_actor.IsPlayerRef();
		const auto who = player ? std::string("you now wear") : std::format("{} now wears", NameOf(a_actor));
		if (a_assignment.pieces.empty()) {
			return std::format("Livery: {} {} own clothes.", who, player ? "your" : "their");
		}
		const auto suffix = SituationStore::Suffix(a_assignment.worn);
		if (a_assignment.worn == Situation::kDefault) {
			return std::format("Livery: {} the default outfit{}.", who, suffix);
		}
		return std::format("Livery: {} {}{}.", who, QuotedOutfitName(a_assignment.wornOutfit), suffix);
	}

	std::vector<RE::FormID> FormIDsOf(const std::vector<RE::TESObjectARMO*>& a_armor)
	{
		std::vector<RE::FormID> ids;
		for (const auto* armor : a_armor) {
			ids.push_back(armor->GetFormID());
		}
		return ids;
	}

	// --- Dressing -------------------------------------------------------------
	//
	// Contract: game thread, all.

	std::size_t RandomIndex(std::size_t a_count)
	{
		return std::uniform_int_distribution<std::size_t>{ 0, a_count - 1 }(g_random);
	}

	// How Redress dresses them, besides the situations they are in.
	struct RedressOptions
	{
		bool                            force = false;  // dress them even when they wear what is due
		std::vector<RE::TESObjectARMO*> leaveOut;       // stays out of this dressing only
		// An outfit just given to `givenRows` (Assign, Change): a random one of them worn now
		// wears it as its pick.
		std::uint32_t          given = 0;
		std::vector<Situation> givenRows;
	};

	bool InLibrary(std::uint32_t a_outfit)
	{
		return Library::Find(a_outfit) != nullptr;
	}

	// Their rows as dressing sees them (OutfitRows::WithoutGone): an outfit another save's row
	// names after it left the library is never picked, and a situation row with only such
	// outfits is passed over. The record keeps them.
	OutfitRows::Rows LiveRows(const Assignment& a_assignment)
	{
		auto rows = a_assignment.rows;
		for (auto& row : rows) {
			row = OutfitRows::WithoutGone(std::move(row), InLibrary);
		}
		return rows;
	}

	// The library outfit row `a_row` of `a_rows` (LiveRows) wears now, 0 for none
	// (OutfitRows::Wears): a random row keeps the pick they wear for it while it is on the
	// list, or wears the outfit just given.
	std::uint32_t OutfitFor(const Assignment& a_assignment, const OutfitRows::Rows& a_rows, Situation a_row, const RedressOptions& a_options)
	{
		const bool given = a_options.given != 0 && std::ranges::find(a_options.givenRows, a_row) != a_options.givenRows.end();
		const auto kept = given ? a_options.given : (a_assignment.worn == a_row ? a_assignment.wornOutfit : 0);
		return OutfitRows::Wears(a_rows[RowOf(a_row)], kept, RandomIndex);
	}

	// The armor row `a_row` wears as `a_outfit`: the library outfit's loaded pieces, or with
	// none the Default's pieces. Empty for an outfit gone from the library or nothing to wear.
	std::vector<RE::TESObjectARMO*> ArmorFor(const Assignment& a_assignment, Situation a_row, std::uint32_t a_outfit)
	{
		if (a_outfit != 0) {
			const auto* outfit = Library::Find(a_outfit);
			return outfit ? Library::LoadedPieces(*outfit) : std::vector<RE::TESObjectARMO*>{};
		}
		std::vector<RE::TESObjectARMO*> armor;
		if (a_row != Situation::kDefault) {
			return armor;
		}
		for (const auto id : a_assignment.defaultPieces) {
			if (auto* loaded = RE::TESForm::LookupByID<RE::TESObjectARMO>(id)) {
				armor.push_back(loaded);
			}
		}
		return armor;
	}

	// Puts row `a_row`'s `a_outfit` on them in place of what Livery has on them, less
	// `a_leaveOut`. Their own pieces from before go back on first, so they are what this
	// dressing takes off and notes. With no armor for the row they end up in their own
	// clothes. A random row's pick is recorded as its `chosen`, which turning random off
	// keeps on (OutfitRows::SetRandom).
	Dressing::Result Wear(Assignment& a_assignment, RE::Actor& a_actor, Situation a_row, std::uint32_t a_outfit, const std::vector<RE::TESObjectARMO*>& a_leaveOut)
	{
		Dressing::Release(a_actor, a_assignment.pieces, true);
		Dressing::PutBack(a_actor, a_assignment.takenOff);
		auto armor = ArmorFor(a_assignment, a_row, a_outfit);
		std::erase_if(armor, [&a_leaveOut](const RE::TESObjectARMO* a_piece) { return std::ranges::find(a_leaveOut, a_piece) != a_leaveOut.end(); });
		const bool isPlayer = a_actor.IsPlayerRef();
		auto       result = Dressing::Dress(a_actor, armor, !isPlayer, !isPlayer || Settings::PlayerAddsMissingPieces());
		a_assignment.pieces = result.worn;
		a_assignment.takenOff = result.takenOff;
		a_assignment.worn = a_row;
		a_assignment.wornOutfit = a_outfit;
		if (auto& row = a_assignment.rows[RowOf(a_row)]; row.random && a_outfit != 0) {
			row.chosen = a_outfit;  // on the list: OutfitRows::Wears picks from it
		}
		return result;
	}

	// Dresses them for `a_now` unless they already wear the row it resolves to and the
	// outfit that row wears (`force` dresses them anyway), the rows as LiveRows sees them. A
	// situation outfit that puts nothing on falls back to the Default. Returns the dressing's
	// result, or nullopt when nothing changed; the assignment's `worn` and `wornOutfit` tell
	// what went on.
	std::optional<Dressing::Result> Redress(Assignment& a_assignment, RE::Actor& a_actor, const Situations::Applicable& a_now, const RedressOptions& a_options = {})
	{
		const auto rows = LiveRows(a_assignment);
		const auto row = OutfitRows::Resolve(rows, a_now);
		const auto outfit = OutfitFor(a_assignment, rows, row, a_options);
		if (!a_options.force && row == a_assignment.worn && outfit == a_assignment.wornOutfit) {
			return std::nullopt;
		}
		// Read before Wear, which changes what they wear and so a random Default's kept pick.
		const auto fallback = row != Situation::kDefault ? OutfitFor(a_assignment, rows, Situation::kDefault, a_options) : outfit;
		auto       result = Wear(a_assignment, a_actor, row, outfit, a_options.leaveOut);
		if (result.worn.empty() && row != Situation::kDefault) {
			logger::info("No piece of the {} outfit went on {:08X}; the default outfit instead", Situations::Name(row), a_actor.GetFormID());
			result = Wear(a_assignment, a_actor, Situation::kDefault, fallback, a_options.leaveOut);
		}
		return result;
	}

	// Children and the dead are never dressed; a HUD message says why.
	bool CanBeDressed(RE::Actor& a_actor)
	{
		if (a_actor.IsChild()) {
			Notify("Livery: children cannot be dressed.");
			return false;
		}
		if (a_actor.IsDead()) {
			Notify(std::format("Livery: {} is dead.", NameOf(a_actor)));
			return false;
		}
		return true;
	}

	// Row `a_row` gets library outfit `a_outfit` (OutfitRows::Give); a library outfit made the
	// Default's `chosen` with random off drops the Default's pieces (a random Default keeps
	// them for an empty list). Nobody is dressed here.
	void GiveRow(Assignment& a_assignment, Situation a_row, std::uint32_t a_outfit)
	{
		auto& row = a_assignment.rows[RowOf(a_row)];
		OutfitRows::Give(row, a_outfit);
		if (a_row == Situation::kDefault && !row.random && row.chosen == a_outfit) {
			a_assignment.defaultPieces.clear();
		}
	}

	// The rows Change makes: the removed ones lose the outfit, the assigned ones get it.
	// Nobody is dressed here.
	// Contract: no row is in both lists.
	void ApplyChanges(Assignment& a_assignment, const Assignments::Changes& a_changes)
	{
		for (const auto row : a_changes.remove) {
			OutfitRows::Take(a_assignment.rows[RowOf(row)], a_changes.outfit);
		}
		for (const auto row : a_changes.assign) {
			GiveRow(a_assignment, row, a_changes.outfit);
		}
	}

	// The outfit the row they wear now gains first in `a_rows` while it is random, which their
	// next dressing wears as its pick (docs/situation-management-ui.md §1); 0 for none, or
	// while nothing is on them.
	std::uint32_t GainedPick(const Assignment& a_assignment, const OutfitRows::Rows& a_rows)
	{
		const auto& before = a_assignment.rows[RowOf(a_assignment.worn)].outfits;
		const auto& after = a_rows[RowOf(a_assignment.worn)];
		if (a_assignment.pieces.empty() || !after.random) {
			return 0;
		}
		const auto gained = std::ranges::find_if(after.outfits, [&before](std::uint32_t a_outfit) { return std::ranges::find(before, a_outfit) == before.end(); });
		return gained != after.outfits.end() ? *gained : 0;
	}

	// The library outfit's loaded pieces not in `a_armor`.
	std::vector<RE::TESObjectARMO*> PiecesNotIn(std::uint32_t a_outfit, const std::vector<RE::TESObjectARMO*>& a_armor)
	{
		const auto* outfit = Library::Find(a_outfit);
		auto        pieces = outfit ? Library::LoadedPieces(*outfit) : std::vector<RE::TESObjectARMO*>{};
		std::erase_if(pieces, [&a_armor](const RE::TESObjectARMO* a_piece) { return std::ranges::find(a_armor, a_piece) != a_armor.end(); });
		return pieces;
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
	// Record ASGN v4: assignment count; per assignment the actor FormID, the library outfit
	// worn (wornOutfit), the piece count, per piece its instance key (armor FormID, the
	// ExtraUniqueID's baseID and uniqueID), added and locked; the count of their own copies
	// taken off and their instance keys; the Default's piece count and their armor FormIDs;
	// the row worn (one byte); the row count (one byte), per row its situation (one byte, the
	// Situations::Situation value), random (one byte), chosen, the outfit count and the
	// outfit ids.
	// v3 has the default's outfit id where v4 has wornOutfit, its armor where v4 has the
	// Default's pieces, then one outfit id per situation in Situations::kChoosable order and
	// the situation worn. v2 ends after the copies taken off, v1 after the pieces and has no
	// outfit id: both get the pieces' armor as the default. FromLegacy turns them into rows.
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

	bool WriteRow(const SKSE::SerializationInterface& a_intfc, std::size_t a_situation, const OutfitRows::Row& a_row)
	{
		bool written = Write(a_intfc, static_cast<std::uint8_t>(a_situation)) && Write(a_intfc, static_cast<std::uint8_t>(a_row.random)) &&
		               Write(a_intfc, a_row.chosen) && Write(a_intfc, static_cast<std::uint32_t>(a_row.outfits.size()));
		for (const auto outfit : a_row.outfits) {
			written = written && Write(a_intfc, outfit);
		}
		return written;
	}

	bool WriteAssignment(const SKSE::SerializationInterface& a_intfc, const Assignment& a_assignment)
	{
		bool written = Write(a_intfc, a_assignment.actor) && Write(a_intfc, a_assignment.wornOutfit) &&
		               Write(a_intfc, static_cast<std::uint32_t>(a_assignment.pieces.size()));
		for (const auto& [key, added, locked] : a_assignment.pieces) {
			written = written && WriteKey(a_intfc, key) && Write(a_intfc, static_cast<std::uint8_t>(added)) && Write(a_intfc, static_cast<std::uint8_t>(locked));
		}
		written = written && Write(a_intfc, static_cast<std::uint32_t>(a_assignment.takenOff.size()));
		for (const auto& key : a_assignment.takenOff) {
			written = written && WriteKey(a_intfc, key);
		}
		written = written && Write(a_intfc, static_cast<std::uint32_t>(a_assignment.defaultPieces.size()));
		for (const auto armor : a_assignment.defaultPieces) {
			written = written && Write(a_intfc, armor);
		}
		written = written && Write(a_intfc, static_cast<std::uint8_t>(a_assignment.worn)) && Write(a_intfc, static_cast<std::uint8_t>(a_assignment.rows.size()));
		for (std::size_t i = 0; i < a_assignment.rows.size(); ++i) {
			written = written && WriteRow(a_intfc, i, a_assignment.rows[i]);
		}
		return written;
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

	// Reads an armor count and the FormIDs. A form whose plugin is gone is dropped. Returns
	// false when the record ends early.
	bool ReadArmorForms(const SKSE::SerializationInterface& a_intfc, std::vector<RE::FormID>& a_forms)
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
				a_forms.push_back(armor);
			}
		}
		return true;
	}

	// Reads a row worn (one byte). Returns false when the record ends early or names a row
	// this version does not have.
	bool ReadWornRow(const SKSE::SerializationInterface& a_intfc, Assignment& a_assignment)
	{
		std::uint8_t worn = 0;
		if (!Read(a_intfc, worn) || worn >= a_assignment.rows.size()) {
			return false;
		}
		a_assignment.worn = static_cast<Situation>(worn);
		return true;
	}

	// One row of a v4 record, after its situation.
	bool ReadRow(const SKSE::SerializationInterface& a_intfc, OutfitRows::Row& a_row)
	{
		std::uint8_t  random = 0;
		std::uint32_t outfits = 0;
		if (!Read(a_intfc, random) || !Read(a_intfc, a_row.chosen) || !Read(a_intfc, outfits)) {
			return false;
		}
		a_row.random = random != 0;
		for (; outfits > 0; --outfits) {
			std::uint32_t outfit = 0;
			if (!Read(a_intfc, outfit)) {
				return false;
			}
			a_row.outfits.push_back(outfit);
		}
		return true;
	}

	// Reads the Default's pieces, the row worn and the rows (v4). A row for a situation this
	// version does not have is dropped with a log line. Returns false when the record ends
	// early or names a worn row this version does not have.
	bool ReadRows(const SKSE::SerializationInterface& a_intfc, Assignment& a_assignment)
	{
		std::uint8_t rows = 0;
		if (!ReadArmorForms(a_intfc, a_assignment.defaultPieces) || !ReadWornRow(a_intfc, a_assignment) || !Read(a_intfc, rows)) {
			return false;
		}
		for (; rows > 0; --rows) {
			std::uint8_t    situation = 0;
			OutfitRows::Row row;
			if (!Read(a_intfc, situation) || !ReadRow(a_intfc, row)) {
				return false;
			}
			if (situation >= a_assignment.rows.size()) {
				logger::warn("Dropped the co-save row of situation {}, unknown to this version, for {:08X}", situation, a_assignment.actor);
				continue;
			}
			a_assignment.rows[situation] = std::move(row);
		}
		return true;
	}

	// What versions 1 to 3 kept of an actor's choices.
	struct LegacyChoices
	{
		std::uint32_t                                            defaultOutfit = 0;  // the library outfit the default armor came from; 0 for none
		std::vector<RE::FormID>                                  defaultArmor;       // empty for no default
		std::array<std::uint32_t, Situations::kChoosable.size()> situations{};       // outfit per situation (Situations::Index); 0 for none
	};

	// Reads the default armor, the situation outfits and the situation worn (v3). Returns
	// false when the record ends early or names a situation this version does not have.
	bool ReadLegacyChoices(const SKSE::SerializationInterface& a_intfc, LegacyChoices& a_choices, Assignment& a_assignment)
	{
		if (!ReadArmorForms(a_intfc, a_choices.defaultArmor)) {
			return false;
		}
		for (auto& outfit : a_choices.situations) {
			if (!Read(a_intfc, outfit)) {
				return false;
			}
		}
		return ReadWornRow(a_intfc, a_assignment);
	}

	// Rows from versions 1 to 3: a default with an outfit id still in the library becomes a
	// row of that outfit, chosen, and its armor is dropped; else the armor becomes the
	// Default's pieces. Each situation's outfit becomes a row of it, chosen. Random off.
	// Contract: the library is loaded (Library::Load at kDataLoaded, before any co-save load).
	void FromLegacy(Assignment& a_assignment, const LegacyChoices& a_choices)
	{
		auto& defaultRow = a_assignment.rows[RowOf(Situation::kDefault)];
		if (a_choices.defaultOutfit != 0 && !a_choices.defaultArmor.empty() && Library::Find(a_choices.defaultOutfit)) {
			defaultRow = { .outfits = { a_choices.defaultOutfit }, .chosen = a_choices.defaultOutfit };
		} else {
			a_assignment.defaultPieces = a_choices.defaultArmor;
		}
		for (const auto situation : Situations::kChoosable) {
			if (const auto outfit = a_choices.situations[Situations::Index(situation)]; outfit != 0) {
				a_assignment.rows[RowOf(situation)] = { .outfits = { outfit }, .chosen = outfit };
			}
		}
		a_assignment.wornOutfit = a_assignment.worn == Situation::kDefault ? defaultRow.chosen : a_choices.situations[Situations::Index(a_assignment.worn)];
	}

	// Reads one assignment of a `a_version` record. Returns false when the record ends early.
	bool ReadAssignment(const SKSE::SerializationInterface& a_intfc, std::uint32_t a_version, Assignment& a_assignment, bool& a_actorResolved)
	{
		LegacyChoices legacy;
		// v4's outfit worn, v2's and v3's default outfit; v1 has neither.
		auto& second = a_version == kAssignmentsVersion ? a_assignment.wornOutfit : legacy.defaultOutfit;
		if (!Read(a_intfc, a_assignment.actor) || (a_version != kFirstAssignmentsVersion && !Read(a_intfc, second))) {
			return false;
		}
		a_actorResolved = a_intfc.ResolveFormID(a_assignment.actor, a_assignment.actor);
		if (!ReadWorn(a_intfc, a_version, a_assignment)) {
			return false;
		}
		if (a_version == kAssignmentsVersion) {
			return ReadRows(a_intfc, a_assignment);
		}
		if (a_version == kThirdAssignmentsVersion) {
			if (!ReadLegacyChoices(a_intfc, legacy, a_assignment)) {
				return false;
			}
		} else {
			// Older records know only what went on: those pieces make the default.
			for (const auto& piece : a_assignment.pieces) {
				legacy.defaultArmor.push_back(piece.key.armor);
			}
		}
		FromLegacy(a_assignment, legacy);
		return true;
	}

	// The rows of situations removed in situations.json since this save was made, each with
	// a log line. Pieces worn for one stay on until the poll's next dressing
	// (ActorsWithSituations), as after ForgetSituation.
	// Contract: SituationStore is loaded (kDataLoaded, before any co-save load).
	void DropRemovedRows(Assignment& a_assignment)
	{
		for (const auto situation : Situations::kChoosable) {
			auto& row = a_assignment.rows[RowOf(situation)];
			if (SituationStore::IsRemoved(situation) && row != OutfitRows::Row{}) {
				logger::info("Dropped the {} row of {:08X}: the situation is removed in situations.json", Situations::Name(situation), a_assignment.actor);
				row = {};
			}
		}
	}

	// An assignment with nothing on the actor and no situation row holding an outfit, or
	// whose actor's plugin is gone, is dropped.
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
			DropRemovedRows(assignment);
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
			if (version != kAssignmentsVersion && version != kThirdAssignmentsVersion && version != kSecondAssignmentsVersion && version != kFirstAssignmentsVersion) {
				logger::warn("Co-save record version {} is unknown; no assignments loaded", version);
				continue;
			}
			g_assignments = ReadAssignments(*a_intfc, version);
			logger::info("Read co-save record version {}", version);
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

	// An actor not in memory (a non-persistent NPC whose cell is not loaded) cannot be
	// reached: their locks and copies stay, which never breaks a save.
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

	void Assign(RE::Actor& a_actor, const std::vector<RE::TESObjectARMO*>& a_armor, std::uint32_t a_outfit)
	{
		if (!CanBeDressed(a_actor) || a_armor.empty()) {
			return;
		}

		const auto     id = a_actor.GetFormID();
		auto&          assignment = FindOrAddAssignment(id);
		RedressOptions options;
		if (a_outfit != 0) {
			GiveRow(assignment, Situation::kDefault, a_outfit);
			options = { .leaveOut = PiecesNotIn(a_outfit, a_armor), .given = a_outfit, .givenRows = { Situation::kDefault } };
		} else {
			assignment.defaultPieces = FormIDsOf(a_armor);
			auto& row = assignment.rows[RowOf(Situation::kDefault)];
			row.chosen = 0;
			row.random = false;
		}
		// The new Default goes on even when they already wore the Default; a situation
		// outfit they wear stays on and the Default waits for its turn.
		const auto now = Situations::Evaluate(a_actor);
		options.force = OutfitRows::Resolve(LiveRows(assignment), now) == Situation::kDefault;
		const auto result = Redress(assignment, a_actor, now, options);
		const auto wornFor = assignment.worn;  // read before DropIfEmpty, which may end the record
		DropIfEmpty(id);
		SyncManaged();
		if (!result) {
			Notify(std::format("Livery: {} will wear these by default; dressed now{}.", NameOf(a_actor), SituationStore::Suffix(wornFor)));
			return;
		}

		const auto added = std::ranges::count_if(result->worn, &Dressing::Piece::added);
		auto       text = a_actor.IsPlayerRef() ? std::string("Livery: you now wear these") : std::format("Livery: {} now wears these", NameOf(a_actor));
		text += SituationStore::Suffix(wornFor);
		if (added > 0) {
			text += std::format(", {} added", added);
		}
		Notify(text + DressingNotes(*result) + ".");
	}

	void Change(RE::Actor& a_actor, const Changes& a_changes)
	{
		if (!CanBeDressed(a_actor)) {
			return;
		}
		const auto id = a_actor.GetFormID();
		auto&      assignment = FindOrAddAssignment(id);
		ApplyChanges(assignment, a_changes);
		// A removed row that still holds an outfit is a random one with others on its list.
		std::vector<Situation> emptied;
		std::vector<Situation> kept;
		for (const auto row : a_changes.remove) {
			(OutfitRows::HoldsAny(assignment.rows[RowOf(row)]) ? kept : emptied).push_back(row);
		}
		const auto result = Redress(assignment, a_actor, Situations::Evaluate(a_actor),
			{ .force = true, .leaveOut = a_changes.leaveOut, .given = a_changes.outfit, .givenRows = a_changes.assign });
		const auto worn = assignment.worn;  // read before DropIfEmpty, which may end the record
		const auto pieces = assignment.pieces.size();
		DropIfEmpty(id);
		SyncManaged();

		std::vector<std::string> parts;
		if (!a_changes.assign.empty()) {
			parts.push_back(std::format("{} for {}", QuotedOutfitName(a_changes.outfit), JoinSituations(a_changes.assign)));
		}
		if (!emptied.empty()) {
			parts.push_back(std::format("no outfit for {} any more", JoinSituations(emptied)));
		}
		if (!kept.empty()) {
			parts.push_back(std::format("{} no longer for {}", QuotedOutfitName(a_changes.outfit), JoinSituations(kept)));
		}
		auto text = std::format("Livery: {}:", NameOf(a_actor));
		for (std::size_t i = 0; i < parts.size(); ++i) {
			text += (i == 0 ? " " : "; ") + parts[i];
		}
		text += pieces > 0 ? std::format(". Dressed now{}", SituationStore::Suffix(worn)) : ". Own clothes now";
		Notify(text + (result ? DressingNotes(*result) : "") + ".");
	}

	std::optional<std::uint32_t> SetRows(RE::Actor& a_actor, const OutfitRows::Rows& a_rows, bool a_dropDefaultPieces)
	{
		if (!CanBeDressed(a_actor)) {
			return std::nullopt;
		}
		const auto id = a_actor.GetFormID();
		auto&      assignment = FindOrAddAssignment(id);
		const auto pick = GainedPick(assignment, a_rows);
		assignment.rows = a_rows;
		for (const auto situation : Situations::kChoosable) {
			if (SituationStore::IsRemoved(situation)) {
				assignment.rows[RowOf(situation)] = {};
			}
		}
		if (a_dropDefaultPieces) {
			assignment.defaultPieces.clear();
		}
		// Contract: no DropIfEmpty here. The record of someone Livery has nothing on yet would
		// end before the Refresh that dresses them; that dressing ends it if nothing goes on.
		SyncManaged();
		return pick;
	}

	void Refresh(RE::Actor& a_actor, const std::vector<RE::TESObjectARMO*>& a_leaveOut, std::uint32_t a_pick)
	{
		const auto id = a_actor.GetFormID();
		auto*      assignment = FindAssignment(id);
		if (!assignment) {
			return;
		}
		if (a_actor.IsDead()) {
			Notify(std::format("Livery: {} is dead.", NameOf(a_actor)));
			return;
		}
		const auto result = Redress(*assignment, a_actor, Situations::Evaluate(a_actor),
			{ .force = true, .leaveOut = a_leaveOut, .given = a_pick, .givenRows = { assignment->worn } });
		const auto worn = assignment->worn;  // read before DropIfEmpty, which may end the record
		const auto outfit = assignment->wornOutfit;
		const auto pieces = assignment->pieces.size();
		DropIfEmpty(id);
		SyncManaged();
		auto text = pieces > 0 ? std::format("Livery: {} now wears {}{}", NameOf(a_actor), outfit ? QuotedOutfitName(outfit) : "the default pieces",
									 SituationStore::Suffix(worn)) :
		                         std::format("Livery: {}: own clothes now", NameOf(a_actor));
		Notify(text + (result ? DressingNotes(*result) : "") + ".");
	}

	bool WearFor(RE::Actor& a_actor, const Situations::Applicable& a_now)
	{
		const auto id = a_actor.GetFormID();
		auto*      assignment = FindAssignment(id);
		if (!assignment) {
			return false;
		}
		if (!Redress(*assignment, a_actor, a_now)) {
			return false;
		}
		logger::info("Dressed {:08X} for {}: {} piece(s) on", id, Situations::Name(assignment->worn), assignment->pieces.size());
		// Read before DropIfEmpty: a situation outfit gone from their rows (ForgetOutfit) may
		// leave nothing, which ends the record.
		const auto text = SituationChangeText(a_actor, *assignment);
		DropIfEmpty(id);
		SyncManaged();
		if (Settings::NotifySituationChanges()) {
			Notify(text);
		}
		return true;
	}

	std::vector<RE::FormID> ActorsWithSituations()
	{
		std::vector<RE::FormID> actors;
		for (const auto& assignment : g_assignments) {
			if (HasSituations(assignment) || assignment.worn != Situation::kDefault) {
				actors.push_back(assignment.actor);
			}
		}
		return actors;
	}

	std::vector<Use> UsesOf(std::uint32_t a_outfit)
	{
		std::vector<Use> uses;
		if (a_outfit == 0) {
			return uses;
		}
		for (const auto& assignment : g_assignments) {
			Use use{ .actor = assignment.actor };
			for (std::size_t row = 0; row < assignment.rows.size(); ++row) {
				if (OutfitRows::Holds(assignment.rows[row], a_outfit)) {
					use.rows.push_back(static_cast<Situation>(row));
				}
			}
			if (!use.rows.empty()) {
				uses.push_back(std::move(use));
			}
		}
		return uses;
	}

	std::vector<Holder> HoldersOf(Situations::Situation a_situation)
	{
		std::vector<Holder> holders;
		for (const auto& assignment : g_assignments) {
			const auto& outfits = assignment.rows[RowOf(a_situation)].outfits;
			if (const auto count = static_cast<std::size_t>(std::ranges::count_if(outfits, InLibrary)); count > 0) {
				holders.push_back({ .actor = assignment.actor, .outfits = count });
			}
		}
		return holders;
	}

	// The pieces worn for a cleared situation row stay on until the next dressing (WearFor),
	// which ActorsWithSituations keeps the poll watching for. A Default worn from the outfit
	// is no longer worn from a library outfit (wornOutfit 0).
	std::vector<RE::FormID> ForgetOutfit(std::uint32_t a_outfit, const std::vector<RE::TESObjectARMO*>& a_pieces)
	{
		std::vector<RE::FormID> wearing;
		if (a_outfit == 0) {
			return wearing;
		}
		for (auto& assignment : g_assignments) {
			if (assignment.worn != Situation::kDefault && assignment.wornOutfit == a_outfit && !assignment.pieces.empty()) {
				wearing.push_back(assignment.actor);
			}
			const auto& defaultRow = assignment.rows[RowOf(Situation::kDefault)];
			const bool  heldByDefault = OutfitRows::Holds(defaultRow, a_outfit);
			for (auto& row : assignment.rows) {
				OutfitRows::Take(row, a_outfit);
			}
			if (heldByDefault && !OutfitRows::HoldsAny(defaultRow) && assignment.defaultPieces.empty()) {
				assignment.defaultPieces = FormIDsOf(a_pieces);
			}
			if (assignment.worn == Situation::kDefault && assignment.wornOutfit == a_outfit) {
				assignment.wornOutfit = 0;
			}
		}
		std::erase_if(g_assignments, [](const Assignment& a_assignment) { return a_assignment.pieces.empty() && !HasSituations(a_assignment); });
		SyncManaged();
		return wearing;
	}

	// As ForgetOutfit: the pieces worn for the cleared row stay on until the next dressing.
	std::vector<RE::FormID> ForgetSituation(Situations::Situation a_situation)
	{
		std::vector<RE::FormID> wearing;
		if (a_situation == Situation::kDefault) {
			return wearing;
		}
		for (auto& assignment : g_assignments) {
			if (assignment.worn == a_situation && !assignment.pieces.empty()) {
				wearing.push_back(assignment.actor);
			}
			assignment.rows[RowOf(a_situation)] = {};
		}
		std::erase_if(g_assignments, [](const Assignment& a_assignment) { return a_assignment.pieces.empty() && !HasSituations(a_assignment); });
		SyncManaged();
		return wearing;
	}

	void Unassign(RE::Actor& a_actor)
	{
		if (!FindAssignment(a_actor.GetFormID())) {
			return;
		}
		const auto putBack = ReleaseActor(a_actor, true, true);
		logger::info("Put {} of their own piece(s) back on {:08X}", putBack, a_actor.GetFormID());
		Notify(a_actor.IsPlayerRef() ? std::string("Livery: restored your original outfit.") : std::format("Livery: restored {}'s original outfit.", NameOf(a_actor)));
	}

	std::optional<Choices> ChoicesOf(RE::FormID a_actor)
	{
		const auto* assignment = FindAssignment(a_actor);
		if (!assignment) {
			return std::nullopt;
		}
		std::vector<RE::FormID> wornArmor;
		for (const auto& piece : assignment->pieces) {
			wornArmor.push_back(piece.key.armor);
		}
		return Choices{
			.dressed = !assignment->pieces.empty(),
			.wornArmor = std::move(wornArmor),
			.rows = assignment->rows,
			.defaultPieces = !assignment->defaultPieces.empty(),
			.worn = assignment->worn,
			.wornOutfit = assignment->wornOutfit,
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
