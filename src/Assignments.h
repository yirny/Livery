// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Dressing.h"
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

	// Whether a keyboard key going down (DirectInput scan code) is the release-everyone
	// key; when it is, the release is queued as an SKSE task.
	// Contract: call from the input path while handling that key's event.
	bool HandleKey(std::uint32_t a_scanCode);

	// Makes `a_armor` their default outfit, worn wherever no chosen situation applies, and
	// dresses them for their situations now: in the default, kept on and an NPC locked,
	// or in a situation outfit they have. `a_outfit` is the library outfit the pieces came
	// from, 0 for none. NPCs get the pieces they lack, the player only when the settings
	// allow it. Their own pieces that come off are noted and go back on at release. Shows
	// the result as a HUD message.
	// Contract: game thread; no preview is on (OutfitPreview::End ran).
	void Assign(RE::Actor& a_actor, const std::vector<RE::TESObjectARMO*>& a_armor, std::uint32_t a_outfit);

	// What the capture window's Save page changes for one actor (docs/ui-redesign.md §4):
	// the rows that get the library outfit `outfit` and those that lose what they have, each
	// a situation or kDefault for the default. A situation outfit is read from the library
	// each time it goes on; the default keeps `armor`, the outfit's pieces as they are now.
	// Without a default they wear their own clothes where no chosen situation applies.
	struct Changes
	{
		std::uint32_t                      outfit = 0;
		std::vector<RE::TESObjectARMO*>    armor;
		std::vector<Situations::Situation> assign;
		std::vector<Situations::Situation> remove;
	};

	// Makes every change, then dresses them once, anew, for their situations now, so an
	// outfit whose pieces changed goes on even for the situation they already wear. Shows one
	// HUD message.
	// Contract: game thread; no preview is on (OutfitPreview::End ran); no row is in both lists.
	void Change(RE::Actor& a_actor, const Changes& a_changes);

	// Dresses them anew for their situations now, even in the outfit they already wear, so a
	// library outfit whose pieces just changed goes on at once; `a_leaveOut` stays out of this
	// dressing only (pieces that could not be moved to them). Nothing for an actor Livery does
	// not dress. Shows a HUD message.
	// Contract: game thread; no preview is on (OutfitPreview::End ran).
	void Refresh(RE::Actor& a_actor, const std::vector<RE::TESObjectARMO*>& a_leaveOut);

	// Dresses them for `a_now` when that means another outfit than the one they wear: the
	// first situation they have an outfit for, else the default. A situation outfit that
	// puts nothing on falls back to the default. An assignment left with nothing on them
	// and no situation chosen ends. Returns whether they were redressed.
	// Contract: game thread; no preview is on (the poll checks).
	bool WearFor(RE::Actor& a_actor, const Situations::Applicable& a_now);

	// The actors the situation poll watches: those with an outfit chosen for at least one
	// situation, and those still wearing one whose row ForgetOutfit cleared, whom their
	// next dressing takes out of it. Contract: game thread.
	std::vector<RE::FormID> ActorsWithSituations();

	// Who has library outfit `a_outfit`, for the capture window's Remove: each actor with
	// the rows it is theirs for, Default (pieces from it) first, then the situations in
	// Situations::kChoosable order. Contract: game thread.
	struct Use
	{
		RE::FormID                         actor = 0;
		std::vector<Situations::Situation> rows;
	};
	std::vector<Use> UsesOf(std::uint32_t a_outfit);

	// The outfit has left the library: every situation row holding it is cleared, and a
	// default from it keeps its pieces as pieces that are not a saved outfit (id 0). An
	// assignment left with nothing on them and no situation chosen ends. Nobody is
	// redressed here; returns the actors wearing it for a situation now, whom WearFor
	// takes out of it (the actors not in memory at their next situation change).
	// Contract: game thread.
	std::vector<RE::FormID> ForgetOutfit(std::uint32_t a_outfit);

	// Ends their assignment, situation choices included: Livery's locks and copies go,
	// their own pieces it took off go back on. Nothing for an actor Livery does not dress.
	// Shows a HUD message.
	// Contract: game thread; no preview is on.
	void Unassign(RE::Actor& a_actor);

	// What Livery has chosen for them, for the window's summary line and piece headings.
	struct Choices
	{
		bool                                                     dressed = false;                         // Livery has pieces on them now
		std::vector<RE::FormID>                                  wornArmor;                               // the armor of those pieces
		bool                                                     hasDefault = false;                      // they have a default outfit
		std::uint32_t                                            defaultOutfit = 0;                       // the library outfit the default pieces came from; 0 for none
		std::array<std::uint32_t, Situations::kChoosable.size()> situations{};                            // outfit per situation (Situations::Index); 0 for none
		Situations::Situation                                    worn = Situations::Situation::kDefault;  // what the pieces they wear are for
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
