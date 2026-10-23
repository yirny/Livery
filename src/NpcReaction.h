// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace RE
{
	class Actor;
}

// NPC Reaction: the captured NPC turns their head to the player and says a generic
// greeting (Skyrim.esm's DialogueGenericHello) as the capture window opens, while
// [General] NpcReaction is on. Only with Live Preview on: a frozen game would hold the
// head turn and the speech until the window closes.
//
// Contract: every function runs on the game thread.
namespace NpcReaction
{
	// Turns the actor's head to the player and has them say the greeting; does nothing
	// for the player, a dead actor or with the setting off.
	void Begin(RE::Actor& actor);

	// Lets the actor's head go where their AI wants again. Does nothing without Begin
	// or once the actor is gone.
	void End();
}
