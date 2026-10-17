// SPDX-License-Identifier: GPL-3.0-or-later
//
// The captured NPC's locomotion hold; see MovementHold.h.
#include "PCH.h"

#include "MovementHold.h"

#include <utility>

namespace
{
	// Kept as a handle: the hold spans many frames, and the actor may unload meanwhile.
	RE::ActorHandle g_actor;               // none while nothing is held
	bool            g_wasBlocked = false;  // movement was blocked before Begin, so End leaves it blocked

	void Block(RE::Actor& a_actor)
	{
		a_actor.GetActorRuntimeData().boolFlags.set(RE::Actor::BOOL_FLAGS::kMovementBlocked);
		if (auto* controller = a_actor.GetCharController()) {
			controller->SetLinearVelocityImpl(RE::hkVector4{});
		}
	}
}

namespace MovementHold
{
	void Begin(RE::Actor& a_actor)
	{
		if (a_actor.IsPlayerRef()) {
			return;
		}
		g_actor = a_actor.GetHandle();
		g_wasBlocked = a_actor.GetActorRuntimeData().boolFlags.any(RE::Actor::BOOL_FLAGS::kMovementBlocked);
		Block(a_actor);
	}

	void Enforce()
	{
		if (const auto actor = g_actor.get()) {
			Block(*actor);
		}
	}

	void End()
	{
		const auto actor = std::exchange(g_actor, {}).get();
		if (actor && !g_wasBlocked) {
			actor->GetActorRuntimeData().boolFlags.reset(RE::Actor::BOOL_FLAGS::kMovementBlocked);
		}
	}
}
