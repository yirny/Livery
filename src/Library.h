// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "OutfitIcons.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Livery's outfit library (docs/design.md §4): named sets of armor shared by every save,
// kept in Data/SKSE/Plugins/Livery/outfits.json (see Library.cpp). A piece is stored as
// the file name of the plugin that defines it and its FormID within that plugin, so the
// library survives load order changes; a piece whose plugin is not loaded stays in it.
//
// Contract: every function runs on the game thread (SKSE task); the capture window never
// calls here. Load runs once at kDataLoaded, before any other function.
namespace Library
{
	// One armor piece as the file stores it.
	struct PieceRef
	{
		std::string plugin;       // file name of the plugin that defines the armor
		RE::FormID  localID = 0;  // its FormID within that plugin (12 bits for a light plugin)
		std::string name;         // its name when saved, for people reading the file only
	};

	struct Outfit
	{
		std::uint32_t         id = 0;  // unique and never reused
		std::string           name;    // unique, ASCII case ignored
		std::vector<PieceRef> pieces;  // never empty
		OutfitIcons::Icon     icon;    // the default one for a file that names none
	};

	// Reads the file. A missing or empty file (nothing but whitespace) is an empty library. A
	// file that cannot be read, or that a newer version wrote, leaves the library empty and
	// refuses every change, so the file is never overwritten. An older file's outfit
	// categories and gender are read past; the file is not written here.
	void Load();

	// Contract: pointers and references into the library go stale at the next Create, Update
	// or Remove.
	const std::vector<Outfit>& Outfits();
	const Outfit*              Find(std::uint32_t a_id);
	const Outfit*              FindByName(std::string_view a_name);  // ASCII case ignored

	// The outfit's armor that is loaded, each form once. `a_unloaded`, when given, gets the
	// number of pieces whose plugin or form is not loaded.
	std::vector<RE::TESObjectARMO*> LoadedPieces(const Outfit& a_outfit, std::size_t* a_unloaded = nullptr);

	// Adds an outfit with the default icon and writes the file. Returns why it refused, having
	// changed nothing: an empty or taken name, no armor, armor created in game, or a file it
	// cannot write.
	std::optional<std::string> Create(std::string_view a_name, const std::vector<RE::TESObjectARMO*>& a_armor);

	// Replaces the outfit's loaded pieces with `a_armor` (pieces whose plugin or form is not
	// loaded stay), its name and its icon, then writes the file. Refuses as Create does, having
	// changed nothing; a name is taken when another outfit has it.
	std::optional<std::string> Update(std::uint32_t a_id, std::string_view a_name, const std::vector<RE::TESObjectARMO*>& a_armor, const OutfitIcons::Icon& a_icon);

	// Takes the outfit out and writes the file; its id is never given again. Returns why it
	// refused, having changed nothing: the outfit is gone already, or a file it cannot write.
	std::optional<std::string> Remove(std::uint32_t a_id);
}
