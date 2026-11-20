// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "PieceFile.h"
#include "Strings.h"
#include "Utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The conditions of a situation the user defines (docs/custom-situations.md §2, §3): its rows,
// what each kind offers, the Applies line, and the rows as situations.json keeps them. The rules
// here need no game, so the unit tests reach them; Situations::Evaluate tells whether a row
// holds for an actor. Every word shown is a text of LIVERY_STRINGS_CONDITIONS, read in the
// chosen language where it is shown; the file's keys and the reasons a file cannot be read
// stay English.
//
// A situation applies while every row holds; a row with values holds while any of its values
// does (a Place or Spell row while any of its forms does), `negated` inverts it, and a row with
// nothing selected never holds (NothingSelected). An Indoors row has `negated` alone: indoors,
// or negated outdoors.
namespace SituationRules
{
	enum class Kind : std::uint8_t
	{
		kState,
		kLocationType,
		kIndoors,
		kPlace,
		kWeather,
		kTimeOfDay,
		kSpell,
		kCrafting
	};

	// Whose state a row reads (Row::target): the character wearing the situation's outfit, or
	// the player whoever wears it. Only a kind that reads an actor's own state takes it
	// (Targetable).
	enum class Target : std::uint8_t
	{
		kWearer,
		kPlayer
	};

	// Location type's values, bits of Row::values.
	inline constexpr std::uint32_t kTown = 1u << 0;
	inline constexpr std::uint32_t kCastle = 1u << 1;
	inline constexpr std::uint32_t kPlayerHome = 1u << 2;
	inline constexpr std::uint32_t kDungeon = 1u << 3;
	// Weather's values, bits of Row::values.
	inline constexpr std::uint32_t kRain = 1u << 0;
	inline constexpr std::uint32_t kSnow = 1u << 1;
	inline constexpr std::uint32_t kWarmRegion = 1u << 2;  // the climates of RegionClimate
	inline constexpr std::uint32_t kCoolRegion = 1u << 3;
	inline constexpr std::uint32_t kFreezingRegion = 1u << 4;
	// State's values, bits of Row::values. Wading and Swimming never hold together.
	inline constexpr std::uint32_t kInCombat = 1u << 0;
	inline constexpr std::uint32_t kWeaponDrawn = 1u << 1;
	inline constexpr std::uint32_t kWading = 1u << 2;
	inline constexpr std::uint32_t kSwimming = 1u << 3;
	inline constexpr std::uint32_t kSleeping = 1u << 4;
	// Crafting's values, bits of Row::values: what the furniture the actor uses is for. Smithing
	// is the forge, anvil, workbench and grindstone.
	inline constexpr std::uint32_t kAlchemy = 1u << 0;
	inline constexpr std::uint32_t kCooking = 1u << 1;
	inline constexpr std::uint32_t kEnchanting = 1u << 2;
	inline constexpr std::uint32_t kSmithing = 1u << 3;
	inline constexpr std::uint32_t kSmelting = 1u << 4;
	inline constexpr std::uint32_t kTanning = 1u << 5;
	inline constexpr std::uint32_t kMining = 1u << 6;
	inline constexpr std::uint32_t kWoodcutting = 1u << 7;

	inline constexpr std::uint16_t kMinutesPerDay = 24 * 60;

	// A form a Place or Spell row lists, kept as a piece is (plugin and local FormID), so it
	// survives load order changes; one whose plugin is not loaded stays and holds nowhere.
	struct FormRef
	{
		PieceFile::PieceRef ref;
		// Its FormID in this game's load order, 0 when its plugin is not loaded: what the poll
		// compares. SituationStore sets it as it reads or saves the rules (PieceFile::FormIDOf);
		// not in the file, and not compared, being the same form.
		std::uint32_t formID = 0;
		// A spell's visible effects (named, not hidden in the menus; SameEffects): their FormIDs
		// in this game's load order, ascending, each once. Set with formID; empty for a place, a
		// spell not loaded, or one without a visible effect, which then holds by its own FormID.
		std::vector<std::uint32_t> effects;

		friend bool operator==(const FormRef& a_left, const FormRef& a_right) { return a_left.ref == a_right.ref; }
	};

	using Place = FormRef;  // a location or a cell
	using Spell = FormRef;  // a MagicItem: a spell, power, shout's word, potion, scroll or ingredient

	// The same form: the same local FormID of the same plugin (ASCII case ignored).
	inline bool SameForm(const PieceFile::PieceRef& a_left, const PieceFile::PieceRef& a_right)
	{
		return a_left.localID == a_right.localID && ExampleMod::CaseInsensitiveEqual(a_left.plugin, a_right.plugin);
	}

	// The form's name as saved; its plugin and FormID when it has none.
	inline std::string FormName(const FormRef& a_form)
	{
		return a_form.ref.name.empty() ? std::format("{} {:06X}", a_form.ref.plugin, a_form.ref.localID) : a_form.ref.name;
	}

