// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "PieceFile.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Livery's chests (docs/chest-ui.md): named sets of items shared by every save, kept in
// Data/SKSE/Plugins/Livery/chests.json (see Chests.cpp) beside the outfit library. Unlike an
// outfit, a chest is never worn: it holds any number of items, several on one body slot too,
// which the window offers when dressing someone. A chest may be empty.
//
// Contract: every function runs on the game thread (SKSE task); the capture window never
// calls here. Load runs once at kDataLoaded, after Library::Load, before any other function.
namespace Chests
{
	using PieceRef = PieceFile::PieceRef;

	struct Chest
	{
		std::uint32_t         id = 0;  // unique and never reused
		std::string           name;    // unique among chests, ASCII case ignored
		std::vector<PieceRef> pieces;  // each form once; may be empty
	};

	// Reads the file. A missing or empty file (nothing but whitespace) is no chests. A file
	// that cannot be read, or that a newer version wrote, leaves the chests empty and refuses
	// every change, so the file is never overwritten. The file is not written here.
	void Load();

	// Contract: pointers and references into the list go stale at the next Create, Update or
	// Remove.
	const std::vector<Chest>& All();
	const Chest*              Find(std::uint32_t a_id);
	const Chest*              FindByName(std::string_view a_name);  // ASCII case ignored

	// The chest's armor that is loaded, each form once. `a_unloaded`, when given, gets the
	// number of pieces whose plugin or form is not loaded.
	std::vector<RE::TESObjectARMO*> LoadedPieces(const Chest& a_chest, std::size_t* a_unloaded = nullptr);

	// Adds an empty chest and writes the file. Returns why it refused, having changed nothing:
	// an empty or taken name, or a file it cannot write.
	std::optional<std::string> Create(std::string_view a_name);

	// Replaces the chest's loaded pieces with `a_armor` (pieces whose plugin or form is not
	// loaded stay) and its name, then writes the file. Refuses as Create does, or for armor
	// created in game, having changed nothing; a name is taken when another chest has it.
	std::optional<std::string> Update(std::uint32_t a_id, std::string_view a_name, const std::vector<RE::TESObjectARMO*>& a_armor);

	// Takes the chest out and writes the file; its id is never given again. Returns why it
	// refused, having changed nothing: the chest is gone already, or a file it cannot write.
	std::optional<std::string> Remove(std::uint32_t a_id);
}
