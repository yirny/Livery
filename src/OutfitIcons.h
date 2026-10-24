// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// An outfit's icon (docs/design.md §9): a piece icon PNG, named by PieceIcons' key, tinted with
// one of kColors. The tables set the order of the icon picker (Choose icon) and of the outfit
// lists' icon sort. Pure data and logic with no ImGui or SKSE, so the unit tests build it.
namespace OutfitIcons
{
	constexpr std::string_view kDefaultKey = "skyui/icons_item_psychosteve/armor_body";

	// The picker's icons in order: SkyUI's by slot (helmet, circlet, body, hands, feet,
	// necklace, ring, shield), each slot heavy, light, then clothing; Jelidity's cloak and
	// backpack; then BOOBIES' from head to feet. Only those whose PNG is loaded show. Icons
	// drawn the same as one listed are left out: SkyUI's forearms and calves (its hands and
	// feet), BOOBIES' torso, ring and circlet (SkyUI's body, ring and circlet).
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

	// The picker's colours in order, 0xRRGGBB: white, then pale tints of low saturation and
	// high lightness (HSL about 60% and 78%): blue violet, purple, pink, red, orange, yellow
	// green, green, blue green, blue.
	constexpr std::array<std::uint32_t, 10> kColors{ 0xEAEAEA, 0xAEA5E8, 0xC8A5E8, 0xE8A5CB, 0xE8A5A5, 0xE8C2A5, 0xC2E8A5, 0xA5E8B1, 0xA5E8E0,
		0xA5CDE8 };
	constexpr std::uint32_t                 kDefaultColor = kColors[0];

	struct Icon
	{
		std::string   key{ kDefaultKey };   // PieceIcons' key, ASCII lower case; one with no PNG draws kDefaultKey's
		std::uint32_t rgb = kDefaultColor;  // its tint, 0xRRGGBB

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

	// Its place in kColors; kColors.size() for a colour not there, which sorts last.
	inline std::size_t ColorOrder(std::uint32_t a_rgb)
	{
		return static_cast<std::size_t>(std::ranges::find(kColors, a_rgb) - kColors.begin());
	}

	// "#eaeaea", as outfits.json keeps it.
	inline std::string ToHex(std::uint32_t a_rgb)
	{
		return std::format("#{:06x}", a_rgb & 0xFFFFFF);
	}

	// "#rrggbb", hex digits of either case; nullopt for anything else.
	inline std::optional<std::uint32_t> FromHex(std::string_view a_text)
	{
		constexpr std::size_t kLength = 7;  // "#" and six digits

		if (a_text.size() != kLength || a_text[0] != '#') {
			return std::nullopt;
		}
		std::uint32_t rgb = 0;
		const auto [end, error] = std::from_chars(a_text.data() + 1, a_text.data() + kLength, rgb, 16);
		if (error != std::errc{} || end != a_text.data() + kLength) {
			return std::nullopt;
		}
		return rgb;
	}
}
