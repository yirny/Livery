// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// An outfit's icon (docs/design.md §9): a piece icon PNG, named by PieceIcons' key, tinted with
// one of kColors, named by its key. The tables set the order of the icon picker (Choose icon)
// and of the outfit lists' icon sort. Pure data and logic with no ImGui or SKSE, so the unit
// tests build it.
namespace OutfitIcons
{
	constexpr std::string_view kDefaultKey = "skyui/icons_item_psychosteve/armor_body";

	// The picker's icons in order: SkyUI's by slot (helmet, circlet, body, hands, feet,
	// necklace, ring, shield), each slot heavy, light, then clothing; Jelidity's cloak and
	// backpack; then SkyUI's other items: weapons, the schools of magic (in the game's order)
	// and magic effect, the elements, book, potion and misc; then BOOBIES' from head to feet.
	// Only those whose PNG is loaded show. Icons drawn the same as one listed are left out:
	// SkyUI's forearms and calves (its hands and feet), BOOBIES' torso, ring and circlet
	// (SkyUI's body, ring and circlet).
	constexpr auto kKeys = std::to_array<std::string_view>({
		"skyui/icons_item_psychosteve/armor_head",
		"skyui/icons_item_psychosteve/lightarmor_head",
		"skyui/icons_item_psychosteve/clothing_head",
		"skyui/icons_item_psychosteve/armor_circlet",
		"skyui/icons_item_psychosteve/armor_body",
		"skyui/icons_item_psychosteve/lightarmor_body",
		"skyui/icons_item_psychosteve/clothing_body",
		"skyui/icons_item_psychosteve/armor_hands",
		"skyui/icons_item_psychosteve/lightarmor_hands",
		"skyui/icons_item_psychosteve/clothing_hands",
		"skyui/icons_item_psychosteve/armor_feet",
		"skyui/icons_item_psychosteve/lightarmor_feet",
		"skyui/icons_item_psychosteve/clothing_feet",
		"skyui/icons_item_psychosteve/armor_amulet",
		"skyui/icons_item_psychosteve/armor_ring",
		"skyui/icons_item_psychosteve/armor_shield",
		"skyui/icons_item_psychosteve/lightarmor_shield",
		"skyui/icons_item_psychosteve/clothing_shield",
		"i4iconaddon/icons/clothing_cloak",
		"i4iconaddon/icons/clothing_backpack",
		"skyui/icons_item_psychosteve/weapon_sword",
		"skyui/icons_item_psychosteve/weapon_staff",
		"skyui/icons_item_psychosteve/weapon_bow",
		"skyui/icons_item_psychosteve/weapon_pickaxe",
		"skyui/icons_item_psychosteve/weapon_fishingrod",
		"skyui/icons_item_psychosteve/default_alteration",
		"skyui/icons_item_psychosteve/default_conjuration",
		"skyui/icons_item_psychosteve/default_destruction",
		"skyui/icons_item_psychosteve/default_illusion",
		"skyui/icons_item_psychosteve/default_restoration",
		"skyui/icons_item_psychosteve/default_effect",
		"skyui/icons_item_psychosteve/magic_fire",
		"skyui/icons_item_psychosteve/magic_frost",
		"skyui/icons_item_psychosteve/magic_shock",
		"skyui/icons_item_psychosteve/magic_sun",
		"skyui/icons_item_psychosteve/magic_wind",
		"skyui/icons_item_psychosteve/magic_water",
		"skyui/icons_item_psychosteve/magic_earth",
		"skyui/icons_item_psychosteve/default_book",
		"skyui/icons_item_psychosteve/default_potion",
		"skyui/icons_item_psychosteve/default_misc",
		"skyui/icons_item_psychosteve/misc_gem",
		"skyui/icons_item_psychosteve/misc_ingot",
		"skyui/icons_item_psychosteve/misc_dwarvenscrap",
		"skyui/icons_item_psychosteve/misc_remains",
		"skyui/icons_item_psychosteve/misc_tool",
		"skyui/icons_item_psychosteve/misc_toy",
		"skyui/icons_item_psychosteve/misc_instrument",
		"skyui/icons_item_psychosteve/misc_housepart",
		"skyui/icons_item_psychosteve/misc_camping",
		"boobies immersive icons/bodyparts/armor_hat",
		"boobies immersive icons/bodyparts/armor_horns",
		"boobies immersive icons/bodyparts/armor_ears",
		"boobies immersive icons/bodyparts/armor_dragonmask",
		"boobies immersive icons/bodyparts/armor_fullmask",
		"boobies immersive icons/bodyparts/armor_facemask",
		"boobies immersive icons/bodyparts/armor_eyemask",
		"boobies immersive icons/bodyparts/armor_earrings",
		"boobies immersive icons/bodyparts/armor_piercing",
		"boobies immersive icons/bodyparts/armor_choker",
		"boobies immersive icons/bodyparts/armor_scarf",
		"boobies immersive icons/bodyparts/armor_shoulder",
		"boobies immersive icons/bodyparts/armor_pauldron",
		"boobies immersive icons/bodyparts/armor_armlets",
		"boobies immersive icons/bodyparts/armor_bra",
		"boobies immersive icons/bodyparts/armor_abs",
		"boobies immersive icons/bodyparts/armor_tattoo",
		"boobies immersive icons/bodyparts/armor_belt",
		"boobies immersive icons/bodyparts/armor_bandolier",
		"boobies immersive icons/bodyparts/armor_pouch",
		"boobies immersive icons/bodyparts/armor_lantern",
		"boobies immersive icons/bodyparts/armor_tasset",
		"boobies immersive icons/bodyparts/armor_cuisse",
		"boobies immersive icons/bodyparts/armor_greaves",
		"boobies immersive icons/bodyparts/armor_skirt",
		"boobies immersive icons/bodyparts/armor_trousers",
		"boobies immersive icons/bodyparts/armor_panties",
		"boobies immersive icons/bodyparts/armor_underwears",
		"boobies immersive icons/bodyparts/armor_stockings",
		"boobies immersive icons/bodyparts/armor_tail",
		"boobies immersive icons/bodyparts/armor_rose",
		"boobies immersive icons/bodyparts/armor_daedricshield",
	});

