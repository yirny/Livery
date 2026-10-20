// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants  - speeds and sensitivities
//   State      - what Begin changed, kept for End
//   Helpers    - free camera fields, the third person state, the toggle that
//                keeps the control switches
//   API        - Begin / Move / End
//
// Entering and leaving the free camera follows Tailor 3.0's own outfit preview
// (src/preview/TailorPreviewSession.cpp and PreviewSessionPolicy.h in its
// repository); the movement is the plugin's own, driven by the window.
#include "PCH.h"

#include "PreviewCamera.h"

#include "Settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>

namespace
{
	// --- Constants ------------------------------------------------------------

	// The move, pan and zoom rates are at 100% camera speed; Settings::CameraSpeedScale scales them.
	// A left drag turns at the game's own mouse look rate instead (ReadLookRadiansPerPixel).
	constexpr float kMoveUnitsPerSecond = 225.0f;   // with W / A / S / D
	constexpr float kSlowFactor = 0.5f;             // on the move speed while Shift is held, as in tfc
	constexpr float kLookRadiansPerPixel = 0.004f;  // of a left drag, only when the game's settings cannot be read
	constexpr float kPanUnitsPerPixel = 0.5f;       // of a right drag
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
		bool                                    forcedThirdPerson = false;                   // the player was in first person
		bool                                    restoreOrbit = false;                        // the game was in third person, so End puts `orbit` back
		RE::NiPoint2                            orbit;                                       // ThirdPersonState::freeRotation as Begin found it
		RE::ActorHandle                         actor;                                       // the previewed actor, which a left drag turns around
		float                                   pitch = 0.0f;                                // radians in -kMaxPitch..kMaxPitch, positive looks down
		bool                                    invertY = false;                             // the game's Invert Y Axis option, for a left drag up and down
		float                                   lookRadiansPerPixel = kLookRadiansPerPixel;  // of a left drag, from the game's look sensitivity
	};

	// Turns the camera's place around the actor by the angle the view just
	// turned, so the actor keeps its place on screen and seems to turn on the
	// spot (as in Tailor's preview). The actor's current position is the pivot,
	// so it follows an NPC who walks; without the actor the view turns in place.
	void OrbitActor(RE::FreeCameraState& free, const RE::ActorHandle& actor, float radians)
	{
		const auto target = actor.get();
		if (!target || radians == 0.0f) {
			return;
		}
		const auto  pivot = target->GetPosition();
		const float dx = free.translation.x - pivot.x;
		const float dy = free.translation.y - pivot.y;
		const float sin = std::sin(radians);
		const float cos = std::cos(radians);
		// The same rotation the heading applies to its forward vector (sin yaw, cos yaw).
		free.translation.x = pivot.x + dx * cos + dy * sin;
		free.translation.y = pivot.y - dx * sin + dy * cos;
	}

	Session g_session;

	// --- Helpers --------------------------------------------------------------

	RE::FreeCameraState* FreeState(RE::PlayerCamera& camera)
	{
		return static_cast<RE::FreeCameraState*>(camera.GetRuntimeData().cameraStates[RE::CameraState::kFree].get());
	}

	RE::ThirdPersonState* ThirdState(RE::PlayerCamera& camera)
	{
		return static_cast<RE::ThirdPersonState*>(camera.GetRuntimeData().cameraStates[RE::CameraState::kThirdPerson].get());
	}

	float Degrees(float radians)
	{
		return radians * 180.0f / std::numbers::pi_v<float>;
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

	// The game's Invert Y Axis option (SkyrimPrefs.ini). Read once per preview:
	// GetSetting scans the whole collection, and the window holds the game's
	// menus, so the option cannot change while the preview is on.
	bool ReadInvertY()
	{
		auto* prefs = RE::INIPrefSettingCollection::GetSingleton();
		auto* setting = prefs ? prefs->GetSetting("bInvertYValues:Controls") : nullptr;
		if (!setting) {
			logger::warn("Preview camera: the game's Invert Y setting was not found; treated as off");
			return false;
		}
		return setting->GetBool();
	}

	// The game's mouse look rate: pi * fMouseHeadingSensitivity (SkyrimPrefs.ini,
	// the Look Sensitivity slider) * fMouseHeadingXScale (Skyrim.ini) radians per
	// pixel, as measured in the game's own heading turn; the same rate is used up
	// and down, since the game's pitch rate depends on the frame rate. Read once
	// per preview, like ReadInvertY.
	float ReadLookRadiansPerPixel()
	{
		auto* prefs = RE::INIPrefSettingCollection::GetSingleton();
		auto* ini = RE::INISettingCollection::GetSingleton();
		auto* sensitivity = prefs ? prefs->GetSetting("fMouseHeadingSensitivity:Controls") : nullptr;
		auto* xScale = ini ? ini->GetSetting("fMouseHeadingXScale:Controls") : nullptr;
		if (!sensitivity || !xScale) {
			logger::warn("Preview camera: the game's look sensitivity was not found; using the built-in rate");
			return kLookRadiansPerPixel;
		}
		return std::numbers::pi_v<float> * sensitivity->GetFloat() * xScale->GetFloat();
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

		// The way back to third person sets the camera's turn around the player to
		// zero, so End puts back the one it has now.
		if (auto* third = ThirdState(*camera); third && camera->IsInThirdPerson() && !session.forcedThirdPerson) {
			session.orbit = third->freeRotation;
			session.restoreOrbit = true;
		}
		// The free camera starts where the camera is, looking the same way; its fields are kept only to put them back.
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

		session.actor = actor.GetHandle();
		// Brought into -pi..pi once: the engine may keep the angle as 0..2pi.
		session.pitch = std::clamp(std::remainder(free->rotation.x, 2.0f * std::numbers::pi_v<float>), -kMaxPitch, kMaxPitch);
		session.invertY = ReadInvertY();
		session.lookRadiansPerPixel = ReadLookRadiansPerPixel();
		session.active = true;
		g_session = std::move(session);
		logger::info("Preview camera on {:08X} (invert Y: {}, look {:.6f} rad/px)", actor.GetFormID(), g_session.invertY ? "on" : "off",
			g_session.lookRadiansPerPixel);
		return true;
	}

	// rotation.x is the pitch (positive looks down), rotation.y the heading the
	// camera faces, in Skyrim's convention: heading 0 faces +Y, and turns clockwise
	// seen from above. A left drag sideways turns the view and the camera's place
	// around the actor together; up and down tilts in place, as the game's mouse
	// look does (Invert Y Axis included). The engine reads the fields every frame,
	// so nothing else needs an update.
	void Move(const CaptureWindow::ViewInput& input)
	{
		auto* camera = RE::PlayerCamera::GetSingleton();
		auto* free = camera ? FreeState(*camera) : nullptr;
		if (!g_session.active || !free || camera->currentState.get() != free) {
			return;
		}

		const float scale = Settings::CameraSpeedScale();
		const float look = g_session.lookRadiansPerPixel;  // the game's own rate: the camera speed setting does not apply
		const float turn = input.lookX * look;
		free->rotation.y += turn;
		OrbitActor(*free, g_session.actor, turn);
		// The pitch is the session's own, never read back: the engine may store a
		// look above the horizon as an angle near 2pi, which a clamp would turn
		// into looking straight down.
		const float lookY = g_session.invertY ? -input.lookY : input.lookY;
		g_session.pitch = std::clamp(g_session.pitch + lookY * look, -kMaxPitch, kMaxPitch);
		free->rotation.x = g_session.pitch;
		const float        yaw = free->rotation.y;
		const float        pitch = g_session.pitch;
		const RE::NiPoint3 forward{ std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) };
		const RE::NiPoint3 right{ std::cos(yaw), -std::sin(yaw), 0.0f };
		const RE::NiPoint3 up{ 0.0f, 0.0f, 1.0f };

		const float step = kMoveUnitsPerSecond * scale * (input.slow ? kSlowFactor : 1.0f) * input.seconds;
		auto        move = forward * (input.moveForward * step + input.zoomSteps * kZoomUnitsPerStep * scale);
		move += right * (input.moveRight * step);
		// A right drag moves the camera the way the mouse goes; the scene slides the other way.
		const float pan = kPanUnitsPerPixel * scale;
		move += right * (input.panX * pan) + up * (-input.panY * pan);
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
		// Contract: after the switch back, which leaves the turn around the player
		// at zero (seen in game), so it is written once the state is current.
		auto* third = session.restoreOrbit && camera->IsInThirdPerson() ? ThirdState(*camera) : nullptr;
		if (third) {
			logger::info("Preview camera off: the turn around the player put back (the game had moved it by heading {:+.1f} deg, pitch {:+.1f} deg)",
				Degrees(third->freeRotation.x - session.orbit.x), Degrees(third->freeRotation.y - session.orbit.y));
			third->freeRotation = session.orbit;
		}
		if (session.forcedThirdPerson) {
			camera->ForceFirstPerson();
		}
	}
}
