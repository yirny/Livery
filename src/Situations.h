// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace RE
{
	class Actor;
}

// The situations an actor can have an outfit for besides their default (docs/design.md §7),
// how the ones an actor is in are told, and the poll that redresses assigned actors, the
// player included, when they change (Assignments::WearFor).
//
// Contract: Evaluate runs on the game thread. Start and Stop run from the SKSE message
// handler, on the game thread; the poll thread touches no game state and only queues
// SKSE tasks.
namespace Situations
{
	enum class Situation : std::uint8_t
	{
		kDefault,  // worn where no chosen situation applies; never chosen itself
		kTown,
		kHome,
		kSleep,
		kSwimming,
		kWarm
	};

	// The situations an outfit can be chosen for, in the window's order.
	constexpr std::array kChoosable{ Situation::kTown, Situation::kHome, Situation::kSleep, Situation::kSwimming, Situation::kWarm };

	// Index of a chosen situation in a per-actor choice array. Contract: not kDefault.
	constexpr std::size_t Index(Situation a_situation) noexcept
	{
		return static_cast<std::size_t>(a_situation) - 1;
	}

	// "Town", "Home", ... as the library's situation categories spell them; "Default".
	const char* Name(Situation a_situation);

	// The situations an actor is in now, the most specific first: the first one they have
	// an outfit for is the one worn (Assignments). Empty in combat, and anywhere none
	// applies.
	struct Applicable
	{
		std::array<Situation, kChoosable.size()> items{};
		std::size_t                              count = 0;

		void Add(Situation a_situation)
		{
			items[count++] = a_situation;
		}

		friend bool operator==(const Applicable&, const Applicable&) = default;
	};

	// Contract: game thread.
	Applicable Evaluate(RE::Actor& a_actor);

	// The poll that keeps assigned actors dressed for their situations: Start at
	// kPostLoadGame and kNewGame, Stop at kPreLoadGame.
	void Start();
	void Stop();
}
