// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

// An armor's Type and Class as SkyUI's inventory shows them, the piece list's Type column and
// the library's Class categories. The rules are SkyUI 5's (schlangster/skyui on GitHub,
// src/ItemMenus/InventoryDataSetter.as: processArmorClass, processArmorPartMask,
// processArmorOther, processArmorBaseId; src/Common/skyui/defines/Armor.as), with SkyUI's
// strings. Header only: the functions run when the capture window opens, never per frame.
//
// Contract: game thread (reads forms and the data handler).
namespace ArmorInfo
{
	// SkyUI's weight classes in its order (WEIGHT_LIGHT, WEIGHT_HEAVY, WEIGHT_CLOTHING,
	// WEIGHT_JEWELRY), then the armor it gives none, which it shows as Other.
	enum class ArmorClass : std::uint8_t
	{
		kLight,
		kHeavy,
		kClothing,
		kJewelry,
		kOther
	};

	// The library stores the first three as its Armor Class categories; the Create tab's
	// suggested category skips the other two.
	constexpr std::array<const char*, 5> kClassNames{ "Light", "Heavy", "Clothing", "Jewelry", "Other" };

	constexpr const char* ClassName(ArmorClass a_class)
	{
		return kClassNames[static_cast<std::size_t>(a_class)];
	}

	struct PieceType
	{
		const char* name;   // SkyUI's subTypeDisplay; "" for an armor on unnamed slots only
		int         order;  // SkyUI's subType, which its Type column sorts by
	};

	namespace detail
	{
		using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;

		struct NamedSlot
		{
			Slot       slot;
			PieceType  type;
			ArmorClass otherClass;  // processArmorOther's class for an armor the game gives none
		};

		constexpr int kUnnamedOrder = 100;  // above SkyUI's EQUIP_ values (0 to 13), as its part-mask subType is

		// PARTMASK_PRECEDENCE's named slots, in its order: the first one an armor covers is its main slot.
		constexpr std::array<NamedSlot, 14> kPrecedence{ {
			{ Slot::kBody, { "Body", 3 }, ArmorClass::kClothing },
			{ Slot::kHair, { "Head", 1 }, ArmorClass::kClothing },
			{ Slot::kHands, { "Hands", 5 }, ArmorClass::kClothing },
			{ Slot::kForearms, { "Forearms", 4 }, ArmorClass::kClothing },
			{ Slot::kFeet, { "Feet", 8 }, ArmorClass::kClothing },
			{ Slot::kCalves, { "Calves", 7 }, ArmorClass::kClothing },
			{ Slot::kShield, { "Shield", 6 }, ArmorClass::kClothing },
			{ Slot::kAmulet, { "Amulet", 10 }, ArmorClass::kJewelry },
			{ Slot::kRing, { "Ring", 12 }, ArmorClass::kJewelry },
			{ Slot::kLongHair, { "Head", 2 }, ArmorClass::kClothing },
			{ Slot::kEars, { "Ears", 11 }, ArmorClass::kJewelry },
			{ Slot::kHead, { "Head", 0 }, ArmorClass::kClothing },
			{ Slot::kCirclet, { "Circlet", 9 }, ArmorClass::kJewelry },
			{ Slot::kTail, { "Tail", 13 }, ArmorClass::kClothing },
		} };

		// SkyUI's base-id exceptions, which it matches by the form id's low 24 bits.
		constexpr RE::FormID kWeddingWreath = 0x08895A;     // Skyrim.esm ClothesWeddingWreath: Jewelry
		constexpr RE::FormID kVampireLordArmor = 0x011A84;  // Dawnguard.esm DLC1ClothesVampireLordArmor: Body

		inline const NamedSlot* MainSlot(const RE::TESObjectARMO& a_armor)
		{
			for (const auto& named : kPrecedence) {
				if (a_armor.HasPartOf(named.slot)) {
					return &named;
				}
			}
			return nullptr;
		}

		inline bool IsForm(const RE::TESObjectARMO& a_armor, RE::FormID a_localID, std::string_view a_plugin)
		{
			auto* dataHandler = RE::TESDataHandler::GetSingleton();
			// LookupFormID returns 0 for a plugin that is not loaded, which matches no armor.
			return dataHandler && dataHandler->LookupFormID(a_localID, a_plugin) == a_armor.GetFormID();
		}
	}

	inline PieceType TypeOf(const RE::TESObjectARMO& a_armor)
	{
		if (detail::IsForm(a_armor, detail::kVampireLordArmor, "Dawnguard.esm")) {
			return { "Body", 3 };
		}
		if (const auto* main = detail::MainSlot(a_armor)) {
			return main->type;
		}
		// Unnamed slots follow PARTMASK_PRECEDENCE's order, which is theirs by bit.
		const auto mask = a_armor.GetSlotMask().underlying();
		return { "", detail::kUnnamedOrder + std::countr_zero(static_cast<std::uint32_t>(mask)) };
	}

	inline ArmorClass ClassOf(const RE::TESObjectARMO& a_armor)
	{
		using Type = RE::BGSBipedObjectForm::ArmorType;
		if (detail::IsForm(a_armor, detail::kWeddingWreath, "Skyrim.esm")) {
			return ArmorClass::kJewelry;
		}
		switch (a_armor.GetArmorType()) {
		case Type::kLightArmor:
			return ArmorClass::kLight;
		case Type::kHeavyArmor:
			return ArmorClass::kHeavy;
		default:
			break;
		}
		if (a_armor.HasKeywordString("VendorItemClothing")) {
			return ArmorClass::kClothing;
		}
		if (a_armor.HasKeywordString("VendorItemJewelry")) {
			return ArmorClass::kJewelry;
		}
		const auto* main = detail::MainSlot(a_armor);
		return main ? main->otherClass : ArmorClass::kOther;
	}
}