	// Whether an item whose visible effects are `a_item` (FormIDs in any order, repeats allowed)
	// has exactly the effects `a_set` (FormRef::effects): an Active Effects row counts every
	// spell, scroll or potion with the same effects as the one listed. An empty set matches an
	// item without visible effects; the poll holds such a spell by its own FormID instead.
	template <class Ids>
	bool SameEffects(std::span<const std::uint32_t> a_set, Ids&& a_item)
	{
		for (const auto id : a_item) {
			if (!std::ranges::binary_search(a_set, id)) {
				return false;
			}
		}
		for (const auto id : a_set) {
			if (std::ranges::find(a_item, id) == std::ranges::end(a_item)) {
				return false;
			}
		}
		return true;
	}

	// A spell the Add spell popup offers: the spell, its visible effects resolved, and the lines
	// its hover lists them in ("- Armor - Stone (ArmorFFSelf25)", the editor ID only when the
	// game keeps one); and the icon before its name (PieceIcons::ResolveMagic).
	struct SpellChoice
	{
		Spell                        spell;
		std::string                  effectLines;
		std::string                  icon;       // PieceIcons' key; "" for none
		std::optional<std::uint32_t> iconColor;  // the icon's tint, 0xRRGGBB; none for the text colour
	};

	namespace Detail
	{
		inline std::weak_ordering CompareNames(std::string_view a_left, std::string_view a_right)
		{
			const auto compare = [](unsigned char a, unsigned char b) { return ExampleMod::AsciiToLower(a) <=> ExampleMod::AsciiToLower(b); };
			return std::lexicographical_compare_three_way(a_left.begin(), a_left.end(), a_right.begin(), a_right.end(), compare);
		}
	}

	// Sorts the choices by name (ASCII case ignored), and of those with the same name and the
	// same visible effects keeps the lowest FormID alone: an Active Effects row counts them all
	// the same (Stoneflesh is in Skyrim.esm twice, once for the left hand).
	inline void MergeSameSpells(std::vector<SpellChoice>& a_choices)
	{
		std::ranges::sort(a_choices, [](const SpellChoice& a_left, const SpellChoice& a_right) {
			const auto& left = a_left.spell;
			const auto& right = a_right.spell;
			if (const auto order = Detail::CompareNames(left.ref.name, right.ref.name); order != 0) {
				return order < 0;
			}
			if (left.effects != right.effects) {
				return left.effects < right.effects;
			}
			return left.formID < right.formID;
		});
		const auto repeated = std::ranges::unique(a_choices, [](const SpellChoice& a_left, const SpellChoice& a_right) {
			return Detail::CompareNames(a_left.spell.ref.name, a_right.spell.ref.name) == 0 && a_left.spell.effects == a_right.spell.effects;
		});
		a_choices.erase(repeated.begin(), repeated.end());
	}

	// What the Add place popup offers (docs/custom-situations.md §7), read as the window opens
	// (Situations::PlacesAround).
	struct PlaceChoices
	{
		std::optional<Place> location;  // the actor's current location; nullopt outside any
		std::vector<Place>   parents;   // the locations it is in, nearest first, below any hold
		std::optional<Place> cell;      // the cell they stand in

		// The choice at `a_index` in the order the popup shows them: the location, its parents,
		// the cell. nullptr past them.
		const Place* At(int a_index) const
		{
			if (a_index < 0) {
				return nullptr;
			}
			auto index = static_cast<std::size_t>(a_index);
			if (location) {
				if (index == 0) {
					return &*location;
				}
				--index;
			}
			if (index < parents.size()) {
				return &parents[index];
			}
			index -= parents.size();
			return index == 0 && cell ? &*cell : nullptr;
		}
	};

	// Of two locations in a row with the same name, the location and its parents keep the wider:
	// it holds wherever the narrower does, and the popup shows a name once. Solitude's Avenues, a
	// group of its houses without its streets, is named "Solitude" as the city is.
	inline void MergeSameNames(PlaceChoices& a_choices)
	{
		auto& parents = a_choices.parents;
		for (std::size_t i = 0; i + 1 < parents.size();) {
			if (parents[i].ref.name == parents[i + 1].ref.name) {
				parents.erase(parents.begin() + static_cast<std::ptrdiff_t>(i));
			} else {
				++i;
			}
		}
		if (a_choices.location && !parents.empty() && a_choices.location->ref.name == parents.front().ref.name) {
			a_choices.location = std::move(parents.front());
			parents.erase(parents.begin());
		}
	}

	struct Row
	{
		Kind               kind = Kind::kState;
		bool               negated = false;           // the row's `not`
		Target             target = Target::kWearer;  // a Targetable kind: whose state it reads; kWearer for the others
		std::uint32_t      values = 0;                // a kind with values (KindInfo::values): the bits of those ticked
		std::uint16_t      from = 20 * 60;            // Time of day: minutes after midnight, below kMinutesPerDay
		std::uint16_t      to = 6 * 60;
		std::vector<Place> places;  // Place: in the order added
		std::vector<Spell> spells;  // Spell: in the order added