	// A colour of the picker: the key outfits.json keeps, the name its tooltip shows, its tint.
	// An outfit keeps the key, so a tint changed here shows on every outfit that has it.
	struct Color
	{
		std::string_view key;   // ASCII lower case, never changed once shipped
		std::string_view name;  // shown to the player
		std::uint32_t    rgb;   // 0xRRGGBB
	};

	// The picker's colours in order, in three rows of ten: the colours, then each lighter
	// ("- Light") and greyer ("- Muted"). The leftmost of those two rows are Nordic silver
	// and Ash grey. Each row is white (grey below), then red, pink, purple, blue, cyan,
	// teal, green, yellow and gold.
	constexpr auto kColors = std::to_array<Color>({
		{ "bone-white", "Bone white", 0xEAEAEA },
		{ "snowberry-red", "Snowberry red", 0xB13847 },
		{ "sheogorath-pink", "Sheogorath pink", 0xC356A5 },
		{ "deathbell-purple", "Deathbell purple", 0x5F53C3 },
		{ "jazbay-blue", "Jazbay blue", 0x3E5CB1 },
		{ "coldharbour-cyan", "Coldharbour cyan", 0x59B1C9 },
		{ "glass-teal", "Glass teal", 0x51A49C },
		{ "spriggan-green", "Spriggan green", 0x9CD15F },
		{ "luna-yellow", "Luna yellow", 0xE0E356 },
		{ "dwarven-gold", "Dwarven gold", 0xD6962C },
		{ "bone-white-light", "Nordic silver", 0xAEAEAE },
		{ "snowberry-red-light", "Snowberry red - Light", 0xC4727E },
		{ "sheogorath-pink-light", "Sheogorath pink - Light", 0xD38FC2 },
		{ "deathbell-purple-light", "Deathbell purple - Light", 0x9288CF },
		{ "jazbay-blue-light", "Jazbay blue - Light", 0x6E84B6 },
		{ "coldharbour-cyan-light", "Coldharbour cyan - Light", 0x93CBD8 },
		{ "glass-teal-light", "Glass teal - Light", 0x8BC2BD },
		{ "spriggan-green-light", "Spriggan green - Light", 0xC2DF9C },
		{ "luna-yellow-light", "Luna yellow - Light", 0xEAED96 },
		{ "dwarven-gold-light", "Dwarven gold - Light", 0xE1BC6C },
		{ "bone-white-muted", "Ash grey", 0x777777 },
		{ "snowberry-red-muted", "Snowberry red - Muted", 0x836368 },
		{ "sheogorath-pink-muted", "Sheogorath pink - Muted", 0x9D8096 },
		{ "deathbell-purple-muted", "Deathbell purple - Muted", 0x6F6C89 },
		{ "jazbay-blue-muted", "Jazbay blue - Muted", 0x596073 },
		{ "coldharbour-cyan-muted", "Coldharbour cyan - Muted", 0x90A7AD },
		{ "glass-teal-muted", "Glass teal - Muted", 0x859B98 },
		{ "spriggan-green-muted", "Spriggan green - Muted", 0xB2C1A2 },
		{ "luna-yellow-muted", "Luna yellow - Muted", 0xD8D8B2 },
		{ "dwarven-gold-muted", "Dwarven gold - Muted", 0xB5A488 },
	});

