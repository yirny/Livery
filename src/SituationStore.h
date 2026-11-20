// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SituationRules.h"
#include "Situations.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

// The situations' names, removed flags and rules (docs/situation-management-ui.md §2,
// docs/custom-situations.md), the presets' and those the user defines, and their order, shared
// by every save in Data/SKSE/Plugins/Livery/situations.json (SituationTable.h). Logs name a
// situation by its key (Situations::Name); the window and the HUD by Name here.
//
// Contract: every function runs on the game thread (SKSE task); the capture window never
// calls here. Load runs once at kDataLoaded, before the co-save is read (Assignments drops
// the rows of removed situations there) and before any other function. The table keeps the
// registry's ids (Situations::AddUser): Load, Create and Duplicate alone add to both; they and
// Apply give the registry the table's order (Situations::Reorder).
namespace SituationStore
{
	// Reads the file and adds its user's situations to the registry. A missing or empty file
	// (nothing but whitespace) means every preset as installed and is not written here. A file
	// that cannot be read, or that a newer version wrote, leaves every preset as installed, no
	// user's situation, and refuses every change, so the file is never overwritten.
	void Load();

	// Whether Load read the file, or found none; false for the whole session when it could not.
	// The co-save then keeps the rows of the user's situations it does not know
	// (SituationTable::KeepsUnknownRow).
	bool WasLoaded();

	// Contract: the reference goes stale at the next Apply, Create, Duplicate, Remove or RestorePresets.
	const std::string& Name(Situations::Situation a_situation);

	// " for Town" for the end of a sentence that says what is worn for it; "" for the Default.
	std::string Suffix(Situations::Situation a_situation);

	// A removed situation offers no row anywhere; Situations::Evaluate still reports it and
	// nothing wears it.
	bool IsRemoved(Situations::Situation a_situation);

	// The situations not removed, in Situations::Order(): the window's rows after the Default.
	std::vector<Situations::Situation> Offered();

	// A situation's rules, a preset's or a user's; nullptr for the Default. Situations::Evaluate
	// reads them on the poll.
	// Contract: the pointer goes stale at the next Apply, Create, Duplicate, Remove or RestorePresets.
	const SituationRules::Rules* RulesOf(Situations::Situation a_situation);

	// A situation's name, and its rules, as the user leaves them.
	struct Edit
	{
		Situations::Situation                situation = Situations::Situation::kDefault;
		std::string                          name;
		std::optional<SituationRules::Rules> rules;  // when they changed; nullopt keeps them. Never the Default's
	};

	// Gives each situation its name and rules, and with `a_order` (Offered() as the user left it,
	// empty to keep it) the order, removed situations keeping their places
	// (SituationTable::Reordered); then writes the file once and gives the registry the order.
	// Returns why it refused, having changed nothing: a name rule broken
	// (SituationTable::CheckNames), a removed situation, rules for the Default, an order that is
	// not Offered()'s situations, or a file it cannot write.
	std::optional<std::string> Apply(const std::vector<Edit>& a_edits, std::span<const Situations::Situation> a_order);

	// Makes a user's situation named `a_name`, with no rules (it never applies until it has
	// some), last in the order, writes the file, then adds it to the registry as `a_created`. Returns why it
	// refused, having changed nothing: a name rule broken, the registry full
	// (Situations::kMaxSituations, removed ones included until the game is closed), or a file
	// it cannot write.
	std::optional<std::string> Create(const std::string& a_name, Situations::Situation& a_created);

	// Makes a user's situation named `a_name` with the rules of `a_source` (its conditions and
	// Allow during combat; no outfit: rows are the co-save's), right after it in the order, so it
	// wins over it; writes the file, then adds it to the registry as `a_created`. Returns why it
	// refused, having changed nothing: as Create, or the Default or a removed situation as the
	// source.
	std::optional<std::string> Duplicate(Situations::Situation a_source, const std::string& a_name, Situations::Situation& a_created);

	// Removes the situation and writes the file; a user's situation leaves the file for good.
	// Its rows in this save are the caller's (Assignments::ForgetSituation, after this write
	// took). Returns why it refused, having changed nothing: the Default, a situation removed
	// already, or a file it cannot write.
	std::optional<std::string> Remove(Situations::Situation a_situation);

	// Every preset as installed: a removed one returns, a renamed or edited one gets its name
	// and rules back; the order and the user's situations stay as they are. Then writes the
	// file. Nobody's rows change. Returns why it refused, having changed nothing: a file it
	// cannot write.
	std::optional<std::string> RestorePresets();

	// What RestorePresets would change, one line per situation (SituationTable::RestoreChanges);
	// empty while every preset is as installed.
	std::vector<std::string> RestoreChanges();
}