		friend bool operator==(const Row&, const Row&) = default;
	};

	struct Rules
	{
		std::vector<Row> rows;
		bool             applyInCombat = false;  // as the user ticked it; AppliesInCombat is what counts

		friend bool operator==(const Rules&, const Rules&) = default;
	};

	// A value of a kind with a fixed set of them.
	struct Value
	{
		std::uint32_t                bit;
		const char*                  key;                // in situations.json
		Strings::Text                label;              // the editor's checkbox
		Strings::Text                phrase;             // in the Applies line, the clause's {0} ("a town"; State's "in combat", "swimming")
		std::optional<Strings::Text> tip;                // on its row in the editor's dropdown, while hovered
		bool                         ruleAbove = false;  // the editor's dropdown draws a rule above its row, ending a group of values
	};

	inline constexpr std::array<Value, 4> kLocationTypes{ {
		{ kTown, "town", Strings::Text::kValueTown, Strings::Text::kPhraseTown },
		{ kCastle, "castle", Strings::Text::kValueCastle, Strings::Text::kPhraseCastle },
		{ kPlayerHome, "playerHome", Strings::Text::kValuePlayerHome, Strings::Text::kPhrasePlayerHome },
		{ kDungeon, "dungeon", Strings::Text::kValueDungeon, Strings::Text::kPhraseDungeon },
	} };

	inline constexpr std::array<Value, 5> kWeathers{ {
		{ kRain, "rain", Strings::Text::kValueRain, Strings::Text::kPhraseRain },
		{ kSnow, "snow", Strings::Text::kValueSnow, Strings::Text::kPhraseSnow },
		{ kWarmRegion, "warmRegion", Strings::Text::kValueWarmRegion, Strings::Text::kPhraseWarmRegion, Strings::Text::kTipWarmRegion, true },  // the weather above, the climates below
		{ kCoolRegion, "coolRegion", Strings::Text::kValueCoolRegion, Strings::Text::kPhraseCoolRegion, Strings::Text::kTipCoolRegion },
		{ kFreezingRegion, "freezingRegion", Strings::Text::kValueFreezingRegion, Strings::Text::kPhraseFreezingRegion, Strings::Text::kTipFreezingRegion },
	} };

	// In combat's tip is the editor's while the situation applies in combat (AppliesInCombat); it
	// warns with another while it does not.
	inline constexpr std::array<Value, 5> kStates{ {
		{ kInCombat, "inCombat", Strings::Text::kValueInCombat, Strings::Text::kPhraseInCombat, Strings::Text::kTipInCombat },
		{ kWeaponDrawn, "weaponDrawn", Strings::Text::kValueWeaponDrawn, Strings::Text::kPhraseWeaponDrawn, Strings::Text::kTipWeaponDrawn },
		{ kWading, "wading", Strings::Text::kValueWading, Strings::Text::kPhraseWading, Strings::Text::kTipWading, true },  // fighting above, water below
		{ kSwimming, "swimming", Strings::Text::kValueSwimming, Strings::Text::kPhraseSwimming, Strings::Text::kTipSwimming },
		{ kSleeping, "sleeping", Strings::Text::kValueSleeping, Strings::Text::kPhraseSleeping, Strings::Text::kTipSleeping, true },
	} };

	inline constexpr std::array<Value, 8> kCraftings{ {
		{ kAlchemy, "alchemy", Strings::Text::kValueAlchemy, Strings::Text::kPhraseAlchemy },
		{ kCooking, "cooking", Strings::Text::kValueCooking, Strings::Text::kPhraseCooking, Strings::Text::kTipCooking },
		{ kEnchanting, "enchanting", Strings::Text::kValueEnchanting, Strings::Text::kPhraseEnchanting },
		{ kSmithing, "smithing", Strings::Text::kValueSmithing, Strings::Text::kPhraseSmithing, Strings::Text::kTipSmithing, true },  // the smith's below
		{ kSmelting, "smelting", Strings::Text::kValueSmelting, Strings::Text::kPhraseSmelting },
		{ kTanning, "tanning", Strings::Text::kValueTanning, Strings::Text::kPhraseTanning },
		{ kMining, "mining", Strings::Text::kValueMining, Strings::Text::kPhraseMining, std::nullopt, true },  // gathering below
		{ kWoodcutting, "woodcutting", Strings::Text::kValueWoodcutting, Strings::Text::kPhraseWoodcutting },
	} };

	// A kind's clauses in the Applies line (KindInfo::clauses), each a whole clause: read of the
	// wearer, that negated, read of the player (Target::kPlayer), and that negated. A kind that
	// is not targetable has the first two alone.
	struct Clauses
	{
		Strings::Text wearer;
		Strings::Text negated;
		Strings::Text you = wearer;
		Strings::Text youNegated = negated;
	};

