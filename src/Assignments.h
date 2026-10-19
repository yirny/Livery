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

	// Chooses the library outfit `a_outfit` for one of their situations (0 forgets the
	// choice) and dresses them for their situations now, as Assign does. Without a default
	// outfit they wear their own clothes outside the chosen situations. Shows a HUD message.
	// Contract: game thread; no preview is on; `a_situation` is not kDefault.
	void SetSituation(RE::Actor& a_actor, Situations::Situation a_situation, std::uint32_t a_outfit);

	// Dresses them for `a_now` when that means another outfit than the one they wear: the
	// first situation they have an outfit for, else the default. A situation outfit that
	// puts nothing on falls back to the default. Returns whether they were redressed.
	// Contract: game thread; no preview is on (the poll checks).
	bool WearFor(RE::Actor& a_actor, const Situations::Applicable& a_now);

	// The actors with an outfit chosen for at least one situation. Contract: game thread.
	std::vector<RE::FormID> ActorsWithSituations();

	// Ends their assignment, situation choices included: Livery's locks and copies go,
	// their own pieces it took off go back on. Nothing for an actor Livery does not dress.
	// Shows a HUD message.
	// Contract: game thread; no preview is on.
	void Unassign(RE::Actor& a_actor);

	// What Livery has chosen for them, for the window's summary line.
	struct Choices
	{
		bool                                                     dressed = false;                         // Livery has pieces on them now
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
