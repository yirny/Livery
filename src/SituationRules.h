// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "PieceFile.h"
#include "Utils.h"

#include <nlohmann/json.hpp>

#include <array>
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
// holds for an actor.
//
// A situation applies while every row holds; a row with values holds while any of its values
// does (a Place row while any of its places does), `negated` inverts it, and a row with
// nothing selected never holds (NothingSelected).
namespace SituationRules
{
	enum class Kind : std::uint8_t
	{
		kInCombat,
		kSleeping,
		kInWater,
		kSwimming,
		kIndoors,
		kLocationType,
		kPlace,
		kWeather,
		kTimeOfDay
	};

	// Location type's values, bits of Row::values.
	inline constexpr std::uint32_t kTown = 1u << 0;
	inline constexpr std::uint32_t kCastle = 1u << 1;
	inline constexpr std::uint32_t kPlayerHome = 1u << 2;
	inline constexpr std::uint32_t kDungeon = 1u << 3;
	// Weather's values, bits of Row::values.
	inline constexpr std::uint32_t kRain = 1u << 0;
	inline constexpr std::uint32_t kSnow = 1u << 1;
	inline constexpr std::uint32_t kSnowyRegion = 1u << 2;

	inline constexpr std::uint16_t kMinutesPerDay = 24 * 60;

	// A place of a Place row: a location or a cell, kept as a piece is (plugin and local FormID),
	// so it survives load order changes; one whose plugin is not loaded stays and holds nowhere.
	struct Place
	{
		PieceFile::PieceRef ref;
		// Its FormID in this game's load order, 0 when its plugin is not loaded: what the poll
		// compares. SituationStore sets it as it reads or saves the rules (PieceFile::FormIDOf);
		// not in the file, and not compared, being the same place.
		std::uint32_t formID = 0;

		friend bool operator==(const Place& a_left, const Place& a_right) { return a_left.ref == a_right.ref; }
	};

	// The same place: the same local FormID of the same plugin (ASCII case ignored).
	inline bool SamePlace(const PieceFile::PieceRef& a_left, const PieceFile::PieceRef& a_right)
	{
		return a_left.localID == a_right.localID && ExampleMod::CaseInsensitiveEqual(a_left.plugin, a_right.plugin);
	}

	// The place's name as saved; its plugin and FormID when it has none.
	inline std::string PlaceName(const Place& a_place)
	{
		return a_place.ref.name.empty() ? std::format("{} {:06X}", a_place.ref.plugin, a_place.ref.localID) : a_place.ref.name;
	}

	// What the Add place popup offers (docs/custom-situations.md §7), read as the window opens
	// (Situations::PlacesAround).
	struct PlaceChoices
	{
		std::optional<Place> location;  // the actor's current location; nullopt outside any
		std::optional<Place> cell;      // the cell they stand in
	};

	struct Row
	{
		Kind               kind = Kind::kInCombat;
		bool               negated = false;  // the row's `not`
		std::uint32_t      values = 0;       // a kind with values (KindInfo::values): the bits of those ticked
		std::uint16_t      from = 20 * 60;   // Time of day: minutes after midnight, below kMinutesPerDay
		std::uint16_t      to = 6 * 60;
		std::vector<Place> places;  // Place: in the order added

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
		std::uint32_t bit;
		const char*   key;     // in situations.json
		const char*   label;   // the editor's checkbox
		const char*   phrase;  // in the Applies line, after "in"
	};

	inline constexpr std::array<Value, 4> kLocationTypes{ {
		{ kTown, "town", "Town", "a town" },
		{ kCastle, "castle", "Castle", "a castle" },
		{ kPlayerHome, "playerHome", "Player home", "a player home" },
		{ kDungeon, "dungeon", "Dungeon", "a dungeon" },
	} };

	inline constexpr std::array<Value, 3> kWeathers{ {
		{ kRain, "rain", "Rain", "rain" },
		{ kSnow, "snow", "Snow", "snow" },
		{ kSnowyRegion, "snowyRegion", "Snowy region", "a snowy region" },
	} };

	struct KindInfo
	{
		Kind                   kind;
		const char*            key;     // in situations.json
		const char*            label;   // the editor's
		const char*            tip;     // the label's tooltip
		const char*            phrase;  // in the Applies line; unused for a kind with values or Time of day
		std::span<const Value> values;  // empty for a kind without
	};

