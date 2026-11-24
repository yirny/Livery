// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "OutfitIcons.h"
#include "PieceFile.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Livery's outfit library (docs/design.md §4): named sets of armor shared by every save,
// kept in Data/SKSE/Plugins/Livery/outfits.json (see Library.cpp). A piece is stored as
// the file name of the plugin that defines it and its FormID within that plugin
// (PieceFile::PieceRef), so the library survives load order changes; a piece whose plugin is
// not loaded stays in it.
//
// Contract: every function runs on the game thread (SKSE task); the capture window never
// calls here. Load runs once at kDataLoaded, before any other function.
namespace Library
{
	using PieceRef = PieceFile::PieceRef;

	struct Outfit
	{
		std::uint32_t         id = 0;  // unique and never reused
		std::string           name;    // unique, ASCII case ignored
		std::vector<PieceRef> pieces;  // never empty
		OutfitIcons::Icon     icon;    // the default one for a file that names none
		// Worn over the outfit beneath instead of in its place (docs/design.md §7); off for a
		// file that names none.
		bool layer = false;
		// A layer only: the outfit beneath keeps its items where they share a body slot with
		// this one's, which are left off (OutfitRows::LayerOver). Off for a file that names none.
		bool keepBeneath = false;
	};

	// Reads the file. A missing or empty file (nothing but whitespace) is an empty library. A
	// file that cannot be read, or that a newer version wrote, leaves the library empty and
	// refuses every change, so the file is never overwritten. The file is not written here.
	void Load();

	// Contract: pointers and references into the library go stale at the next Create, Update
	// or Remove.
	const std::vector<Outfit>& Outfits();
	const Outfit*              Find(std::uint32_t a_id);
	const Outfit*              FindByName(std::string_view a_name);  // ASCII case ignored

	// The outfit's armor that is loaded, each form once. `a_unloaded`, when given, gets the
	// number of pieces whose plugin or form is not loaded.
	std::vector<RE::TESObjectARMO*> LoadedPieces(const Outfit& a_outfit, std::size_t* a_unloaded = nullptr);

	// Whether any of the outfit's pieces is loaded (LoadedPieces would not be empty), without
	// building the list.
	bool HasLoadedPiece(const Outfit& a_outfit);

	// Adds an outfit with icon `a_icon`, a layer with `a_layer` that keeps the items beneath with
	// `a_keepBeneath` (ignored for no layer), and writes the file. Returns why it refused, having
	// changed nothing: an empty or taken name, no armor, armor created in game, or a file it
	// cannot write.
	std::optional<std::string> Create(std::string_view a_name, const std::vector<RE::TESObjectARMO*>& a_armor, const OutfitIcons::Icon& a_icon, bool a_layer,
		bool a_keepBeneath);

	// Replaces the outfit's loaded pieces with `a_armor` (pieces whose plugin or form is not
	// loaded stay), its name, its icon and its layer flags (`a_keepBeneath` ignored for no layer),
	// then writes the file. Refuses as Create does, having changed nothing; a name is taken when
	// another outfit has it.
	std::optional<std::string> Update(std::uint32_t a_id, std::string_view a_name, const std::vector<RE::TESObjectARMO*>& a_armor, const OutfitIcons::Icon& a_icon,
		bool a_layer, bool a_keepBeneath);

	// Takes the outfit out and writes the file; its id is never given again. Returns why it
	// refused, having changed nothing: the outfit is gone already, or a file it cannot write.
	std::optional<std::string> Remove(std::uint32_t a_id);
}
