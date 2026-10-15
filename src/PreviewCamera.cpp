// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants  - where the actor sits on screen, zoom steps and limits
//   State      - what Begin changed, kept for End
//   Helpers    - free camera fields, player hiding, movement hold, framing, placement
//   API        - Begin / Orbit / End
//
// Follows Tailor 3.0's own outfit preview (src/preview/TailorPreviewSession.cpp,
// PreviewScene.cpp and PreviewSessionPolicy.h in its repository), without its
// backdrop: other people and objects can still stand in the shot.
#include "PCH.h"

#include "PreviewCamera.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <unordered_map>
#include <utility>

namespace
{
	// --- Constants ------------------------------------------------------------

	constexpr float kActorScreenX = 0.45f;  // the actor's place right of the screen centre, in half screen widths
	constexpr float kZoomPerStep = 0.9f;    // camera distance factor per wheel step in
	constexpr float kMinZoom = 0.4f;        // of the distance that frames the whole body
	constexpr float kMaxZoom = 2.0f;

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

	struct HiddenNode
	{
		RE::NiPointer<RE::NiAVObject> hold;  // keeps the node alive until its flag is put back
		bool                          wasCulled;
	};

	// Everything Begin changed, so End can put it back.
	struct Session
	{
		bool                                            active = false;
		RE::ActorHandle                                 actor;
		RE::BSTSmartPointer<RE::TESCameraState>         previousState;
		FreeCameraFields                                freeFields;
		bool                                            forcedThirdPerson = false;  // the player was in first person
		bool                                            blockedMovement = false;    // Begin set the NPC's kMovementBlocked
		bool                                            hidesPlayer = false;        // the actor is an NPC
		std::unordered_map<RE::NiAVObject*, HiddenNode> hiddenPlayer;
		RE::NiPoint3                                    pivot;             // the body's centre, which the camera turns around
		float                                           frontHeading = 0;  // radians, from the actor towards their front
		float                                           yaw = 0;           // radians, added by Orbit
		float                                           distance = 0;      // game units that frame the whole body
		float                                           zoom = 1;          // factor on distance
		float                                           tanHalfFOV = 0;    // of the horizontal field of view
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

	// Culls the node and every node under it: room and portal rendering can draw
	// a child whose parent is culled. Running it again culls nodes the game showed
	// since and adds new ones; each keeps the flag it had when first seen.
	void HideBranch(RE::NiAVObject* node, std::unordered_map<RE::NiAVObject*, HiddenNode>& hidden)
	{
		if (!node) {
			return;
		}
		hidden.try_emplace(node, HiddenNode{ RE::NiPointer<RE::NiAVObject>(node), node->GetAppCulled() });
		node->SetAppCulled(true);
		if (auto* branch = node->AsNode()) {
			for (auto& child : branch->GetChildren()) {
				HideBranch(child.get(), hidden);
			}
		}
	}

	// Both of the player's bodies: which one the free camera draws depends on the view it started from.
	void HidePlayer(Session& session)
	{
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			HideBranch(player->Get3D(false), session.hiddenPlayer);
			HideBranch(player->Get3D(true), session.hiddenPlayer);
		}
	}

	void ShowPlayer(Session& session)
	{
		for (auto& [node, entry] : session.hiddenPlayer) {
			node->SetAppCulled(entry.wasCulled);
		}
		session.hiddenPlayer.clear();
	}

	// Keeps the NPC where the camera frames them (Tailor's movement-only hold);
	// their AI and animations keep running. Returns true when it set the flag.
	bool HoldMovement(RE::Actor& actor)
	{
		auto&      flags = actor.GetActorRuntimeData().boolFlags;
		const bool wasBlocked = flags.any(RE::Actor::BOOL_FLAGS::kMovementBlocked);
		flags.set(RE::Actor::BOOL_FLAGS::kMovementBlocked);
		if (auto* controller = actor.GetCharController()) {
			controller->SetLinearVelocityImpl(RE::hkVector4(0.0f));
		}
		return !wasBlocked;
	}

	// Game units from the feet to the top of the head; the bounding sphere's
	// diameter when the skeleton has no head node.
	float BodyHeight(RE::Actor& actor, RE::NiAVObject& root)
	{
		constexpr float kHeadToTop = 1.08f;  // the head node sits at about 93% of a standing body

		const float feet = actor.GetPosition().z;
		if (const auto* head = root.GetObjectByName(RE::BSFixedString("NPC Head [Head]"))) {
			if (const float height = (head->world.translate.z - feet) * kHeadToTop; height > 0.0f) {
				return height;
			}
		}
		return root.worldBound.radius * 2.0f;
	}

