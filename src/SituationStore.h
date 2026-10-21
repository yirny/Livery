// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Situations.h"

#include <optional>
#include <string>
#include <vector>

// The situations' names, descriptions and removed flags (docs/situation-management-ui.md
// §2), shared by every save in Data/SKSE/Plugins/Livery/situations.json, which holds only
// the situations that differ from the presets (SituationTable.h). Logs name a situation by
// its key (Situations::Name); the window and the HUD by Name here.
//
// Contract: every function runs on the game thread (SKSE task); the capture window never
// calls here. Load runs once at kDataLoaded, before the co-save is read (Assignments drops
// the rows of removed situations there) and before any other function.
namespace SituationStore
{
	// Reads the file. A missing file means every preset as installed and is not written
	// here. A file that cannot be read, or that a newer version wrote, leaves every preset as
	// installed and refuses every change, so the file is never overwritten.
	void Load();

	// Contract: the reference goes stale at the next Apply, Remove or RestorePresets.
	const std::string& Name(Situations::Situation a_situation);
	const std::string& Description(Situations::Situation a_situation);

	// ", in Town" for the end of a sentence that says what is worn for it; "" for an empty
	// description.
	std::string Suffix(Situations::Situation a_situation);

	// A removed situation offers no row anywhere; Situations::Evaluate still reports it and
	// nothing wears it.
	bool IsRemoved(Situations::Situation a_situation);

	// The situations not removed, in Situations::kChoosable order: the window's rows after
	// the Default.
	std::vector<Situations::Situation> Offered();

	// A situation's name and description as the user leaves them.
	struct Edit
	{
		Situations::Situation situation = Situations::Situation::kDefault;
		std::string           name;
		std::string           description;
	};

	// Gives each situation its name and description, then writes the file once. Returns why
	// it refused, having changed nothing: a name rule broken (SituationTable::CheckNames), a
	// removed situation, or a file it cannot write.
	std::optional<std::string> Apply(const std::vector<Edit>& a_edits);

	// Removes the situation and writes the file; its rows in this save are the caller's
	// (Assignments::ForgetSituation, after this write took). Returns why it refused, having
	// changed nothing: the Default, a situation removed already, or a file it cannot write.
	std::optional<std::string> Remove(Situations::Situation a_situation);

	// Every situation as installed: a removed one returns, a renamed or redescribed one gets
	// its words back; then writes the file. Nobody's rows change. Returns why it refused,
	// having changed nothing: a file it cannot write.
	std::optional<std::string> RestorePresets();

	// What RestorePresets would change, one line per situation (SituationTable::RestoreChanges);
	// empty while every preset is as installed.
	std::vector<std::string> RestoreChanges();
}
