// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// Who Livery dresses in what (docs/design.md §2, §5): kept in the SKSE co-save, put
// back on when another mod took a piece off, released when an NPC dies. Until the
// outfit window exists, keys drive it: F10 dresses the crosshair target (the player
// when there is none) in the pieces Shift+F10 copied from someone, Alt+F10 releases
// the target, F8 releases everyone (the uninstall step).
namespace Assignments
{
	// Contract: call once from SKSEPluginLoad.
	void RegisterCoSave();

	// Contract: call once at kDataLoaded.
	void RegisterSinks();

	// Whether a keyboard key going down (DirectInput scan code) is one of the assignment
	// keys; when it is, its command is queued as an SKSE task for `a_target`.
	// Contract: call from the input path while handling that key's event, since the
	// modifiers are read from the keyboard's current state.
	bool HandleKey(std::uint32_t a_scanCode, RE::ObjectRefHandle a_target);

	// Takes Livery's locks off the pieces it put on an assigned NPC and holds off putting
	// them back on, so the preview can dress them freely; the assignment stays. Nothing
	// for the player, whose pieces are never locked, or for an actor without an assignment.
	// Contract: game thread; one actor at a time, ended by ResumeLocks or by a revert.
	void SuspendLocks(RE::Actor& a_actor);

	// Ends the suspension: the assigned pieces they still carry go back on, locked
	// (Dressing::PutBackOn). The actor may be gone by then; the suspension ends either way.
	// Contract: game thread.
	void ResumeLocks(RE::FormID a_actor);
}
