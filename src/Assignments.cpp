// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants   - the player's reference, co-save ids
//   Records     - one assignment per actor, with its rows (OutfitRows); g_managed mirrors the
//                 NPCs for the event sinks, g_dressed those with pieces on for OwnOutfitGuard
//   Dressing    - which row a situation resolves to and the outfit it wears (a random row's
//                 pick), and putting it on in place of the one worn
//   Release     - Livery's locks and copies off, the actor's own pieces back on
//   Load, death - pieces put back on, and armor worn over them taken off, when an assigned NPC
//                 loads, or their own clothes back on when every piece is gone; outfit copies
//                 that came since locked; locks released when one dies; armor put on one in a
//                 menu, or by the engine's worn-gear pass, taken off at once; pieces taken from
//                 one in a menu replaced as it closes
//   Co-save     - save, load (version 6; the rows of situations removed in situations.json
//                 or unknown to this version dropped),
//                 the items the rows' outfits leave off in a record of their own, revert
//   Event sinks - TESObjectLoadedEvent, TESDeathEvent, TESEquipEvent (while a menu lists an
//                 inventory), MenuOpenCloseEvent
//   API         - RegisterCoSave / RegisterSinks / ReleaseEveryone / Assign / Change / SetRows / Refresh / PickAnother / WearFor /
//                 ActorsWithSituations / UsesOf / HoldersOf / ForgetOutfit / ForgetSituation / Unassign / ChoicesOf /
//                 SuspendLocks / ResumeLocks / MarkAddedCopies / UnmarkAddedCopies / NoteShownInMenu / IsDressedNpc /
//                 TakeOffAfterPass
#include "PCH.h"

#include "Assignments.h"

