// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants  - the icon and rule folders, SkyUI's icon swf and armor icon colours
//   Keys       - an icon's key from a rule's iconSource and iconLabel
//   Rules      - an I4 file's text as I4 accepts it (comments, a comma before a closing bracket);
//                an I4 Armor rule: reading its match (formType, keywords, formId, parts) and
//                assign (iconSource, iconLabel, iconColor, subTypeDisplay), and testing an
//                armor against it
//   Type text  - a rule's subTypeDisplay as the game's translations show it
//   SkyUI      - the icon label and colour SkyUI gives an armor
//   Magic      - the icon label and colour SkyUI gives a spell, scroll, potion or ingredient
//   PieceIcons - FindFiles / Load / Resolve / ResolveMagic
//
// Inventory Interface Information Injector (I4; Exit-9B/InventoryInjector on GitHub,
// src/Data/CustomDataManager.cpp, Config/Rule.cpp, Config/Property.cpp, Config/PropertyParser.cpp)
// reads SKSE/Plugins/InventoryInjector/<plugin>.json for each active plugin, in load order, and
// every rule an item matches sets the fields it assigns, so a later rule wins field by field.
// Livery reads the files the same way but keeps only the rules for formType "Armor" whose match
// uses keywords, formId and parts alone. It reads each of those as I4's documentation states:
// a value, or {"anyOf": [...]} met by any of its values, or an array of these that must all be
// met (I4 itself keeps only an array's last one). A rule's icon counts only when its PNG exists,
// as I4 drops a rule's icon when its swf is missing. A rule's subTypeDisplay counts only when it
// has no "$" or the game has a translation for it, so a piece never shows a raw "$" key.
#include "PCH.h"

#include "PieceIcons.h"