	// Every kind at the index of its Kind (InfoOf), in the order the editor offers them.
	inline constexpr std::array<KindInfo, 9> kKinds{ {
		{ Kind::kInCombat, "inCombat", "In combat", "Fighting.\nWith this condition the situation applies in combat.", "in combat", {} },
		{ Kind::kSleeping, "sleeping", "Sleeping", "Going to bed, in it or getting up.", "sleeping", {} },
		{ Kind::kInWater, "inWater", "In water", "Swimming, or wading.\nOn a horse, swimming only.", "in water", {} },
		{ Kind::kSwimming, "swimming", "Swimming", "Swimming only, not wading.", "swimming", {} },
		{ Kind::kIndoors, "indoors", "Indoors", "Inside a building, cave or other interior.", "indoors", {} },
		{ Kind::kLocationType, "locationType", "Location type",
			"Town: a city, town, settlement, inn or house.\nCastle: a jarl's palace or keep.\nPlayer home: a house you own.\nDungeon: a cave, ruin, fort or other dungeon.",
			"", kLocationTypes },
		{ Kind::kPlace, "place", "Place", "In one of the places listed.\nA place counts with every room and area in it.", "", {} },
		{ Kind::kWeather, "weather", "Weather",
			"Rain and Snow follow the sky, indoors too.\nSnowy region: a region where it often snows, whatever the sky.", "", kWeathers },
		{ Kind::kTimeOfDay, "timeOfDay", "Time of day", "Game time.\nA range past midnight wraps, as 8 PM to 6 AM.", "", {} },
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

	// A row of a kind with values with none ticked, or a Place row with no place: it never holds.
	inline bool NothingSelected(const Row& a_row)
	{
		return HasValues(a_row.kind) ? a_row.values == 0 : a_row.kind == Kind::kPlace && a_row.places.empty();
	}

	// Whether the situation applies in combat: ticked, or a row that holds only in combat.
	inline bool AppliesInCombat(const Rules& a_rules)
	{
		if (a_rules.applyInCombat) {
			return true;
		}
		for (const auto& row : a_rules.rows) {
			if (row.kind == Kind::kInCombat && !row.negated) {
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

	// "8 PM", "8:30 PM", "12 AM" for midnight.
	inline std::string TimeText(std::uint16_t a_minutes)
	{
		const int  hour = a_minutes / 60;
		const int  minute = a_minutes % 60;
		const int  shown = hour % 12 == 0 ? 12 : hour % 12;
		const auto half = hour < 12 ? "AM" : "PM";
		return minute == 0 ? std::format("{} {}", shown, half) : std::format("{}:{:02} {}", shown, minute, half);
	}

	// "a", "a or b", "a, b or c".
	inline std::string JoinWithOr(const std::vector<std::string>& a_parts)
	{
		std::string text;
		for (std::size_t i = 0; i < a_parts.size(); ++i) {
			if (i > 0) {
				text += i + 1 == a_parts.size() ? " or " : ", ";
			}
			text += a_parts[i];
		}
		return text;
	}

	// The row's phrase in the Applies line, `not` first: "in a town or castle", "not indoors",
	// "at Breezehome or Whiterun".
	inline std::string RowText(const Row& a_row)
	{
		const auto& info = InfoOf(a_row.kind);
		if (NothingSelected(a_row)) {
			return std::format("{} (nothing selected)", info.label);
		}
		std::string phrase;
		if (!info.values.empty()) {
			std::vector<std::string> ticked;
			for (const auto& value : info.values) {
				if (a_row.values & value.bit) {
					ticked.emplace_back(value.phrase);
				}
			}
			phrase = "in " + JoinWithOr(ticked);
		} else if (a_row.kind == Kind::kPlace) {
			std::vector<std::string> names;
			for (const auto& place : a_row.places) {
				names.push_back(PlaceName(place));
			}
			phrase = "at " + JoinWithOr(names);
		} else if (a_row.kind == Kind::kTimeOfDay) {
			phrase = a_row.from == a_row.to ? "at any hour" : std::format("from {} to {}", TimeText(a_row.from), TimeText(a_row.to));
		} else {
			phrase = info.phrase;
		}
		return a_row.negated ? "not " + phrase : phrase;
	}

	// The Applies line: one phrase per row in order, "In a town, in rain or snow, not indoors.";
	// "Never applies." with no rows.
	inline std::string Text(const Rules& a_rules)
	{
		if (a_rules.rows.empty()) {
			return "Never applies.";
		}
		std::string text;
		for (const auto& row : a_rules.rows) {
			text += (text.empty() ? "" : ", ") + RowText(row);
		}
		if (text[0] >= 'a' && text[0] <= 'z') {
			text[0] = static_cast<char>(text[0] - 'a' + 'A');
		}
		return text + ".";
	}

	// --- situations.json ------------------------------------------------------
	//
	// A row: { "kind": "weather", "not": false, "values": ["rain", "snow"] }; Time of day has
	// "from" and "to" ("20:00") instead of "values"; Place's values are pieces
	// (PieceFile::ToJson: plugin, formId, name). Other fields are ignored.

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
						return std::format("has a value {} cannot have.", info.label);
					}
					a_row.values |= found->bit;
				}
			} else if (*kind == Kind::kPlace) {
				const auto values = a_json.find("values");
				if (values == a_json.end() || !values->is_array()) {
					return "has no place list.";
				}
				for (const auto& value : *values) {
					Place place;
					if (PieceFile::ParsePiece(value, place.ref)) {
						return "has a place without a plugin, or whose formId is not 0x and up to six hex digits.";
					}
					a_row.places.push_back(std::move(place));
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
			if (!info.values.empty()) {
				auto values = nlohmann::ordered_json::array();
				for (const auto& value : info.values) {
					if (row.values & value.bit) {
						values.push_back(value.key);
					}
				}
				json["values"] = std::move(values);
			} else if (row.kind == Kind::kPlace) {
				auto values = nlohmann::ordered_json::array();
				for (const auto& place : row.places) {
					values.push_back(PieceFile::ToJson(place.ref));
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
