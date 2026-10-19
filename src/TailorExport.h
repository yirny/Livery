// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Library.h"

#include <vector>

// Writes the outfit library as Tailor 3.0's import file, Data/SKSE/Plugins/Livery/tailor-export.json,
// which the player copies into Data/SKSE/Plugins/Tailor to import it there (docs/design.md §4).
// Self-contained: removing it takes this file, TailorExport.cpp, its CMake line and the calls in
// Library.cpp, and leaves outfits.json untouched.
namespace TailorExport
{
	// Replaces the file with these outfits. Outfits without a Tailor category are left out, as
	// Tailor's own export does. A failure is logged only.
	// Contract: game thread; called by Library after it read or wrote outfits.json.
	void Write(const std::vector<Library::Outfit>& a_outfits);
}
