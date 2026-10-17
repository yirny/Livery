// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace RE
{
	class Actor;
}

// Keeps the captured NPC where they stand while the capture window is open,
// as Tailor 3.0's own outfit preview does (ActorMovementHold in its
// src/preview/PreviewSessionPolicy.h). Only locomotion is held: AI, life
// state and animation stay the engine's. The player is never held; the
// window already keeps every input from the game.
//
// Contract: every function runs on the game thread.
namespace MovementHold
{
	// Holds the actor until End; does nothing for the player.
	void Begin(RE::Actor& actor);

	// Blocks movement again and stops the character controller, once a frame
	// as Tailor does. Does nothing without Begin or once the actor is gone.
	void Enforce();

	// Lets the actor walk again, unless their movement was already blocked
	// before Begin. Does nothing without Begin.
	void End();
}