	struct KindInfo
	{
		Kind                         kind;
		const char*                  key;         // in situations.json
		Strings::Text                label;       // the condition's heading in the editor, and its name in Add Condition...
		Strings::Text                tip;         // the heading's help; {0} names the actor
		Clauses                      clauses;     // the row in the Applies line; {0} the values, places or spells (JoinWithOr), Time of day's {0} and {1} its times
		std::span<const Value>       values;      // empty for a kind without
		bool                         targetable;  // the row may read the player instead of the wearer (Target)
		std::optional<Strings::Text> subject;     // the row's first words in the editor for a kind that is not targetable ("The weather")
		std::optional<Strings::Text> addTip;      // its row's tooltip in Add Condition...; none where the label says it all
	};

	// Every kind at the index of its Kind (InfoOf); Add Condition... lists them in its own order.
	inline constexpr std::array<KindInfo, 8> kKinds{ {
		{ .kind = Kind::kState,
			.key = "state",
			.label = Strings::Text::kKindState,
			.tip = Strings::Text::kTipState,
			.clauses = { Strings::Text::kClauseState, Strings::Text::kClauseNotState, Strings::Text::kClauseYouState, Strings::Text::kClauseYouNotState },
			.values = kStates,
			.targetable = true,
			.addTip = Strings::Text::kAddTipState },
		{ .kind = Kind::kLocationType,
			.key = "locationType",
			.label = Strings::Text::kKindLocationType,
			.tip = Strings::Text::kTipLocationType,
			.clauses = { Strings::Text::kClauseLocationType, Strings::Text::kClauseNotLocationType },
			.values = kLocationTypes,
			.targetable = false,
			.subject = Strings::Text::kSubjectLocationType,
			.addTip = Strings::Text::kAddTipLocationType },
		// The cell's kind, apart from the location's keywords: the row's dropdown chooses Indoors
		// or Outdoors (`negated`), so its clauses are whole.
		{ .kind = Kind::kIndoors,
			.key = "indoors",
			.label = Strings::Text::kKindIndoors,
			.tip = Strings::Text::kTipIndoors,
			.clauses = { Strings::Text::kClauseIndoors, Strings::Text::kClauseOutdoors },
			.targetable = false,
			.subject = Strings::Text::kSubjectIndoors },
		{ .kind = Kind::kPlace,
			.key = "place",
			.label = Strings::Text::kKindPlace,
			.tip = Strings::Text::kTipPlace,
			.clauses = { Strings::Text::kClausePlace, Strings::Text::kClauseNotPlace },
			.targetable = false,
			.subject = Strings::Text::kSubjectLocation,
			.addTip = Strings::Text::kAddTipPlace },
		{ .kind = Kind::kWeather,
			.key = "weather",
			.label = Strings::Text::kKindWeather,
			.tip = Strings::Text::kTipWeather,
			.clauses = { Strings::Text::kClauseWeather, Strings::Text::kClauseNotWeather },
			.values = kWeathers,
			.targetable = false,
			.subject = Strings::Text::kSubjectWeather,
			.addTip = Strings::Text::kAddTipWeather },
		{ .kind = Kind::kTimeOfDay,
			.key = "timeOfDay",
			.label = Strings::Text::kKindTimeOfDay,
			.tip = Strings::Text::kTipTimeOfDay,
			.clauses = { Strings::Text::kClauseTime, Strings::Text::kClauseNotTime },
			.targetable = false,
			.subject = Strings::Text::kSubjectTime,
			.addTip = Strings::Text::kAddTipTimeOfDay },
		{ .kind = Kind::kSpell,
			.key = "spell",
			.label = Strings::Text::kKindSpell,
			.tip = Strings::Text::kTipSpell,
			.clauses = { Strings::Text::kClauseSpell, Strings::Text::kClauseNotSpell, Strings::Text::kClauseYouSpell, Strings::Text::kClauseYouNotSpell },
			.targetable = true,
			.addTip = Strings::Text::kAddTipSpell },
		// What the actor is doing at a piece of furniture, read as State's values are: its clauses
		// are State's.
		{ .kind = Kind::kCrafting,
			.key = "crafting",
			.label = Strings::Text::kKindCrafting,
			.tip = Strings::Text::kTipCrafting,
			.clauses = { Strings::Text::kClauseState, Strings::Text::kClauseNotState, Strings::Text::kClauseYouState, Strings::Text::kClauseYouNotState },
			.values = kCraftings,
			.targetable = true,
			.addTip = Strings::Text::kAddTipCrafting },
	} };

	static_assert([] {
		for (std::size_t i = 0; i < kKinds.size(); ++i) {
			if (static_cast<std::size_t>(kKinds[i].kind) != i) {
				return false;
			}
		}
		return true;
	}());

