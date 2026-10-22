// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The icon left of a piece's name in the capture window's piece list (docs/design.md §9). An
// icon is a white PNG in Data/SKSE/Plugins/Livery/icons/, at <iconSource without .swf>/<iconLabel>.png:
// named as SkyUI or an Inventory Interface Information Injector (I4) rule names the icon in its
// swf. A piece takes the icon SkyUI gives its armor, then each I4 Armor rule it matches overrides
// the icon, the colour or both, in the order I4 reads the rules (PieceIcons.cpp).
namespace PieceIcons
{
	// A PNG under the icons folder. Its key is its path from that folder without ".png", in
	// ASCII lower case with "/" between folders: "skyui/icons_item_psychosteve/armor_head".
	struct File
	{
		std::string           key;
		std::filesystem::path path;
	};

	// Every PNG under the icons folder. Any thread: it reads the folder only.
	std::vector<File> FindFiles();

	struct Icon
	{
		std::string   key;        // a File's key; "" when no PNG has the piece's icon
		std::uint32_t color = 0;  // the tint of the white icon, 0xRRGGBB
	};

	// Reads the icons folder and the Armor rules of the I4 files named after the active plugins.
	// Contract: game thread, once at kDataLoaded: the rule files go by the loaded plugins' names,
	// and their formId values resolve against the loaded plugins.
	void Load();

	// Contract: game thread, after Load; when the capture window lists pieces, never per frame
	// (it tests the armor against every rule).
	Icon Resolve(const RE::TESObjectARMO& a_armor);
}