	constexpr std::string_view kDefaultColor = kColors[0].key;

	struct Icon
	{
		std::string key{ kDefaultKey };      // PieceIcons' key, ASCII lower case; one with no PNG draws kDefaultKey's
		std::string color{ kDefaultColor };  // a kColors key; one not there draws kDefaultColor's

		bool operator==(const Icon&) const = default;
	};

	// Its place in kKeys; kKeys.size() for a key not there, which sorts last.
	inline std::size_t KeyOrder(std::string_view a_key)
	{
		return static_cast<std::size_t>(std::ranges::find(kKeys, a_key) - kKeys.begin());
	}

	// A piece a new outfit may take its icon from: its body slot mask (BipedObjectSlot bits,
	// slot 30 in bit 0) and its PieceIcons key ("" for none).
	struct PieceKey
	{
		std::uint32_t    slots = 0;
		std::string_view key;
	};

	// The icon key the Save page's Create tab starts a new outfit with: the key of the first
	// piece the picker could choose (kKeys lists it and `a_loaded(key)`, its PNG is loaded),
	// looking at the pieces on the body slot first, then head (head, hair, long hair), circlet,
	// hands, feet, amulet, ring, then any, each in `a_pieces`' order; kDefaultKey for none.
	template <class Loaded>
	std::string_view SuggestKey(const std::vector<PieceKey>& a_pieces, Loaded a_loaded)
	{
		constexpr std::uint32_t kAny = 0xFFFFFFFFu;
		constexpr auto          kSlotOrder = std::to_array<std::uint32_t>({
			1u << 2,                       // body (32)
			1u << 0 | 1u << 1 | 1u << 11,  // head (30), hair (31), long hair (41)
			1u << 12,                      // circlet (42)
			1u << 3,                       // hands (33)
			1u << 7,                       // feet (37)
			1u << 5,                       // amulet (35)
			1u << 6,                       // ring (36)
			kAny,                          // a piece on no slot too
		});

		for (const auto slots : kSlotOrder) {
			for (const auto& piece : a_pieces) {
				if ((slots == kAny || (piece.slots & slots) != 0) && KeyOrder(piece.key) < kKeys.size() && a_loaded(piece.key)) {
					return piece.key;
				}
			}
		}
		return kDefaultKey;
	}

	// The colour's place in kColors; kColors.size() for a key not there, which sorts last.
	inline std::size_t ColorOrder(std::string_view a_color)
	{
		return static_cast<std::size_t>(std::ranges::find(kColors, a_color, &Color::key) - kColors.begin());
	}

	// The colour the key names; kDefaultColor's for a key not there.
	inline const Color& ColorOf(std::string_view a_color)
	{
		const auto order = ColorOrder(a_color);
		return kColors[order < kColors.size() ? order : 0];
	}
}