	inline const KindInfo& InfoOf(Kind a_kind)
	{
		return kKinds[static_cast<std::size_t>(a_kind)];
	}

	inline bool HasValues(Kind a_kind)
	{
		return !InfoOf(a_kind).values.empty();
	}

	// A kind whose row lists forms: Place, Spell.
	inline bool HasForms(Kind a_kind)
	{
		return a_kind == Kind::kPlace || a_kind == Kind::kSpell;
	}

	// A kind whose row may read the player instead of the wearer (Row::target).
	inline bool Targetable(Kind a_kind)
	{
		return InfoOf(a_kind).targetable;
	}

	// The key of a target in situations.json; the wearer's is left out of the file.
	inline constexpr const char* kPlayerTargetKey = "player";
	inline constexpr const char* kWearerTargetKey = "wearer";

	// The forms a Place or Spell row lists; a row of another kind lists none.
	inline const std::vector<FormRef>& FormsOf(const Row& a_row)
	{
		return a_row.kind == Kind::kSpell ? a_row.spells : a_row.places;
	}

	inline std::vector<FormRef>& FormsOf(Row& a_row)
	{
		return a_row.kind == Kind::kSpell ? a_row.spells : a_row.places;
	}

	// A row of a kind with values with none ticked, or a Place or Spell row with no form: it never
	// holds.
	inline bool NothingSelected(const Row& a_row)
	{
		return HasValues(a_row.kind) ? a_row.values == 0 : HasForms(a_row.kind) && FormsOf(a_row).empty();
	}

	// Whether the situation applies in combat: ticked, or a row that holds only in combat (State,
	// In combat alone, not negated). A row with In combat and another state holds outside combat
	// too, so it leaves the tick to the user; its In combat never holds while unticked.
	inline bool AppliesInCombat(const Rules& a_rules)
	{
		if (a_rules.applyInCombat) {
			return true;
		}
		for (const auto& row : a_rules.rows) {
			if (row.kind == Kind::kState && !row.negated && row.values == kInCombat) {
				return true;
			}
		}
		return false;
	}

	// Whether the clock (minutes after midnight) is in the range: To before From wraps
	// midnight; From equal to To is the whole day.
	inline bool InTimeRange(std::uint16_t a_from, std::uint16_t a_to, std::uint16_t a_minute)
	{
		if (a_from == a_to) {
			return true;
		}
		return a_from < a_to ? a_minute >= a_from && a_minute < a_to : a_minute >= a_from || a_minute < a_to;
	}

	// "8 PM", "8:30 PM", "12 AM" for midnight; as the chosen language writes a time, which may be
	// by the hour of 24 ("20:30"): the time texts take the hour of 12, the minutes and the hour
	// of 24.
	inline std::string TimeText(std::uint16_t a_minutes)
	{
		const int  hour = a_minutes / 60;
		const int  shown = hour % 12 == 0 ? 12 : hour % 12;
		const auto minutes = std::format("{:02}", a_minutes % 60);
		const bool morning = hour < 12;
		const auto text = a_minutes % 60 == 0 ? (morning ? Strings::Text::kTimeAm : Strings::Text::kTimePm) :
		                                        (morning ? Strings::Text::kTimeAmMinutes : Strings::Text::kTimePmMinutes);
		return Strings::Format(text, shown, minutes, hour);
	}

	// "a", "a or b", "a, b or c".
	inline std::string JoinWithOr(const std::vector<std::string>& a_parts)
	{
		std::string text;
		for (std::size_t i = 0; i < a_parts.size(); ++i) {
			if (i > 0) {
				text += Strings::Get(i + 1 == a_parts.size() ? Strings::Text::kAppliesOr : Strings::Text::kListSeparator);
			}
			text += a_parts[i];
		}
		return text;
	}

	// The row's clause in the Applies line (KindInfo::clauses): "in a town or a castle", "not
	// in rain", "indoors", "in combat or at the ready", "smithing or tanning", "at Breezehome or
	// Whiterun", "while Oakflesh or Stoneflesh is on"; reading the player (Target::kPlayer), "while you are swimming", "while
	// you are not in water", "while Stoneflesh is on you".
	inline std::string RowText(const Row& a_row)
	{
		const auto& info = InfoOf(a_row.kind);
		if (NothingSelected(a_row)) {
			return Strings::Format(Strings::Text::kNothingSelected, Strings::Get(info.label));
		}
		const bool you = a_row.target == Target::kPlayer && Targetable(a_row.kind);
		const auto clause = you ? (a_row.negated ? info.clauses.youNegated : info.clauses.you) : (a_row.negated ? info.clauses.negated : info.clauses.wearer);
		if (!info.values.empty()) {
			std::vector<std::string> ticked;
			for (const auto& value : info.values) {
				if (a_row.values & value.bit) {
					ticked.emplace_back(Strings::Get(value.phrase));
				}
			}
			return Strings::Format(clause, JoinWithOr(ticked));
		}
		if (HasForms(a_row.kind)) {
			std::vector<std::string> names;
			for (const auto& form : FormsOf(a_row)) {
				names.push_back(FormName(form));
			}
			return Strings::Format(clause, JoinWithOr(names));
		}
		if (a_row.kind == Kind::kTimeOfDay) {
			if (a_row.from == a_row.to) {
				return Strings::Get(a_row.negated ? Strings::Text::kClauseNotAnyHour : Strings::Text::kClauseAnyHour);
			}
			return Strings::Format(clause, TimeText(a_row.from), TimeText(a_row.to));
		}
		return Strings::Get(clause);
	}

