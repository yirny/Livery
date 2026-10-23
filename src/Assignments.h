// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Dressing.h"
#include "OutfitRows.h"
#include "Situations.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// Who Livery dresses in what (docs/design.md §2, §5, §7): kept in the SKSE co-save, put
// back on when another mod took a piece off, released when an NPC dies. The capture
// window assigns and releases; F8 releases everyone (the uninstall step); the trade-menu
// filter (TradeFilter) asks which copies to hide; the situation poll (Situations) asks
// who has situation outfits and redresses them.
namespace Assignments
{
	// Contract: call once from SKSEPluginLoad.
	void RegisterCoSave();

	// Contract: call once at kDataLoaded.
	void RegisterSinks();

	// The uninstall step: ends every assignment, as Unassign does for one actor, Livery's
	// copies deleted. Actors not in memory keep their locks and copies. Shows a HUD message.
	// Contract: game thread; a save is loaded and no preview is on (the capture window is closed).
	void ReleaseEveryone();

	// Gives their Default, worn wherever no situation applies, library outfit `a_outfit`
	// (OutfitRows::Give; the Default's pieces go when it becomes `chosen`), or with `a_outfit`
	// 0 makes `a_armor` the Default's pieces, `chosen` 0 and random off. Then dresses them for
	// their situations now: in the Default, kept on and an NPC locked, or in a situation
	// outfit they have. A library outfit is read from the library; its pieces not in
	// `a_armor` stay out of this dressing only (pieces that could not be moved to them). NPCs
	// get the pieces they lack, the player only when the settings allow it. Their own pieces
	// that come off are noted and go back on at release. Shows the result as a HUD message.
	// Contract: game thread; no preview is on (OutfitPreview::End ran).
	void Assign(RE::Actor& a_actor, const std::vector<RE::TESObjectARMO*>& a_armor, std::uint32_t a_outfit);

	// What the capture window changes for one actor (docs/situation-management-ui.md §1): the
	// rows that get library outfit `outfit` (OutfitRows::Give) and those that lose it
	// (OutfitRows::Take), each a situation or kDefault. Every row reads its outfit from the
	// library each time it goes on.
	struct Changes
	{
		std::uint32_t                      outfit = 0;
		std::vector<Situations::Situation> assign;
		std::vector<Situations::Situation> remove;
		std::vector<RE::TESObjectARMO*>    leaveOut;  // stays out of this dressing only (pieces that could not be moved to them)
	};

	// Makes every change, then dresses them once, anew, for their situations now, so an
	// outfit whose pieces changed goes on even for the situation they already wear; a random
	// row in `assign` worn now wears `outfit` as its pick. Shows one HUD message.
	// Contract: game thread; no preview is on (OutfitPreview::End ran); no row is in both lists.
	void Change(RE::Actor& a_actor, const Changes& a_changes);

	// Manage Situations' Save Changes: their rows become `a_rows` (the rows of situations
	// removed in situations.json stay empty), and with `a_dropDefaultPieces` the Default's
	// pieces go. Nobody is dressed here: Refresh with the returned pick dresses them for the new
	// rows, and ends the record when that leaves nothing on them and no situation row holding
	// an outfit. Returns the outfit a random row they wear now gained first, which that dressing
	// wears as the row's pick, 0 for none; nullopt when they cannot be dressed (a HUD message
	// says why), with nothing changed.
	// Contract: game thread.
	std::optional<std::uint32_t> SetRows(RE::Actor& a_actor, const OutfitRows::Rows& a_rows, bool a_dropDefaultPieces);

	// Dresses them anew for their situations now, even in the outfit they already wear, so a
	// library outfit whose pieces just changed goes on at once; a random row keeps its pick
	// while it is on the list, or with `a_pick` (SetRows) the row they wear wears that while it
	// is on its list. `a_leaveOut` stays out of this dressing only (pieces that could not be
	// moved to them). Nothing for an actor Livery does not dress. Shows a HUD message.
	// Contract: game thread; no preview is on (OutfitPreview::End ran).
	void Refresh(RE::Actor& a_actor, const std::vector<RE::TESObjectARMO*>& a_leaveOut, std::uint32_t a_pick = 0);

	// Dresses them for `a_now` when that means another outfit than the one they wear: the
	// first situation whose row holds an outfit, else the Default (OutfitRows::Resolve). A
	// random row picks as it goes on and keeps the pick while it stays worn and on the list.
	// A situation outfit that puts nothing on falls back to the Default. An outfit that left
	// the library (another save's row) counts as not on its row (OutfitRows::WithoutGone). An
	// assignment left with nothing on them and no situation row holding an outfit ends.
	// Returns whether they were redressed.
	// Contract: game thread; no preview is on (the poll checks).
	bool WearFor(RE::Actor& a_actor, const Situations::Applicable& a_now);