#include "ArmorInfo.h"
#include "Utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace
{
	constexpr auto kIconDir = L"Data/SKSE/Plugins/Livery/icons";
	constexpr auto kRuleDir = L"Data/SKSE/Plugins/InventoryInjector";

	// The swf SkyUI draws item icons from, which a rule without iconSource names (I4's Rule::Validate).
	constexpr std::string_view kSkyUISource = "skyui/icons_item_psychosteve.swf";

	// SkyUI 5's armor icon colours (InventoryIconSetter.as, processArmorIcon), 0xRRGGBB; I4's
	// Skyrim.json assigns the same by weight class.
	constexpr std::uint32_t kArmorColor = 0xEDDA87;
	constexpr std::uint32_t kLightArmorColor = 0x756000;
	constexpr std::uint32_t kHeavyArmorColor = 0x6B7585;

	// SkyUI 5's icon colours for magic (MagicIconSetter.as, InventoryIconSetter.as), 0xRRGGBB;
	// I4's Skyrim.json assigns the same.
	constexpr std::uint32_t kFireColor = 0xC73636;
	constexpr std::uint32_t kFrostColor = 0x1FFBFF;
	constexpr std::uint32_t kShockColor = 0xEAAB00;        // a spell's or a potion's
	constexpr std::uint32_t kScrollShockColor = 0xFFFF00;  // a scroll's
	constexpr std::uint32_t kPoisonColor = 0xAD00B3;
	constexpr std::uint32_t kHealthColor = 0xDB2E73;
	constexpr std::uint32_t kMagickaColor = 0x2E9FDB;
	constexpr std::uint32_t kStaminaColor = 0x51DB2E;

	// An I4 rule as Livery uses it. Each inner list is a condition met by any of its values;
	// every condition must be met.
	struct Rule
	{
		std::vector<std::vector<std::string>>   keywords;  // keyword editor IDs
		std::vector<std::vector<RE::FormID>>    formIds;   // 0 for a form whose plugin is not loaded
		std::vector<std::vector<std::uint32_t>> parts;     // biped slot bits
		std::string                             key;       // the icon's key; "" when the rule sets none or no PNG has it
		std::optional<std::uint32_t>            color;     // 0xRRGGBB
		std::string                             typeName;  // subTypeDisplay as written, a "$" key or the text; "" for none
		// TypeText of typeName, read at the first piece the rule matches; "" for none.
		std::optional<std::string> typeText;
	};

	// Game thread only; written by Load.
	std::vector<Rule>               g_rules;
	std::unordered_set<std::string> g_keys;  // of the PNGs found

	// --- Keys -----------------------------------------------------------------

	void ToLowerAscii(std::string& a_text)
	{
		std::ranges::transform(a_text, a_text.begin(),
			[](char a_c) { return static_cast<char>(ExampleMod::AsciiToLower(static_cast<unsigned char>(a_c))); });
	}

	// "<iconSource without .swf>/<iconLabel>", with "/" between folders, in ASCII lower case.
	std::string KeyOf(std::string_view a_source, std::string_view a_label)
	{
		std::string key{ a_source };
		std::ranges::replace(key, '\\', '/');
		ToLowerAscii(key);
		if (key.ends_with(".swf")) {
			key.resize(key.size() - 4);
		}
		key += '/';
		key += a_label;
		ToLowerAscii(key);
		return key;
	}

	// --- Rules ----------------------------------------------------------------

	template <class T>
	using ValueReader = std::optional<T> (*)(const nlohmann::json&);

	std::optional<std::string> ReadKeyword(const nlohmann::json& a_value)
	{
		if (!a_value.is_string()) {
			return std::nullopt;
		}
		return a_value.get<std::string>();
	}

	// Hexadecimal digits, with or without "0x", as I4 reads them (std::hex).
	std::optional<std::uint32_t> ReadHex(std::string_view a_text)
	{
		if (a_text.starts_with("0x") || a_text.starts_with("0X")) {
			a_text.remove_prefix(2);
		}
		std::uint32_t value = 0;
		const auto*   last = a_text.data() + a_text.size();
		if (const auto [end, error] = std::from_chars(a_text.data(), last, value, 16); error != std::errc{} || end != last) {
			return std::nullopt;
		}
		return value;
	}

	// "Skyrim.esm|12E47": a plugin's file name and the form's id in it.
	std::optional<RE::FormID> ReadFormId(const nlohmann::json& a_value)
	{
		if (!a_value.is_string()) {
			return std::nullopt;
		}
		const std::string_view       text = a_value.get_ref<const std::string&>();
		const auto                   bar = text.find('|');
		std::optional<std::uint32_t> localID;
		if (bar != std::string_view::npos) {
			localID = ReadHex(text.substr(bar + 1));
		}
		if (!localID) {
			return std::nullopt;
		}
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		// LookupFormID returns 0 for a plugin that is not loaded, which matches no armor.
		return dataHandler ? dataHandler->LookupFormID(*localID, text.substr(0, bar)) : RE::FormID{ 0 };
	}

	// A biped slot's number, 30 to 61, as its bit in a slot mask.
	std::optional<std::uint32_t> ReadPart(const nlohmann::json& a_value)
	{
		constexpr std::int64_t kFirstSlot = 30;
		constexpr std::int64_t kLastSlot = 61;

		if (!a_value.is_number_integer()) {
			return std::nullopt;
		}
		const auto slot = a_value.get<std::int64_t>();
		if (slot < kFirstSlot || slot > kLastSlot) {
			return std::nullopt;
		}
		return 1u << (slot - kFirstSlot);
	}

	// "#RRGGBB" (the "#" optional) or a number, as I4's ParseUtil::ParseColor reads it.
	std::optional<std::uint32_t> ReadColor(const nlohmann::json& a_value)
	{
		constexpr std::uint32_t kRgbMask = 0xFFFFFF;

		if (a_value.is_number_unsigned()) {
			return a_value.get<std::uint32_t>() & kRgbMask;
		}
		if (!a_value.is_string()) {
			return std::nullopt;
		}
		std::string_view text = a_value.get_ref<const std::string&>();
		if (text.starts_with('#')) {
			text.remove_prefix(1);
		}
		const auto color = ReadHex(text);
		return color ? std::optional(*color & kRgbMask) : std::nullopt;
	}

	// One condition: a value, or {"anyOf": [values]}, met by any of them. False when Livery
	// cannot read it, which drops the rule.
	template <class T>
	bool ReadCondition(const nlohmann::json& a_spec, ValueReader<T> a_read, std::vector<std::vector<T>>& a_conditions)
	{
		std::vector<T> values;
		if (a_spec.is_object()) {
			const auto anyOf = a_spec.find("anyOf");
			if (a_spec.size() != 1 || anyOf == a_spec.end() || !anyOf->is_array()) {
				return false;
			}
			for (const auto& value : *anyOf) {
				auto read = a_read(value);
				if (!read) {
					return false;
				}
				values.push_back(std::move(*read));
			}
		} else if (auto read = a_read(a_spec)) {
			values.push_back(std::move(*read));
		}
		if (values.empty()) {
			return false;
		}
		a_conditions.push_back(std::move(values));
		return true;
	}

	// A match property: one condition, or an array of conditions that must all be met.
	template <class T>
	bool ReadProperty(const nlohmann::json& a_spec, ValueReader<T> a_read, std::vector<std::vector<T>>& a_conditions)
	{
		if (!a_spec.is_array()) {
			return ReadCondition(a_spec, a_read, a_conditions);
		}
		for (const auto& condition : a_spec) {
			if (!ReadCondition(condition, a_read, a_conditions)) {
				return false;
			}
		}
		return true;
	}

	// A rule for formType "Armor" that Livery can test and that assigns an icon, a colour or a
	// Type text.
	std::optional<Rule> ReadRule(const nlohmann::json& a_rule)
	{
		if (!a_rule.is_object()) {
			return std::nullopt;
		}
		const auto match = a_rule.find("match");
		const auto assign = a_rule.find("assign");
		if (match == a_rule.end() || assign == a_rule.end() || !match->is_object() || !assign->is_object()) {
			return std::nullopt;
		}
		Rule rule;
		bool armor = false;
		for (const auto& item : match->items()) {
			const auto& name = item.key();
			if (name.starts_with('$')) {  // I4 skips these too
				continue;
			}
			const auto& spec = item.value();
			bool        read = false;
			if (name == "formType") {
				read = armor = spec.is_string() && spec.get_ref<const std::string&>() == "Armor";
			} else if (name == "keywords") {
				read = ReadProperty(spec, ReadKeyword, rule.keywords);
			} else if (name == "formId") {
				read = ReadProperty(spec, ReadFormId, rule.formIds);
			} else if (name == "parts") {
				read = ReadProperty(spec, ReadPart, rule.parts);
			}
			if (!read) {
				return std::nullopt;
			}
		}
		if (!armor) {
			return std::nullopt;
		}
		// No iconSource is SkyUI's swf; one that is not a string drops the icon, as in I4.
		const auto label = assign->find("iconLabel");
		const auto source = assign->find("iconSource");
		if (label != assign->end() && label->is_string()) {
			if (source == assign->end()) {
				rule.key = KeyOf(kSkyUISource, label->get_ref<const std::string&>());
			} else if (source->is_string()) {
				rule.key = KeyOf(source->get_ref<const std::string&>(), label->get_ref<const std::string&>());
			}
		}
		if (const auto color = assign->find("iconColor"); color != assign->end()) {
			rule.color = ReadColor(*color);
		}
		if (const auto type = assign->find("subTypeDisplay"); type != assign->end() && type->is_string()) {
			rule.typeName = type->get<std::string>();
		}
		if (rule.key.empty() && !rule.color && rule.typeName.empty()) {
			return std::nullopt;
		}
		return rule;
	}

	// The first position at or after a_pos that is neither whitespace nor in a comment.
	std::size_t SkipBlank(std::string_view a_text, std::size_t a_pos)
	{
		constexpr std::string_view kBlank = " \t\r\n";
		while (a_pos < a_text.size()) {
			if (kBlank.find(a_text[a_pos]) != std::string_view::npos) {
				++a_pos;
			} else if (a_text.substr(a_pos, 2) == "//") {
				a_pos = (std::min)(a_text.find('\n', a_pos), a_text.size());
			} else if (a_text.substr(a_pos, 2) == "/*") {
				const auto end = a_text.find("*/", a_pos + 2);
				a_pos = end == std::string_view::npos ? a_text.size() : end + 2;
			} else {
				break;
			}
		}
		return a_pos;
	}

	// a_text without each comma that a closing bracket follows, past whitespace and comments;
	// strings and comments are kept as they are.
	std::string DropTrailingCommas(std::string_view a_text)
	{
		std::string result;
		result.reserve(a_text.size());
		std::size_t pos = 0;
		while (pos < a_text.size()) {
			if (const auto next = SkipBlank(a_text, pos); next > pos) {
				result.append(a_text.substr(pos, next - pos));
				pos = next;
			} else if (a_text[pos] == '"') {
				auto end = pos + 1;
				while (end < a_text.size() && a_text[end] != '"') {
					end += a_text[end] == '\\' ? 2 : 1;
				}
				end = (std::min)(end + 1, a_text.size());
				result.append(a_text.substr(pos, end - pos));
				pos = end;
			} else if (a_text[pos] == ',') {
				const auto after = SkipBlank(a_text, pos + 1);
				if (after == a_text.size() || (a_text[after] != ']' && a_text[after] != '}')) {
					result.push_back(',');
				}
				++pos;
			} else {
				result.push_back(a_text[pos++]);
			}
		}
		return result;
	}

	// Adds the usable rules of a plugin's I4 file, if it has one; returns how many.
	std::size_t ReadRuleFile(std::string_view a_plugin)
	{
		auto path = std::filesystem::current_path() / kRuleDir / std::filesystem::path(a_plugin);
		path.replace_extension(L".json");
		std::ifstream file{ path, std::ios::binary };
		if (!file) {
			return 0;
		}
		// I4 reads files with comments and with a comma before a closing bracket (its log counts
		// their rules), so Livery takes both too.
		const std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
		const auto        json = nlohmann::json::parse(DropTrailingCommas(text), nullptr, false, true);
		if (json.is_discarded()) {
			logger::warn("The InventoryInjector file for {} is not valid JSON; Livery uses none of its icon rules", a_plugin);
			return 0;
		}
		const auto rules = json.find("rules");
		if (rules == json.end() || !rules->is_array()) {
			return 0;
		}
		std::size_t count = 0;
		for (const auto& entry : *rules) {
			if (auto rule = ReadRule(entry)) {
				g_rules.push_back(std::move(*rule));
				++count;
			}
		}
		return count;
	}

	// Every condition has a value `a_has` holds.
	template <class T, class Has>
	bool MeetsAll(const std::vector<std::vector<T>>& a_conditions, Has a_has)
	{
		return std::ranges::all_of(a_conditions, [&](const std::vector<T>& a_values) { return std::ranges::any_of(a_values, a_has); });
	}

	bool Matches(const Rule& a_rule, const RE::TESObjectARMO& a_armor)
	{
		const auto slots = static_cast<std::uint32_t>(a_armor.GetSlotMask().underlying());
		const auto formID = a_armor.GetFormID();
		return MeetsAll(a_rule.parts, [&](std::uint32_t a_bit) { return (slots & a_bit) != 0; }) &&
		       MeetsAll(a_rule.formIds, [&](RE::FormID a_id) { return a_id == formID; }) &&
		       MeetsAll(a_rule.keywords, [&](const std::string& a_keyword) { return a_armor.HasKeywordString(a_keyword); });
	}

	// --- Type text ------------------------------------------------------------

	// A rule's subTypeDisplay as SkyUI's Type column shows it: a "$" key through the game's
	// translations (Interface/Translations/<name>_<sLanguage>.txt), other text as written.
	// None for no text or a key with no translation.
	// Contract: game thread; reads the Scaleform translator, so only once the game has made it
	// (the capture window's listing, never Load).
	std::optional<std::string> TypeText(const std::string& a_typeName)
	{
		if (a_typeName.empty()) {
			return std::nullopt;
		}
		if (!a_typeName.starts_with('$')) {
			return a_typeName;
		}
		std::string text;
		if (!SKSE::Translation::Translate(a_typeName, text) || text.empty()) {
			return std::nullopt;
		}
		return text;
	}

	// --- SkyUI ----------------------------------------------------------------

	// SkyUI's EQUIP_ values (src/Common/skyui/defines/Armor.as; kBackpack and kCloak from
	// SkyUI 6), the subType ArmorInfo::TypeOf gives as its order.
	namespace Equip
	{
		enum : int
		{
			kHead,
			kHair,
			kLongHair,
			kBody,
			kForearms,
			kHands,
			kShield,
			kCalves,
			kFeet,
			kCirclet,
			kAmulet,
			kEars,
			kRing,
			kTail,
			kBackpack,
			kCloak
		};
	}

	// The slot part of SkyUI's light armor, heavy armor and clothing labels; "" for none.
	std::string_view SlotPart(int a_subType)
	{
		switch (a_subType) {
		case Equip::kHead:
		case Equip::kHair:
		case Equip::kLongHair:
			return "head";
		case Equip::kBody:
		case Equip::kTail:
			return "body";
		case Equip::kHands:
			return "hands";
		case Equip::kForearms:
			return "forearms";
		case Equip::kFeet:
			return "feet";
		case Equip::kCalves:
			return "calves";
		case Equip::kShield:
			return "shield";
		default:
			return {};
		}
	}

	// SkyUI's jewelry labels (processJewelryArmorIcon); "" for none.
	std::string_view JewelryLabel(int a_subType)
	{
		switch (a_subType) {
		case Equip::kAmulet:
			return "armor_amulet";
		case Equip::kRing:
			return "armor_ring";
		case Equip::kCirclet:
			return "armor_circlet";
		default:
			return {};
		}
	}

	// The icon label and colour SkyUI 5 gives an armor (schlangster/skyui on GitHub,
	// src/ItemMenus/InventoryIconSetter.as: processArmorIcon and the functions it calls), and
	// SkyUI 6's cloak and backpack labels.
	std::pair<std::string, std::uint32_t> SkyUIIcon(const RE::TESObjectARMO& a_armor)
	{
		using ArmorInfo::ArmorClass;
		const int     subType = ArmorInfo::TypeOf(a_armor).order;
		const auto    part = SlotPart(subType);
		const auto    jewelry = JewelryLabel(subType);
		std::string   label;
		std::uint32_t color = kArmorColor;
		switch (ArmorInfo::ClassOf(a_armor)) {
		case ArmorClass::kLight:
			color = kLightArmorColor;
			label = part.empty() ? std::string(jewelry) : std::format("lightarmor_{}", part);
			break;
		case ArmorClass::kHeavy:
			color = kHeavyArmorColor;
			label = part.empty() ? std::string(jewelry) : std::format("armor_{}", part);
			break;
		case ArmorClass::kJewelry:
			label = jewelry;
			break;
		default:  // Clothing, and an armor SkyUI gives no class
			if (!part.empty()) {
				label = std::format("clothing_{}", part);
			}
			break;
		}
		// SkyUI 6 labels a cloak or a backpack before it looks at the class. The colour stays
		// the class's, as I4's Skyrim.json sets it for every armor.
		if (subType == Equip::kCloak) {
			label = "clothing_cloak";
		} else if (subType == Equip::kBackpack) {
			label = "clothing_backpack";
		}
		return { label.empty() ? std::string("default_armor") : std::move(label), color };
	}

	// --- Magic ----------------------------------------------------------------
	//
	// SkyUI reads an item's school, resistance and actor value from the entry SKSE extends
	// (ianpatt/skse64 on GitHub, ScaleformExtendedData.cpp, MagicItemData): those of the base
	// effect of the item's costliest effect.

	// An icon label under SkyUI's swf and its colour, none where SkyUI sets none.
	using MagicLook = std::pair<std::string_view, std::optional<std::uint32_t>>;

	// The icon SkyUI's Magic menu gives a spell (MagicIconSetter.as, processSpellIcon): by its
	// school, a Destruction spell by what resists it.
	MagicLook SpellLook(const RE::EffectSetting* a_effect)
	{
		using AV = RE::ActorValue;
		switch (a_effect ? a_effect->data.associatedSkill : AV::kNone) {
		case AV::kAlteration:
			return { "default_alteration", std::nullopt };
		case AV::kConjuration:
			return { "default_conjuration", std::nullopt };
		case AV::kDestruction:
			switch (a_effect->data.resistVariable) {
			case AV::kResistFire:
				return { "magic_fire", kFireColor };
			case AV::kResistShock:
				return { "magic_shock", kShockColor };
			case AV::kResistFrost:
				return { "magic_frost", kFrostColor };
			default:
				return { "default_destruction", std::nullopt };
			}
		case AV::kIllusion:
			return { "default_illusion", std::nullopt };
		case AV::kRestoration:
			return { "default_restoration", std::nullopt };
		default:
			return { "default_power", std::nullopt };
		}
	}

	// A scroll's colour (InventoryIconSetter.as, processResist): by what resists it.
	std::optional<std::uint32_t> ScrollColor(const RE::EffectSetting* a_effect)
	{
		using AV = RE::ActorValue;
		switch (a_effect ? a_effect->data.resistVariable : AV::kNone) {
		case AV::kResistFire:
			return kFireColor;
		case AV::kResistShock:
			return kScrollShockColor;
		case AV::kResistFrost:
			return kFrostColor;
		default:
			return std::nullopt;
		}
	}

	// The icon SkyUI's inventory gives a potion (InventoryDataSetter.as, processPotionType, then
	// InventoryIconSetter.as, processPotionIcon): food first, a drink being food with the
	// potion's use sound, then poison, then by the actor value it changes.
	MagicLook PotionLook(const RE::AlchemyItem& a_potion, const RE::EffectSetting* a_effect)
	{
		using AV = RE::ActorValue;
		using Flag = RE::AlchemyItem::AlchemyFlag;
		constexpr RE::FormID kPotionUseSound = 0x000B6435;  // ITMPotionUse in Skyrim.esm (SkyUI's Form.as)

		const auto& data = a_potion.data;
		if (data.flags.any(Flag::kFoodItem)) {
			const bool drink = data.consumptionSound && data.consumptionSound->GetFormID() == kPotionUseSound;
			return { drink ? "food_wine" : "default_food", std::nullopt };
		}
		if (data.flags.any(Flag::kPoison)) {
			return { "potion_poison", kPoisonColor };
		}
		switch (a_effect ? a_effect->data.primaryAV : AV::kNone) {
		case AV::kHealth:
		case AV::kHealRate:
		case AV::kHealRateMult:
			return { "potion_health", kHealthColor };
		case AV::kMagicka:
		case AV::kMagickaRate:
		case AV::kMagickaRateMult:
			return { "potion_magic", kMagickaColor };
		case AV::kStamina:
		case AV::kStaminaRate:
		case AV::kStaminaRateMult:
			return { "potion_stam", kStaminaColor };
		case AV::kResistFire:
			return { "potion_fire", kFireColor };
		case AV::kResistShock:
			return { "potion_shock", kShockColor };
		case AV::kResistFrost:
			return { "potion_frost", kFrostColor };
		default:
			return { "default_potion", std::nullopt };
		}
	}

	// SkyUI's icon for a potion, scroll or ingredient as in the inventory and for a spell or
	// power as in the Magic menu. That menu lists a shout whole and what else acts on the actor
	// as an active effect, so Livery gives a shout's word the shout's icon and an ability,
	// disease or the like the active effect's.
	MagicLook MagicLookOf(const RE::MagicItem& a_item)
	{
		using Type = RE::MagicSystem::SpellType;
		const auto* costliest = a_item.GetCostliestEffectItem();
		const auto* effect = costliest ? costliest->baseEffect : nullptr;
		if (const auto* potion = a_item.As<RE::AlchemyItem>()) {
			return PotionLook(*potion, effect);
		}
		if (a_item.Is(RE::FormType::Scroll)) {
			return { "default_scroll", ScrollColor(effect) };
		}
		if (a_item.Is(RE::FormType::Ingredient)) {
			return { "default_ingredient", std::nullopt };
		}
		switch (a_item.GetSpellType()) {
		case Type::kSpell:
			return SpellLook(effect);
		case Type::kPower:
		case Type::kLesserPower:
			return { "default_power", std::nullopt };
		case Type::kVoicePower:
			return { "default_shout", std::nullopt };
		default:
			return { "default_effect", std::nullopt };
		}
	}
}

