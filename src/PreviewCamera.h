// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace RE
{
	class Actor;
}

// The live preview's camera (see PreviewCamera.cpp): the engine's free camera
// with its own input off, placed to show the previewed actor's whole body on
// the right of the screen, beside the capture window on the left. For an NPC
// the player is hidden, as they usually stand between the camera and the NPC.
//
// Contract: every function runs on the game thread.
namespace PreviewCamera
{
	// Takes the camera when it is in ordinary first or third person; otherwise
	// changes nothing and returns false.
	bool Begin(RE::Actor& actor);

	// Turns the camera around the actor by a horizontal mouse drag, in screen
	// widths (one is a full turn; positive is right), and moves it in by mouse
	// wheel steps (negative is out). Does nothing without Begin.
	void Orbit(float turns, float wheelSteps);

	// Puts back everything Begin changed; does nothing when Begin did not take the camera.
	void End();
}