	// The actors the situation poll watches: those with a situation row holding an outfit,
	// and those still wearing one whose row ForgetOutfit cleared, whom their next dressing
	// takes out of it. Contract: game thread.
	std::vector<RE::FormID> ActorsWithSituations();

	// Who has library outfit `a_outfit`, for the capture window's Remove: each actor with
	// the rows that hold it (OutfitRows::Holds), Default first, then the situations in
	// Situations::kChoosable order. Contract: game thread.
	struct Use
	{
		RE::FormID                         actor = 0;
		std::vector<Situations::Situation> rows;
	};
	std::vector<Use> UsesOf(std::uint32_t a_outfit);

	// Who has library outfits on situation `a_situation`'s list, for the capture window's
	// Remove Situation: each actor with how many. Contract: game thread.
	struct Holder
	{
		RE::FormID  actor = 0;
		std::size_t outfits = 0;
	};
	std::vector<Holder> HoldersOf(Situations::Situation a_situation);

	// The outfit has left the library: it leaves every row (OutfitRows::Take), so an outfit
	// hidden behind a `chosen` that was it is not worn. A Default this leaves wearing nothing
	// gets `a_pieces`, the outfit's loaded pieces read before Library::Remove, as its pieces;
	// a Default worn from it stays on. An assignment left with nothing on them and no
	// situation row holding an outfit ends. Nobody is redressed here; returns the actors
	// wearing it for a situation now, whom WearFor takes out of it (the actors not in memory
	// at their next situation change).
	// Contract: game thread.
	std::vector<RE::FormID> ForgetOutfit(std::uint32_t a_outfit, const std::vector<RE::TESObjectARMO*>& a_pieces);

	// The situation has left situations.json (SituationStore::Remove): every actor's row for
	// it is cleared, so a restore brings it back empty. An assignment left with nothing on
	// them and no situation row holding an outfit ends. Nobody is redressed here; returns the
	// actors wearing it now, whom WearFor takes out of it (the actors not in memory at their
	// next situation change).
	// Contract: game thread; not kDefault.
	std::vector<RE::FormID> ForgetSituation(Situations::Situation a_situation);

	// Ends their assignment, situation choices included: Livery's locks and copies go,
	// their own pieces it took off go back on. Nothing for an actor Livery does not dress.
	// Shows a HUD message.
	// Contract: game thread; no preview is on.
	void Unassign(RE::Actor& a_actor);

	// What Livery has chosen for them, for the window's rows, summary line and piece headings.
	struct Choices
	{
		bool                    dressed = false;                         // Livery has pieces on them now
		std::vector<RE::FormID> wornArmor;                               // the armor of those pieces
		OutfitRows::Rows        rows;                                    // the Default, then the situations; gone outfits included (OutfitRows::WithoutGone)
		bool                    defaultPieces = false;                   // the Default has armor not in the wardrobe
		Situations::Situation   worn = Situations::Situation::kDefault;  // the row the pieces they wear are for
		std::uint32_t           wornOutfit = 0;                          // the library outfit they are from; 0 for none or the Default's pieces
	};

	// nullopt when Livery does not dress them. Contract: game thread.
	std::optional<Choices> ChoicesOf(RE::FormID a_actor);

	// Takes Livery's locks off the pieces it put on an assigned NPC and holds off putting
	// them back on, so the preview can dress them freely; the assignment stays. Nothing
	// for the player, whose pieces are never locked, or for an actor without an assignment.
	// Contract: game thread; one actor at a time, ended by ResumeLocks or by a revert.
	void SuspendLocks(RE::Actor& a_actor);

	// Ends the suspension: the assigned pieces they still carry go back on, locked
	// (Dressing::PutBackOn). The actor may be gone by then; the suspension ends either way.
	// Contract: game thread.
	void ResumeLocks(RE::FormID a_actor);

	// Marks the copies Livery added to an assigned NPC as items of their default outfit, so the
	// engine's trade-menu filter leaves them out (docs/design.md §3), and returns what
	// UnmarkAddedCopies takes. Nothing for the player, an actor Livery does not dress, or an NPC
	// without a default outfit.
	// Contract: game thread; UnmarkAddedCopies follows in the same call, before anything else
	// touches the inventory.
	std::vector<Dressing::InstanceKey> MarkAddedCopies(RE::Actor& a_actor);
	void                               UnmarkAddedCopies(RE::Actor& a_actor, const std::vector<Dressing::InstanceKey>& a_keys);
}