	// The Applies line: one clause per row in order, "In a town, in rain or snow, sleeping.", an
	// ASCII first letter in upper case; "-" with no rows.
	inline std::string Text(const Rules& a_rules)
	{
		if (a_rules.rows.empty()) {
			return Strings::Get(Strings::Text::kNeverApplies);
		}
		std::string text;
		for (const auto& row : a_rules.rows) {
			text += (text.empty() ? "" : Strings::Get(Strings::Text::kListSeparator)) + RowText(row);
		}
		if (text[0] >= 'a' && text[0] <= 'z') {
			text[0] = static_cast<char>(text[0] - 'a' + 'A');
		}
		return text + Strings::Get(Strings::Text::kAppliesEnd);
	}

	// --- Never worn -----------------------------------------------------------
	//
	// A situation is never worn while one lower in the list applies whenever it does, as far as
	// the rules tell (docs/custom-situations.md §8a). The Situations page works it out as it opens
	// and after each edit; never the poll.

	// What the never-worn mark knows of a place: Situations::PlaceFacts reads it as the window
	// opens.
	struct PlaceFact
	{
		std::uint32_t formID = 0;
		// The locations it is in, nearest first, as the Place rule walks them: a location's
		// parents; a cell's location, then that location's parents.
		std::vector<std::uint32_t> within;
		// Bit m set when its own keywords, or those of a location within it, give exactly the
		// Location type values m (a cell: those of its location); 0 for a cell without one. There
		// are four values, so m is below 16.
		std::uint16_t typeMasks = 0;
	};

	// A situation that never applies: no rows, or a row with nothing selected (its Applies line
	// says so). It is never marked and covers nothing.
	inline bool NeverApplies(const Rules& a_rules)
	{
		return a_rules.rows.empty() || std::ranges::any_of(a_rules.rows, NothingSelected);
	}

	namespace Detail
	{
		inline const PlaceFact* FactOf(std::span<const PlaceFact> a_facts, std::uint32_t a_formID)
		{
			const auto found = std::ranges::find(a_facts, a_formID, &PlaceFact::formID);
			return a_formID != 0 && found != a_facts.end() ? &*found : nullptr;
		}

		// Every minute of range `a_inner` is in range `a_outer` (InTimeRange). Each range's
		// membership changes only at its From and To, so the minutes at the four of them start
		// every stretch where neither changes.
		inline bool TimeWithin(const Row& a_inner, const Row& a_outer)
		{
			for (const auto minute : { a_inner.from, a_inner.to, a_outer.from, a_outer.to }) {
				if (InTimeRange(a_inner.from, a_inner.to, minute) && !InTimeRange(a_outer.from, a_outer.to, minute)) {
					return false;
				}
			}
			return true;
		}

		// The same spell, or one with the same visible effects, which an Active Effects row
		// counts the same (SameEffects).
		inline bool SameSpell(const FormRef& a_left, const FormRef& a_right)
		{
			return SameForm(a_left.ref, a_right.ref) || (!a_left.effects.empty() && a_left.effects == a_right.effects);
		}

		// Each spell of `a_inner` is one of `a_outer`'s.
		inline bool SpellsWithin(const std::vector<FormRef>& a_inner, const std::vector<FormRef>& a_outer)
		{
			return std::ranges::all_of(a_inner, [&a_outer](const FormRef& a_spell) {
				return std::ranges::any_of(a_outer, [&a_spell](const FormRef& a_other) { return SameSpell(a_spell, a_other); });
			});
		}

		// Each place of `a_inner` is one of `a_outer`'s or within one (PlaceFact::within).
		inline bool PlacesWithin(const std::vector<Place>& a_inner, const std::vector<Place>& a_outer, std::span<const PlaceFact> a_facts)
		{
			return std::ranges::all_of(a_inner, [&a_outer, a_facts](const Place& a_place) {
				const auto* fact = FactOf(a_facts, a_place.formID);
				return std::ranges::any_of(a_outer, [&a_place, fact](const Place& a_other) {
					return SameForm(a_place.ref, a_other.ref) ||
					       (fact && a_other.formID != 0 && std::ranges::find(fact->within, a_other.formID) != fact->within.end());
				});
			});
		}

