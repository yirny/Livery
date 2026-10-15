// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants  - speeds and sensitivities
//   State      - what Begin changed, kept for End
//   Helpers    - free camera fields, the toggle that keeps the control switches
//   API        - Begin / Move / End
//
// Entering and leaving the free camera follows Tailor 3.0's own outfit preview
// (src/preview/TailorPreviewSession.cpp and PreviewSessionPolicy.h in its
// repository); the movement is the plugin's own, driven by the window.
#include "PCH.h"

#include "PreviewCamera.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

namespace
{
	// --- Constants ------------------------------------------------------------

	constexpr float kMoveSpeed = 300.0f;            // game units per second with W / A / S / D
	constexpr float kFastFactor = 3.0f;             // on kMoveSpeed while Shift is held
	constexpr float kLookRadiansPerPixel = 0.004f;  // of a left drag
	constexpr float kPanUnitsPerPixel = 1.0f;       // of a right drag
	constexpr float kZoomUnitsPerStep = 40.0f;      // forward per wheel step
	constexpr float kMaxPitch = 89.0f * std::numbers::pi_v<float> / 180.0f;

	// --- State ----------------------------------------------------------------

	struct FreeCameraFields
	{
		RE::NiPoint3         translation;
		RE::BSTPoint2<float> rotation;
		RE::BSTPoint2<float> zUpDown;
		std::int16_t         verticalDirection = 0;
		bool                 lockToZPlane = false;
		bool                 inputEnabled = false;
	};

	// Everything Begin changed, so End can put it back.
	struct Session
	{
		bool                                    active = false;
		RE::BSTSmartPointer<RE::TESCameraState> previousState;
		FreeCameraFields                        freeFields;
		bool                                    forcedThirdPerson = false;  // the player was in first person
	};

	Session g_session;

	// --- Helpers --------------------------------------------------------------

	RE::FreeCameraState* FreeState(RE::PlayerCamera& camera)
	{
		return static_cast<RE::FreeCameraState*>(camera.GetRuntimeData().cameraStates[RE::CameraState::kFree].get());
	}

	FreeCameraFields ReadFields(RE::FreeCameraState& state)
	{
		return { state.translation, state.rotation, state.zUpDown, state.verticalDirection, state.lockToZPlane,
			state.IsInputEventHandlingEnabled() };
	}

	void WriteFields(RE::FreeCameraState& state, const FreeCameraFields& fields)
	{
		state.translation = fields.translation;
		state.rotation = fields.rotation;
		state.zUpDown = fields.zUpDown;
		state.verticalDirection = fields.verticalDirection;
		state.lockToZPlane = fields.lockToZPlane;
		state.SetInputEventHandlingEnabled(fields.inputEnabled);
	}

	// The free camera saves the control switches as it starts, turns every one
	// on, and puts its copy back as it ends; keeping the switches as they are
	// now leaves one another mod holds off still off afterwards (as Tailor does).
	void ToggleFreeCamera(RE::PlayerCamera& camera)
	{
		auto*         controls = RE::ControlMap::GetSingleton();
		std::uint32_t enabled = 0;
		std::uint32_t stored = 0;
		if (controls) {
			controls->GetControlsState(enabled, stored);
		}
		camera.ToggleFreeCameraMode(false);  // false: time keeps running
		if (controls) {
			controls->SetControlsState(enabled, stored);
		}
	}
}

namespace PreviewCamera
{
	// --- API ------------------------------------------------------------------

	bool Begin(RE::Actor& actor)
	{
		auto* camera = RE::PlayerCamera::GetSingleton();
		auto* free = camera ? FreeState(*camera) : nullptr;
		if (g_session.active || !free || !camera->currentState || (!camera->IsInFirstPerson() && !camera->IsInThirdPerson())) {
			return false;
		}
		Session session;
		if (actor.IsPlayerRef() && camera->IsInFirstPerson()) {
			if (!camera->ForceThirdPerson() || !camera->IsInThirdPerson()) {
				logger::warn("Preview camera: could not switch to third person");
				if (!camera->IsInFirstPerson()) {
					camera->ForceFirstPerson();
				}
				return false;
			}
			session.forcedThirdPerson = true;
		}

		// The free camera starts where the camera is; its fields are kept only to put them back.
		session.previousState = camera->currentState;
		session.freeFields = ReadFields(*free);
		ToggleFreeCamera(*camera);
		if (camera->currentState.get() != free) {
			logger::warn("Preview camera: the free camera did not start");
			WriteFields(*free, session.freeFields);
			if (camera->currentState.get() != session.previousState.get()) {
				camera->SetState(session.previousState.get());
			}
			if (session.forcedThirdPerson) {
				camera->ForceFirstPerson();
			}
			return false;
		}
		free->SetInputEventHandlingEnabled(false);  // the window drives it (Move)
		free->zUpDown = {};
		free->verticalDirection = 0;
		free->lockToZPlane = false;

		session.active = true;
		g_session = std::move(session);
		logger::info("Preview camera on {:08X}", actor.GetFormID());
		return true;
	}

	// rotation.x is the pitch (positive looks down), rotation.y the heading the
	// camera faces, in Skyrim's convention: heading 0 faces +Y, and turns clockwise
	// seen from above. The engine reads the fields every frame, so nothing else
	// needs an update.
	void Move(const CaptureWindow::ViewInput& input)
	{
		auto* camera = RE::PlayerCamera::GetSingleton();
		auto* free = camera ? FreeState(*camera) : nullptr;
		if (!g_session.active || !free || camera->currentState.get() != free) {
			return;
		}

		free->rotation.y += input.lookX * kLookRadiansPerPixel;
		free->rotation.x = std::clamp(free->rotation.x + input.lookY * kLookRadiansPerPixel, -kMaxPitch, kMaxPitch);
		const float        yaw = free->rotation.y;
		const float        pitch = free->rotation.x;
		const RE::NiPoint3 forward{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
		const RE::NiPoint3 right{ std::cos(yaw), -std::sin(yaw), 0.0f };
		const RE::NiPoint3 up{ 0.0f, 0.0f, 1.0f };

		const float step = kMoveSpeed * (input.fast ? kFastFactor : 1.0f) * input.seconds;
		auto        move = forward * (input.moveForward * step + input.zoomSteps * kZoomUnitsPerStep);
		move += right * (input.moveRight * step);
		// A right drag takes the view along with the mouse: the camera goes the other way.
		move += right * (-input.panX * kPanUnitsPerPixel) + up * (input.panY * kPanUnitsPerPixel);
		free->translation += move;
	}

	void End()
	{
		if (!g_session.active) {
			return;
		}
		auto  session = std::exchange(g_session, Session{});
		auto* camera = RE::PlayerCamera::GetSingleton();
		auto* free = camera ? FreeState(*camera) : nullptr;
		if (!free || camera->currentState.get() != free) {
			logger::warn("Preview camera: something else changed the camera; left as it is");
			return;
		}
		WriteFields(*free, session.freeFields);
		ToggleFreeCamera(*camera);
		if (session.previousState && camera->currentState.get() != session.previousState.get()) {
			camera->SetState(session.previousState.get());
		}
		if (session.forcedThirdPerson) {
			camera->ForceFirstPerson();
		}
	}
}
