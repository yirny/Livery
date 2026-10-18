// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Dressing.h"

#include <cstdint>
#include <optional>
#include <vector>

// Who Livery dresses in what (docs/design.md §2, §5): kept in the SKSE co-save, put
// back on when another mod took a piece off, released when an NPC dies. The capture
// window assigns and releases; F8 releases everyone (the uninstall step); the trade-menu
// filter (TradeFilter) asks which copies to hide.
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

	// Dresses them in `a_armor` and keeps it on, an NPC locked, replacing an assignment
	// they had; `a_outfit` is the library outfit the pieces came from, 0 for none. NPCs
	// get the pieces they lack, the player only when the settings allow it. Their own
	// pieces that come off are noted and go back on at release. Shows the result as a HUD
	// message.
	// Contract: game thread; no preview is on (OutfitPreview::End ran).
	void Assign(RE::Actor& a_actor, const std::vector<RE::TESObjectARMO*>& a_armor, std::uint32_t a_outfit);

	// Ends their assignment: Livery's locks and copies go, their own pieces it took off go
	// back on. Nothing for an actor Livery does not dress. Shows a HUD message.
	// Contract: game thread; no preview is on.
	void Unassign(RE::Actor& a_actor);

	// The library outfit Livery dresses them in: 0 for pieces not saved as one, nullopt
	// when Livery does not dress them. Contract: game thread.
	std::optional<std::uint32_t> OutfitOf(RE::FormID a_actor);

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