		// Each place of `a_places` gives one of Location type's `a_values` wherever in it the
		// actor is: it and every location within it (PlaceFact::typeMasks). No place gives an
		// Indoors row: a place may have outdoor cells.
		inline bool PlacesHaveTypes(const std::vector<Place>& a_places, std::uint32_t a_values, std::span<const PlaceFact> a_facts)
		{
			return std::ranges::all_of(a_places, [a_values, a_facts](const Place& a_place) {
				const auto* fact = FactOf(a_facts, a_place.formID);
				if (!fact || fact->typeMasks == 0) {
					return false;
				}
				for (std::uint32_t mask = 0; mask < 16; ++mask) {
					if ((fact->typeMasks & (1u << mask)) && (mask & a_values) == 0) {
						return false;
					}
				}
				return true;
			});
		}

		// Row `a_row` holds whenever row `a_by` does.
		inline bool Implies(const Row& a_by, const Row& a_row, std::span<const PlaceFact> a_facts)
		{
			if (a_by.kind == Kind::kPlace && a_row.kind == Kind::kLocationType && !a_by.negated && !a_row.negated) {
				return PlacesHaveTypes(a_by.places, a_row.values, a_facts);
			}
			if (a_by.kind != a_row.kind || a_by.negated != a_row.negated || a_by.target != a_row.target) {
				return false;
			}
			// Negated, the row holds outside its values: the narrower list is the one implied.
			const auto& inner = a_row.negated ? a_row : a_by;
			const auto& outer = a_row.negated ? a_by : a_row;
			switch (a_row.kind) {
			case Kind::kLocationType:
			case Kind::kWeather:
			case Kind::kState:
			case Kind::kCrafting:
				return (inner.values & ~outer.values) == 0;
			case Kind::kIndoors:
				return true;  // the same side of the door
			case Kind::kTimeOfDay:
				return TimeWithin(inner, outer);
			case Kind::kPlace:
				return PlacesWithin(inner.places, outer.places, a_facts);
			case Kind::kSpell:
				return SpellsWithin(inner.spells, outer.spells);
			}
			return false;
		}
	}

	// Whether `a_lower` applies whenever `a_rules` does, so that a situation with `a_rules` above
	// one with `a_lower` is never worn: `a_lower` has rows and applies in combat if `a_rules`
	// does, and each of its rows holds whenever one row of `a_rules` holds (Detail::Implies).
	// False for a pair this cannot tell, so a missing mark proves nothing.
	inline bool CoveredBy(const Rules& a_rules, const Rules& a_lower, std::span<const PlaceFact> a_facts)
	{
		if (NeverApplies(a_rules) || NeverApplies(a_lower) || (AppliesInCombat(a_rules) && !AppliesInCombat(a_lower))) {
			return false;
		}
		return std::ranges::all_of(a_lower.rows, [&a_rules, a_facts](const Row& a_row) {
			return std::ranges::any_of(a_rules.rows, [&a_row, a_facts](const Row& a_by) { return Detail::Implies(a_by, a_row, a_facts); });
		});
	}

	// --- situations.json ------------------------------------------------------
	//
	// A row: { "kind": "weather", "not": false, "values": ["rain", "snow"] }; Time of day has
	// "from" and "to" ("20:00") instead of "values", Indoors neither ("not": true is outdoors);
	// Place's and Spell's values are pieces
	// (PieceFile::ToJson: plugin, formId, name); a Targetable kind reading the player has
	// "target": "player" (the wearer's is left out). Other fields are ignored.

	namespace Detail
	{
		inline std::string TimeKey(std::uint16_t a_minutes)
		{
			return std::format("{:02}:{:02}", a_minutes / 60, a_minutes % 60);
		}

		// "H:MM" or "HH:MM", 00:00 to 23:59. nullopt for anything else.
		inline std::optional<std::uint16_t> ParseTime(std::string_view a_text)
		{
			const auto colon = a_text.find(':');
			if (colon == std::string_view::npos || colon == 0 || colon > 2 || a_text.size() != colon + 3) {
				return std::nullopt;
			}
			int hour = 0;
			int minute = 0;
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				if (i == colon) {
					continue;
				}
				if (a_text[i] < '0' || a_text[i] > '9') {
					return std::nullopt;
				}
				auto& part = i < colon ? hour : minute;
				part = part * 10 + (a_text[i] - '0');
			}
			if (hour > 23 || minute > 59) {
				return std::nullopt;
			}
			return static_cast<std::uint16_t>(hour * 60 + minute);
		}

		inline std::optional<Kind> KindOf(std::string_view a_key)
		{
			for (const auto& info : kKinds) {
				if (a_key == info.key) {
					return info.kind;
				}
			}
			return std::nullopt;
		}

