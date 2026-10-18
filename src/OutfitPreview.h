// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace RE
{
	class Actor;
	class TESObjectARMO;
}

// Dresses the captured character in a set of armor pieces while the capture
// window is open, and puts back exactly what they had (see OutfitPreview.cpp).
// Tailor is not involved: an NPC wears a runtime outfit written into their base
// record, as Tailor 3.0's own preview does, and the player wears inventory copies.
//
// Contract: every function runs on the game thread (SKSE task). One preview at
// a time: Begin before SetPiece or Show; End or Abandon ends it. Begin on an
// actor without a base record fails; children are refused before the window opens.
namespace OutfitPreview
{
	// Snapshots the character and dresses them in `pieces`. Returns "" when the
	// preview is on, or why it is off (shown over the greyed Live Preview tick).
	std::string Begin(RE::Actor& actor, const std::vector<RE::TESObjectARMO*>& pieces);

	// Adds or removes one piece of the current set (a tick in the window). Does
	// nothing without Begin.
	void SetPiece(RE::TESObjectARMO* armor, bool on);

	// Dresses them in exactly these pieces (an outfit selected on the outfit
	// pages). Does nothing without Begin.
	void Show(const std::vector<RE::TESObjectARMO*>& pieces);

	// Puts back what Begin found, dressing the character again in the very copies
	// they wore, so an NPC's pieces rolled from leveled lists stay as rolled: the
	// window closes or Live Preview goes off. Does nothing without Begin.
	void End();

	// Puts the base record's pointers back without dressing anyone: the world
	// is about to be replaced (kPreLoadGame, kNewGame), and no NPC record may
	// keep pointing at a runtime outfit. Does nothing without Begin.
	void Abandon();

	bool IsActive();
}