#include "Dressing.h"
#include "Library.h"
#include "Settings.h"
#include "SituationStore.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <iterator>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
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
	constexpr std::uint32_t kAssignmentsVersion = 6;  // the only version read
	constexpr std::uint32_t kExclusionsRecord = 'EXCL';
	constexpr std::uint32_t kExclusionsVersion = 2;  // the only version read

	// --- Records --------------------------------------------------------------

	// An actor Livery dresses: the pieces it has on them, their own pieces it took off, and
	// their rows, the Default and the situations. NPCs wear the pieces locked, and their
	// outfit copies carry a lock while they wear pieces (Dressing::LockOutfitCopies); the
	// player does not, since a locked piece cannot be taken off in the inventory menu.
	struct Assignment
	{
		RE::FormID                         actor = 0;
		std::vector<Dressing::Piece>       pieces;        // on them now, for `worn`
		std::vector<Dressing::InstanceKey> takenOff;      // their own copies, put back on at release
		std::vector<Dressing::InstanceKey> lockedCopies;  // their outfit copies Livery locked unworn, unlocked at release
		OutfitRows::Rows                   rows;          // the Default, then the situations
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

	// Contract: the assigned NPCs, for the event sinks, and those Livery has pieces on, for
	// OwnOutfitGuard. The thread that sends TESObjectLoadedEvent and TESDeathEvent is not
	// documented, nor is the worn-gear pass's, so the sinks and the hook read only these, under
	// their lock, and the sinks queue the work as SKSE tasks.
	std::mutex              g_managedLock;
	std::vector<RE::FormID> g_managed;
	std::vector<RE::FormID> g_dressed;

	// Contract: game thread only. The NPC whose locks are off for the preview, or 0.
	RE::FormID g_suspended = 0;

	// The assigned NPC whose inventory the open trade, container or pickpocket menu lists, or
	// 0. Contract: written on the game thread (NoteShownInMenu), taken by MenuSink, whose
	// thread is not documented, hence atomic.
	std::atomic<RE::FormID> g_shownInMenu{ 0 };

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

	// Contract: game thread, after every change of g_assignments, each dressing (Redress)
	// included, since it can leave an assignment with pieces or with none. The player is left
	// out: nothing is put back on them when they load.
	void SyncManaged()
	{
		std::lock_guard lock{ g_managedLock };
		g_managed.clear();
		g_dressed.clear();
		for (const auto& assignment : g_assignments) {
			if (assignment.actor == kPlayerRef) {
				continue;
			}
			g_managed.push_back(assignment.actor);
			if (!assignment.pieces.empty()) {
				g_dressed.push_back(assignment.actor);
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
	// now, ending in the situation it is for.
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

	// The armor row `a_row` wears as `a_outfit`: the library outfit's loaded pieces less those
	// it leaves off on the row (OutfitRows::WornPieces), or with none the Default's pieces.
	// Empty for an outfit gone from the library or nothing to wear.
	std::vector<RE::TESObjectARMO*> ArmorFor(const Assignment& a_assignment, Situation a_row, std::uint32_t a_outfit)
	{
		if (a_outfit != 0) {
			const auto* outfit = Library::Find(a_outfit);
			if (!outfit) {
				return {};
			}
			const auto excluded = OutfitRows::ExcludedOf(a_assignment.rows[RowOf(a_row)], a_outfit);
			return OutfitRows::WornPieces(Library::LoadedPieces(*outfit), excluded, [](const RE::TESObjectARMO* a_piece) { return a_piece->GetFormID(); });
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
	// clothes. An NPC's outfit copies are locked again once pieces are on. A random row's
	// pick is recorded as its `chosen`, which turning random off keeps on
	// (OutfitRows::SetRandom).
	Dressing::Result Wear(Assignment& a_assignment, RE::Actor& a_actor, Situation a_row, std::uint32_t a_outfit, const std::vector<RE::TESObjectARMO*>& a_leaveOut)
	{
		Dressing::Release(a_actor, a_assignment.pieces, true);
		// Before PutBack and Dress, which skip a locked unworn copy: their outfit items can be
		// among the copies taken off, or a piece of the new outfit.
		Dressing::UnlockOutfitCopies(a_actor, a_assignment.lockedCopies);
		a_assignment.lockedCopies.clear();
		Dressing::PutBack(a_actor, a_assignment.takenOff);
		auto armor = ArmorFor(a_assignment, a_row, a_outfit);
		std::erase_if(armor, [&a_leaveOut](const RE::TESObjectARMO* a_piece) { return std::ranges::find(a_leaveOut, a_piece) != a_leaveOut.end(); });
		const bool isPlayer = a_actor.IsPlayerRef();
		auto       result = Dressing::Dress(a_actor, armor, !isPlayer, !isPlayer || Settings::PlayerAddsMissingPieces());
		a_assignment.pieces = result.worn;
		a_assignment.takenOff = result.takenOff;
		a_assignment.worn = a_row;
		a_assignment.wornOutfit = a_outfit;
		if (!isPlayer && !result.worn.empty()) {
			Dressing::LockOutfitCopies(a_actor, a_assignment.pieces, a_assignment.lockedCopies);
		}
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

	// Row `a_row` gets library outfit `a_outfit`, leaving `a_excluded` off (OutfitRows::Give);
	// a library outfit made the Default's `chosen` with random off drops the Default's pieces
	// (a random Default keeps them for an empty list). Nobody is dressed here.
	void GiveRow(Assignment& a_assignment, Situation a_row, std::uint32_t a_outfit, const std::vector<RE::FormID>& a_excluded = {})
	{
		auto& row = a_assignment.rows[RowOf(a_row)];
		OutfitRows::Give(row, a_outfit, a_excluded);
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
			GiveRow(a_assignment, row, a_changes.outfit, a_changes.excluded);
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
		Dressing::UnlockOutfitCopies(actor, assignment.lockedCopies);  // before PutBack, which skips a locked copy
		return putBack ? Dressing::PutBack(actor, assignment.takenOff) : 0;
	}

	// --- Load, death ----------------------------------------------------------
	//
	// Contract: game thread (SKSE tasks from the event sinks), all.

	// Locks the outfit copies of an NPC Livery has pieces on that came since the last lock
	// (another mod's outfit change, the copies the take-off just took off), with a log line.
	void LockNewOutfitCopies(Assignment& a_assignment, RE::Actor& a_actor)
	{
		if (a_assignment.pieces.empty() || a_actor.IsPlayerRef()) {
			return;
		}
		if (const auto count = Dressing::LockOutfitCopies(a_actor, a_assignment.pieces, a_assignment.lockedCopies); count > 0) {
			logger::info("Locked {} of their outfit item(s) on {:08X}", count, a_actor.GetFormID());
		}
	}

	// None of the pieces Livery put on them is in their inventory any more (an outfit change,
	// an inventory reset, a script that took their items): the guard would keep their outfit
	// items off and leave them naked, so their own clothes go back on, as Restore leaves them.
	// The record keeps their rows: a situation row dresses them again at their next change of
	// situation; with none it ends (DropIfEmpty), and the next Apply dresses them.
	// Contract: `a_assignment` is stale after this.
	void ReturnToOwnClothes(Assignment& a_assignment, RE::Actor& a_actor)
	{
		const auto id = a_actor.GetFormID();
		Dressing::UnlockOutfitCopies(a_actor, a_assignment.lockedCopies);  // before PutBack, which skips a locked copy
		const auto putBack = Dressing::PutBack(a_actor, a_assignment.takenOff);
		const auto outfitItems = Dressing::PutOnOutfitItems(a_actor);
		a_assignment.pieces.clear();
		a_assignment.takenOff.clear();
		a_assignment.lockedCopies.clear();
		logger::info("None of Livery's pieces on {:08X} is in their inventory any more; {} of their own piece(s) and {} outfit item(s) went back on", id,
			putBack, outfitItems);
		DropIfEmpty(id);
		SyncManaged();
	}

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
		if (!assignment->pieces.empty() && !Dressing::CarriesAny(*actor, assignment->pieces)) {
			ReturnToOwnClothes(*assignment, *actor);
			return;
		}
		if (const auto count = Dressing::PutBackOn(*actor, assignment->pieces); count > 0) {
			logger::info("Put {} piece(s) back on or locked again on {:08X}", count, a_actor);
		}
		// After PutBackOn, so the pieces it put on again count as worn.
		if (const auto count = Dressing::TakeOffExtras(*actor, assignment->pieces, assignment->lockedCopies); count > 0) {
			logger::info("Took off {} item(s) worn outside Livery's pieces on {:08X}", count, a_actor);
		}
		LockNewOutfitCopies(*assignment, *actor);
	}

	// The pieces the player took from them in the menu that just closed go on again, as
	// copies when they carry none, so the outfit does not change by itself; then a better
	// copy the player gave them goes on in place of a worn piece, and armor the engine put on
	// them outside the pieces (a helmet given for a slot the outfit leaves empty) comes off.
	void ReplenishTaken(RE::FormID a_actor)
	{
		if (a_actor == g_suspended) {
			return;  // the preview has them off on purpose
		}
		auto* assignment = FindAssignment(a_actor);
		auto* actor = assignment ? RE::TESForm::LookupByID<RE::Actor>(a_actor) : nullptr;
		if (!actor || actor->IsDead()) {
			return;
		}
		if (const auto count = Dressing::Replenish(*actor, assignment->pieces); count > 0) {
			logger::info("Replenished {} piece(s) on {:08X} taken in a menu", count, a_actor);
		}
		if (const auto count = Dressing::Upgrade(*actor, assignment->pieces); count > 0) {
			logger::info("Upgraded {} piece(s) on {:08X} to a better copy", count, a_actor);
		}
		// After Upgrade, so a better copy it put on counts as a piece.
		if (const auto count = Dressing::TakeOffExtras(*actor, assignment->pieces, assignment->lockedCopies); count > 0) {
			logger::info("Took off {} item(s) worn outside Livery's pieces on {:08X}", count, a_actor);
		}
		LockNewOutfitCopies(*assignment, *actor);
	}

	// Whether the game skips the actor update that shows an NPC's equip change: a menu pauses
	// it (the trade menu), or the capture window froze it (Live Preview off).
	bool GameHalted()
	{
		auto* ui = RE::UI::GetSingleton();
		auto* main = RE::Main::GetSingleton();
		return (ui && ui->GameIsPaused()) || (main && main->GetRuntimeData().freezeTime);
	}

	// Armor the engine put on them that Livery keeps off comes off at once: given while the
	// trade, container or pickpocket menu is open (a helmet for a slot the outfit leaves empty),
	// not as it closes, or put on by the worn-gear pass's best-armor step (their outfit items or
	// a hat after a dressing took them off). `a_when` ends the log line. Returns whether it took
	// any off.
	bool TakeOffGiven(RE::FormID a_actor, std::string_view a_when)
	{
		if (a_actor == g_suspended) {
			return false;  // the preview has them off on purpose
		}
		auto* assignment = FindAssignment(a_actor);
		auto* actor = assignment ? RE::TESForm::LookupByID<RE::Actor>(a_actor) : nullptr;
		if (!actor || actor->IsDead()) {
			return false;
		}
		const auto count = Dressing::TakeOffExtras(*actor, assignment->pieces, assignment->lockedCopies);
		if (count == 0) {
			return false;  // nothing changed: no lock pass either, this runs after every worn-gear pass
		}
		logger::info("Took off {} item(s) worn outside Livery's pieces on {:08X} {}", count, a_actor, a_when);
		LockNewOutfitCopies(*assignment, *actor);
		// Only a halted game needs the rebuild: it costs, and its NiNodeUpdate event makes
		// body-morph mods refit, for a change a running game's actor update shows anyway.
		if (GameHalted()) {
			Dressing::RefreshModel(*actor);
		}
		return true;
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
	// Record ASGN v6: assignment count; per assignment the actor FormID, the library outfit
	// worn (wornOutfit), the piece count, per piece its instance key (armor FormID, the
	// ExtraUniqueID's baseID and uniqueID), added and locked; the count of their own copies
	// taken off and their instance keys; the count of their outfit copies Livery locked and
	// their instance keys; the Default's piece count and their armor FormIDs;
	// the situation of the row worn; the row count (one byte), per row its situation, random
	// (one byte), chosen, the outfit count and the outfit ids. A situation is stored by its key
	// (Situations::Name): its length (one byte) and its characters, so situations can be
	// added and removed without a new version. A record of another version is not read.
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

	// A key count and the keys.
	bool WriteKeys(const SKSE::SerializationInterface& a_intfc, const std::vector<Dressing::InstanceKey>& a_keys)
	{
		bool written = Write(a_intfc, static_cast<std::uint32_t>(a_keys.size()));
		for (const auto& key : a_keys) {
			written = written && WriteKey(a_intfc, key);
		}
		return written;
	}

	// Reads what WriteKeys wrote. A key whose plugin is gone is dropped. Returns false when the
	// record ends early.
	bool ReadKeys(const SKSE::SerializationInterface& a_intfc, std::vector<Dressing::InstanceKey>& a_keys)
	{
		std::uint32_t count = 0;
		if (!Read(a_intfc, count)) {
			return false;
		}
		for (; count > 0; --count) {
			Dressing::InstanceKey key;
			bool                  resolved = false;
			if (!ReadKey(a_intfc, key, resolved)) {
				return false;
			}
			if (resolved) {
				a_keys.push_back(key);
			}
		}
		return true;
	}

	bool WriteSituation(const SKSE::SerializationInterface& a_intfc, Situation a_situation)
	{
		const std::string_view key = Situations::Name(a_situation);
		return Write(a_intfc, static_cast<std::uint8_t>(key.size())) && a_intfc.WriteRecordData(key.data(), static_cast<std::uint32_t>(key.size()));
	}

	bool WriteRow(const SKSE::SerializationInterface& a_intfc, Situation a_situation, const OutfitRows::Row& a_row)
	{
		bool written = WriteSituation(a_intfc, a_situation) && Write(a_intfc, static_cast<std::uint8_t>(a_row.random)) &&
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
		written = written && WriteKeys(a_intfc, a_assignment.takenOff) && WriteKeys(a_intfc, a_assignment.lockedCopies);
		written = written && Write(a_intfc, static_cast<std::uint32_t>(a_assignment.defaultPieces.size()));
		for (const auto armor : a_assignment.defaultPieces) {
			written = written && Write(a_intfc, armor);
		}
		written = written && WriteSituation(a_intfc, a_assignment.worn) && Write(a_intfc, static_cast<std::uint8_t>(a_assignment.rows.size()));
		for (std::size_t i = 0; i < a_assignment.rows.size(); ++i) {
			written = written && WriteRow(a_intfc, static_cast<Situation>(i), a_assignment.rows[i]);
		}
		return written;
	}

	// Record EXCL v2, written after ASGN while a row's outfit leaves items off
	// (OutfitRows::Exclusion): entry count; per entry the actor FormID, the row's situation (by
	// its key, as in ASGN), the outfit id, the armor count and the armor FormIDs. A record
	// of its own, so a Livery that does not know EXCL skips it and dresses every item.

	std::uint32_t CountExclusions()
	{
		std::uint32_t count = 0;
		for (const auto& assignment : g_assignments) {
			for (const auto& row : assignment.rows) {
				count += static_cast<std::uint32_t>(row.excluded.size());
			}
		}
		return count;
	}

	void SaveExclusions(SKSE::SerializationInterface* a_intfc)
	{
		const auto count = CountExclusions();
		if (count == 0) {
			return;
		}
		if (!a_intfc->OpenRecord(kExclusionsRecord, kExclusionsVersion)) {
			logger::error("Could not open the co-save record of the items left off");
			return;
		}
		bool written = Write(*a_intfc, count);
		for (const auto& assignment : g_assignments) {
			for (std::size_t i = 0; i < assignment.rows.size(); ++i) {
				for (const auto& [outfit, armor] : assignment.rows[i].excluded) {
					written = written && Write(*a_intfc, assignment.actor) && WriteSituation(*a_intfc, static_cast<Situation>(i)) && Write(*a_intfc, outfit) &&
					          Write(*a_intfc, static_cast<std::uint32_t>(armor.size()));
					for (const auto id : armor) {
						written = written && Write(*a_intfc, id);
					}
				}
			}
		}
		if (!written) {
			logger::error("Could not write the co-save record of the items left off");
		}
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
		SaveExclusions(a_intfc);
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

	// Reads the pieces, the copies taken off and the outfit copies locked. Returns false when
	// the record ends early. A piece or copy whose plugin is gone is dropped.
	bool ReadWorn(const SKSE::SerializationInterface& a_intfc, Assignment& a_assignment)
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
		return ReadKeys(a_intfc, a_assignment.takenOff) && ReadKeys(a_intfc, a_assignment.lockedCopies);
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

	// Reads a situation's key as written (WriteSituation). Returns false when the record ends
	// early.
	bool ReadSituationKey(const SKSE::SerializationInterface& a_intfc, std::string& a_key)
	{
		std::uint8_t length = 0;
		if (!Read(a_intfc, length)) {
			return false;
		}
		a_key.resize(length);
		return length == 0 || a_intfc.ReadRecordData(a_key.data(), length) == length;
	}

	// Reads the situation of the row worn. One this version does not have (Situations::FromKey)
	// reads as the Default, with a log line. Returns false when the record ends early.
	bool ReadWornRow(const SKSE::SerializationInterface& a_intfc, Assignment& a_assignment)
	{
		std::string key;
		if (!ReadSituationKey(a_intfc, key)) {
			return false;
		}
		const auto situation = Situations::FromKey(key);
		if (!situation) {
			logger::warn("{:08X} wore the co-save row of situation {}, which this version does not have; read as the Default", a_assignment.actor, key);
		}
		a_assignment.worn = situation.value_or(Situation::kDefault);
		return true;
	}

	// One row of the record, after its situation.
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

	// Reads the Default's pieces, the row worn and the rows. A row for a situation this version
	// does not have is dropped with a log line. Returns false when the record ends early.
	bool ReadRows(const SKSE::SerializationInterface& a_intfc, Assignment& a_assignment)
	{
		std::uint8_t rows = 0;
		if (!ReadArmorForms(a_intfc, a_assignment.defaultPieces) || !ReadWornRow(a_intfc, a_assignment) || !Read(a_intfc, rows)) {
			return false;
		}
		for (; rows > 0; --rows) {
			std::string     key;
			OutfitRows::Row row;
			if (!ReadSituationKey(a_intfc, key) || !ReadRow(a_intfc, row)) {
				return false;
			}
			const auto situation = Situations::FromKey(key);
			if (!situation) {
				if (row != OutfitRows::Row{}) {
					logger::warn("Dropped the co-save row of situation {}, which this version does not have, for {:08X}", key, a_assignment.actor);
				}
				continue;
			}
			a_assignment.rows[RowOf(*situation)] = std::move(row);
		}
		return true;
	}

	// Reads one assignment. Returns false when the record ends early.
	bool ReadAssignment(const SKSE::SerializationInterface& a_intfc, Assignment& a_assignment, bool& a_actorResolved)
	{
		if (!Read(a_intfc, a_assignment.actor) || !Read(a_intfc, a_assignment.wornOutfit)) {
			return false;
		}
		a_actorResolved = a_intfc.ResolveFormID(a_assignment.actor, a_assignment.actor);
		return ReadWorn(a_intfc, a_assignment) && ReadRows(a_intfc, a_assignment);
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
	std::vector<Assignment> ReadAssignments(const SKSE::SerializationInterface& a_intfc)
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
			if (!ReadAssignment(a_intfc, assignment, actorResolved)) {
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

	// An EXCL entry as read, its FormIDs fixed for the current load order.
	struct StoredExclusion
	{
		RE::FormID               actor = 0;
		std::optional<Situation> row;  // nullopt for a situation this version does not have
		std::uint32_t            outfit = 0;
		std::vector<RE::FormID>  armor;
	};

	// Reads the EXCL record. An entry whose actor's plugin is gone is dropped, and so is an
	// armor form whose plugin is gone (ReadArmorForms).
	std::vector<StoredExclusion> ReadExclusions(const SKSE::SerializationInterface& a_intfc)
	{
		std::vector<StoredExclusion> entries;
		std::uint32_t                count = 0;
		if (!Read(a_intfc, count)) {
			logger::error("The co-save record of the items left off is cut short");
			return entries;
		}
		for (; count > 0; --count) {
			StoredExclusion entry;
			std::string     key;
			if (!Read(a_intfc, entry.actor) || !ReadSituationKey(a_intfc, key) || !Read(a_intfc, entry.outfit) || !ReadArmorForms(a_intfc, entry.armor)) {
				logger::error("The co-save record of the items left off is cut short");
				return entries;
			}
			entry.row = Situations::FromKey(key);
			if (a_intfc.ResolveFormID(entry.actor, entry.actor)) {
				entries.push_back(std::move(entry));
			}
		}
		return entries;
	}

	// Gives the assignments read the items their rows' outfits leave off. An entry for an actor
	// without an assignment, a row this version does not have, or an outfit no longer on the
	// row (its situation removed in situations.json, DropRemovedRows) is dropped.
	// Contract: after the ASGN record was read, in whichever order the records came.
	void AttachExclusions(const std::vector<StoredExclusion>& a_entries)
	{
		for (const auto& entry : a_entries) {
			auto* assignment = FindAssignment(entry.actor);
			if (!assignment || !entry.row) {
				continue;
			}
			auto& row = assignment->rows[RowOf(*entry.row)];
			if (std::ranges::find(row.outfits, entry.outfit) != row.outfits.end()) {
				OutfitRows::SetExcluded(row, entry.outfit, entry.armor);
			}
		}
	}

	void OnLoad(SKSE::SerializationInterface* a_intfc)
	{
		std::uint32_t                type = 0;
		std::uint32_t                version = 0;
		std::uint32_t                length = 0;
		std::vector<StoredExclusion> exclusions;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type == kExclusionsRecord) {
				if (version != kExclusionsVersion) {
					logger::warn("Co-save record EXCL version {} is not {}; every outfit wears all its items", version, kExclusionsVersion);
					continue;
				}
				exclusions = ReadExclusions(*a_intfc);
				continue;
			}
			if (type != kAssignmentsRecord) {
				continue;
			}
			if (version != kAssignmentsVersion) {
				logger::warn("Co-save record version {} is not {}; no assignments loaded", version, kAssignmentsVersion);
				continue;
			}
			g_assignments = ReadAssignments(*a_intfc);
			logger::info("Read co-save record version {}", version);
		}
		AttachExclusions(exclusions);
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

	// Registered only while the trade, container or pickpocket menu is open (MenuSink), so
	// normal play sends it nothing. Equips by anyone but an assigned NPC return after the check.
	class EquipSink final : public RE::BSTEventSink<RE::TESEquipEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
		{
			if (!a_event || !a_event->equipped || !a_event->actor || !IsManaged(a_event->actor->GetFormID())) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const auto* item = RE::TESForm::LookupByID(a_event->baseObject);
			if (item && item->IsArmor()) {
				const auto id = a_event->actor->GetFormID();
				SKSE::GetTaskInterface()->AddTask([id] { TakeOffGiven(id, "in a menu"); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		static EquipSink& Get()
		{
			static EquipSink sink;
			return sink;
		}
	};

	// Every other menu returns after the name compares. The note is not cleared as a menu
	// opens: whether its list fills before or after that event is not documented. Only these
	// two menus list an NPC's inventory, so the note is theirs.
	class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (!a_event || (a_event->menuName != RE::ContainerMenu::MENU_NAME && a_event->menuName != RE::BarterMenu::MENU_NAME)) {
				return RE::BSEventNotifyControl::kContinue;
			}
			// Contract: this event's thread is not documented. BSTEventSource's Add/RemoveEventSink
			// take its lock, defer while it notifies, and never add a sink twice
			// (CommonLib's BSTEvent.h), so they are safe from here.
			if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
				if (a_event->opening) {
					events->AddEventSink<RE::TESEquipEvent>(&EquipSink::Get());
				} else {
					events->RemoveEventSink<RE::TESEquipEvent>(&EquipSink::Get());
				}
			}
			if (a_event->opening) {
				return RE::BSEventNotifyControl::kContinue;
			}
			if (const auto id = g_shownInMenu.exchange(0); id != 0) {
				SKSE::GetTaskInterface()->AddTask([id] { ReplenishTaken(id); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		static MenuSink& Get()
		{
			static MenuSink sink;
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
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(&MenuSink::Get());
		} else {
			logger::error("UI missing: items taken back in the trade menu are not replaced");
		}
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
				Dressing::UnlockOutfitCopies(*actor, assignment.lockedCopies);  // before PutBack, which skips a locked copy
				Dressing::PutBack(*actor, assignment.takenOff);
				++released;
			}
		}
		SyncManaged();
		Notify(released == total ? std::format("Livery: released {} characters.", total) :
								   std::format("Livery: released {} of {} characters; the others are out of reach and keep their items.", released, total));
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
		auto text = pieces > 0 ? std::format("Livery: {} now wears {}{}", NameOf(a_actor), outfit ? QuotedOutfitName(outfit) : "the default items",
									 SituationStore::Suffix(worn)) :
		                         std::format("Livery: {}: own clothes now", NameOf(a_actor));
		Notify(text + (result ? DressingNotes(*result) : "") + ".");
	}

	std::uint32_t PickAnother(RE::FormID a_actor)
	{
		const auto* assignment = FindAssignment(a_actor);
		if (!assignment || assignment->pieces.empty()) {
			return 0;
		}
		const auto  rows = LiveRows(*assignment);
		const auto& row = rows[RowOf(assignment->worn)];
		if (!row.random) {
			return 0;
		}
		std::vector<std::uint32_t> others;
		std::ranges::copy_if(row.outfits, std::back_inserter(others), [assignment](std::uint32_t a_outfit) { return a_outfit != assignment->wornOutfit; });
		return others.empty() ? 0 : others[RandomIndex(others.size())];
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
	// the pieces then read as unlocked, which PutBackOn sets right again at ResumeLocks. Their
	// outfit copies are unlocked too, so the preview can show one; ResumeLocks locks them again.
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
		Dressing::UnlockOutfitCopies(a_actor, assignment->lockedCopies);
		assignment->lockedCopies.clear();
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

	void NoteShownInMenu(RE::Actor& a_actor)
	{
		const auto id = a_actor.GetFormID();
		if (id != kPlayerRef && FindAssignment(id)) {
			g_shownInMenu.store(id);
		}
	}

	bool IsDressedNpc(RE::FormID a_actor)
	{
		std::lock_guard lock{ g_managedLock };
		return std::ranges::find(g_dressed, a_actor) != g_dressed.end();
	}

	bool TakeOffAfterPass(RE::FormID a_actor)
	{
		return TakeOffGiven(a_actor, "after the game's worn-gear pass");
	}
}