	// The distance at which the whole body fits the screen height, seen from the actor's front.
	bool Frame(RE::Actor& actor, RE::PlayerCamera& camera, Session& session)
	{
		constexpr float kMargin = 1.15f;  // room around the body

		auto*      root = actor.Get3D(false);
		const auto screen = RE::BSGraphics::Renderer::GetScreenSize();
		if (!root || screen.width == 0 || screen.height == 0) {
			return false;
		}
		const float height = BodyHeight(actor, *root);
		const auto  feet = actor.GetPosition();
		// worldFOV taken as the horizontal field of view, as Tailor's own framing does.
		const float degrees = camera.GetRuntimeData2().worldFOV;
		session.tanHalfFOV = std::tan(degrees * 0.5f * std::numbers::pi_v<float> / 180.0f);
		const float tanHalfVertical = session.tanHalfFOV * static_cast<float>(screen.height) / static_cast<float>(screen.width);
		session.pivot = { feet.x, feet.y, feet.z + height * 0.5f };
		session.distance = height * 0.5f * kMargin / tanHalfVertical;
		session.frontHeading = actor.GetAngleZ();
		return std::isfinite(session.distance) && session.distance > 0.0f;
	}

	// The camera faces the body's centre from the actor's front turned by yaw,
	// shifted sideways so the actor shows kActorScreenX right of the screen centre.
	void Place(const Session& session)
	{
		auto* camera = RE::PlayerCamera::GetSingleton();
		auto* free = camera ? FreeState(*camera) : nullptr;
		if (!free || camera->currentState.get() != free) {
			return;
		}
		const float        heading = session.frontHeading + session.yaw;  // from the actor towards the camera
		const float        distance = session.distance * session.zoom;
		const float        shift = kActorScreenX * distance * session.tanHalfFOV;
		const RE::NiPoint3 toCamera{ std::sin(heading), std::cos(heading), 0.0f };
		const RE::NiPoint3 cameraRight{ -std::cos(heading), std::sin(heading), 0.0f };

		free->translation = session.pivot + toCamera * distance - cameraRight * shift;
		free->rotation = { 0.0f, std::atan2(-toCamera.x, -toCamera.y) };  // x: pitch, y: the heading it faces
		free->zUpDown = {};
		free->verticalDirection = 0;
		free->lockToZPlane = false;
		camera->Update();
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
		if (!Frame(actor, *camera, session)) {
			return false;
		}

		// First person does not draw the player's third-person body, the one the preview shows.
		const bool isPlayer = actor.IsPlayerRef();
		if (isPlayer && camera->IsInFirstPerson()) {
			if (!camera->ForceThirdPerson() || !camera->IsInThirdPerson()) {
				logger::warn("Preview camera: could not switch to third person");
				if (!camera->IsInFirstPerson()) {
					camera->ForceFirstPerson();
				}
				return false;
			}
			session.forcedThirdPerson = true;
		}

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
		free->SetInputEventHandlingEnabled(false);

		session.actor = actor.GetHandle();
		if (!isPlayer) {
			session.blockedMovement = HoldMovement(actor);
			session.hidesPlayer = true;
			HidePlayer(session);
		}
		session.active = true;
		g_session = std::move(session);
		Place(g_session);
		logger::info("Preview camera on {:08X}", actor.GetFormID());
		return true;
	}

	void Orbit(float turns, float wheelSteps)
	{
		if (!g_session.active) {
			return;
		}
		g_session.yaw += turns * 2.0f * std::numbers::pi_v<float>;
		g_session.zoom = std::clamp(g_session.zoom * std::pow(kZoomPerStep, wheelSteps), kMinZoom, kMaxZoom);
		if (g_session.hidesPlayer) {
			HidePlayer(g_session);  // the game can show culled nodes again as the view changes
		}
		Place(g_session);
	}

	void End()
	{
		if (!g_session.active) {
			return;
		}
		auto session = std::exchange(g_session, Session{});
		ShowPlayer(session);
		if (session.blockedMovement) {
			if (const auto actor = session.actor.get()) {
				actor->GetActorRuntimeData().boolFlags.reset(RE::Actor::BOOL_FLAGS::kMovementBlocked);
			}
		}

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
