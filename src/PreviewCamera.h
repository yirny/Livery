// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "CaptureWindow.h"

namespace RE
{
	class Actor;
}

// The live preview's camera (see PreviewCamera.cpp): the engine's free camera,
// as the tfc console command gives it, with the engine's own input off. It
// starts where the camera is and moves only as the window reports mouse and
// key input over the game (CaptureWindow::ViewInput).
//
// Contract: every function runs on the game thread.
namespace PreviewCamera
{
	// Enters the free camera when the camera is in ordinary first or third
	// person; otherwise changes nothing and returns false. Previewing the player
	// from first person switches to third person first: first person does not
	// draw their body.
	bool Begin(RE::Actor& actor);

	// Applies one frame of input. Does nothing without Begin.
	void Move(const CaptureWindow::ViewInput& input);

	// Puts back everything Begin changed; does nothing when Begin did not take the camera.
	void End();
}