		// Reads one row. Returns why it cannot, as a sentence, or nullopt.
		inline std::optional<std::string> ParseRow(const nlohmann::json& a_json, Row& a_row)
		{
			if (!a_json.is_object()) {
				return "is not a JSON object.";
			}
			const auto kindField = a_json.find("kind");
			if (kindField == a_json.end() || !kindField->is_string()) {
				return "has no kind.";
			}
			const auto kind = KindOf(kindField->get<std::string>());
			if (!kind) {
				return std::format("has the unknown kind \"{}\".", kindField->get<std::string>());
			}
			a_row = Row{ .kind = *kind };
			if (const auto negated = a_json.find("not"); negated != a_json.end()) {
				if (!negated->is_boolean()) {
					return "has a not that is not true or false.";
				}
				a_row.negated = negated->get<bool>();
			}
			const auto& info = InfoOf(*kind);
			if (const auto target = a_json.find("target"); target != a_json.end()) {
				const auto key = target->is_string() ? target->get<std::string>() : std::string{};
				if (!info.targetable) {
					return std::format("has a target, which {} cannot have.", Strings::English(info.label));
				}
				if (key != kPlayerTargetKey && key != kWearerTargetKey) {
					return std::format("has a target that is neither \"{}\" nor \"{}\".", kWearerTargetKey, kPlayerTargetKey);
				}
				a_row.target = key == kPlayerTargetKey ? Target::kPlayer : Target::kWearer;
			}
			if (!info.values.empty()) {
				const auto values = a_json.find("values");
				if (values == a_json.end() || !values->is_array()) {
					return "has no value list.";
				}
				for (const auto& value : *values) {
					const auto   key = value.is_string() ? value.get<std::string>() : std::string{};
					const Value* found = nullptr;
					for (const auto& known : info.values) {
						if (key == known.key) {
							found = &known;
						}
					}
					if (!found) {
						return std::format("has a value {} cannot have.", Strings::English(info.label));
					}
					a_row.values |= found->bit;
				}
			} else if (HasForms(*kind)) {
				const auto* noun = *kind == Kind::kPlace ? "place" : "spell";
				const auto  values = a_json.find("values");
				if (values == a_json.end() || !values->is_array()) {
					return std::format("has no {} list.", noun);
				}
				for (const auto& value : *values) {
					FormRef form;
					if (PieceFile::ParsePiece(value, form.ref)) {
						return std::format("has a {} without a plugin, or whose formId is not 0x and up to six hex digits.", noun);
					}
					FormsOf(a_row).push_back(std::move(form));
				}
			} else if (*kind == Kind::kTimeOfDay) {
				const auto from = a_json.find("from");
				const auto to = a_json.find("to");
				const auto fromTime = from != a_json.end() && from->is_string() ? ParseTime(from->get<std::string>()) : std::nullopt;
				const auto toTime = to != a_json.end() && to->is_string() ? ParseTime(to->get<std::string>()) : std::nullopt;
				if (!fromTime || !toTime) {
					return "has a from or to that is not a time such as \"20:00\".";
				}
				a_row.from = *fromTime;
				a_row.to = *toTime;
			}
			return std::nullopt;
		}
	}

	// The rows as situations.json keeps them, every field written.
	inline nlohmann::ordered_json ToJson(const std::vector<Row>& a_rows)
	{
		auto list = nlohmann::ordered_json::array();
		for (const auto& row : a_rows) {
			const auto&            info = InfoOf(row.kind);
			nlohmann::ordered_json json{ { "kind", info.key }, { "not", row.negated } };
			if (row.target == Target::kPlayer && info.targetable) {
				json["target"] = kPlayerTargetKey;
			}
			if (!info.values.empty()) {
				auto values = nlohmann::ordered_json::array();
				for (const auto& value : info.values) {
					if (row.values & value.bit) {
						values.push_back(value.key);
					}
				}
				json["values"] = std::move(values);
			} else if (HasForms(row.kind)) {
				auto values = nlohmann::ordered_json::array();
				for (const auto& form : FormsOf(row)) {
					values.push_back(PieceFile::ToJson(form.ref));
				}
				json["values"] = std::move(values);
			} else if (row.kind == Kind::kTimeOfDay) {
				json["from"] = Detail::TimeKey(row.from);
				json["to"] = Detail::TimeKey(row.to);
			}
			list.push_back(std::move(json));
		}
		return list;
	}

	// Reads the rows ToJson wrote into `a_rows`. All or nothing: returns why one cannot be read,
	// as a sentence ("Condition 2 has the unknown kind ..."), or nullopt.
	inline std::optional<std::string> ParseRows(const nlohmann::json& a_list, std::vector<Row>& a_rows)
	{
		a_rows.clear();
		if (!a_list.is_array()) {
			return "Its conditions are not a list.";
		}
		for (std::size_t i = 0; i < a_list.size(); ++i) {
			Row row;
			if (const auto problem = Detail::ParseRow(a_list[i], row)) {
				a_rows.clear();
				return std::format("Its condition {} {}", i + 1, *problem);
			}
			a_rows.push_back(std::move(row));
		}
		return std::nullopt;
	}
}