namespace PieceIcons
{
	std::vector<File> FindFiles()
	{
		std::vector<File> files;
		const auto        root = std::filesystem::current_path() / kIconDir;
		std::error_code   error;
		for (std::filesystem::recursive_directory_iterator it{ root, error }, end; !error && it != end; it.increment(error)) {
			std::error_code typeError;
			if (!it->is_regular_file(typeError)) {
				continue;
			}
			const auto  relative = it->path().lexically_relative(root).generic_u8string();
			std::string key(relative.begin(), relative.end());
			ToLowerAscii(key);
			if (!key.ends_with(".png")) {
				continue;
			}
			key.resize(key.size() - 4);
			files.push_back({ std::move(key), it->path() });
		}
		return files;
	}

	void Load()
	{
		g_rules.clear();
		g_keys.clear();
		for (auto& file : FindFiles()) {
			g_keys.insert(std::move(file.key));
		}
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			logger::error("No TESDataHandler; the piece list shows SkyUI's icons only");
			return;
		}
		std::size_t files = 0;
		for (const auto* plugin : dataHandler->files) {
			// Active plugins only, as I4 reads them.
			if (plugin && plugin->recordFlags.any(RE::TESFile::RecordFlag::kChecked) && ReadRuleFile(plugin->GetFilename()) > 0) {
				++files;
			}
		}
		for (auto& rule : g_rules) {
			if (!rule.key.empty() && !g_keys.contains(rule.key)) {
				rule.key.clear();
			}
		}
		std::erase_if(g_rules, [](const Rule& a_rule) { return a_rule.key.empty() && !a_rule.color && a_rule.typeName.empty(); });
		logger::info("Found {} piece icon(s); using {} armor icon rule(s) from {} InventoryInjector file(s)", g_keys.size(), g_rules.size(), files);
	}

	Icon Resolve(const RE::TESObjectARMO& a_armor)
	{
		auto [label, color] = SkyUIIcon(a_armor);
		auto key = KeyOf(kSkyUISource, label);
		if (!g_keys.contains(key)) {
			key.clear();
		}
		std::string typeName;
		for (auto& rule : g_rules) {
			if (!Matches(rule, a_armor)) {
				continue;
			}
			if (!rule.key.empty()) {
				key = rule.key;
			}
			if (rule.color) {
				color = *rule.color;
			}
			// The translation follows sLanguage, which a session never changes: once per rule.
			if (!rule.typeText) {
				rule.typeText = TypeText(rule.typeName).value_or(std::string{});
			}
			if (!rule.typeText->empty()) {
				typeName = *rule.typeText;
			}
		}
		return { std::move(key), color, std::move(typeName) };
	}

	MagicIcon ResolveMagic(const RE::MagicItem& a_item)
	{
		const auto [label, color] = MagicLookOf(a_item);
		auto key = KeyOf(kSkyUISource, label);
		if (!g_keys.contains(key)) {
			key.clear();
		}
		return { std::move(key), color };
	}
}
