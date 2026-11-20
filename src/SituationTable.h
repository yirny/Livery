// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SituationRules.h"
#include "Situations.h"
#include "Strings.h"
#include "Utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// The situations as situations.json keeps them (docs/situation-management-ui.md §2,
// docs/custom-situations.md §6): every situation's name, removed flag and rules, their order,
// and the rules that need no game: the presets as installed, the file's parse and write, the
// name rules, Restore Presets' change lines, a reorder of the window's list. SituationStore keeps
// the table and writes the file; the unit tests reach this header.
//
// Version 1 holds the presets' names and removed flags only, those that differ from as
// installed, and is written while nothing else differs: no user's situation ever made, every
// preset's rules as installed, the installed order. Version 2 holds the window's list top to
// bottom, every preset and every user's situation not removed, each with every field:
//   { "version": 2, "nextKey": 3, "situations": [
//     { "key": "Town", "name": "City", "removed": false, "applyInCombat": false, "conditions": [ ... ] },
//     { "key": "user:2", "name": "Rainy town", "applyInCombat": false, "conditions": [ ... ] } ] }
// (the conditions: SituationRules::ToJson). An entry's other fields are ignored.
namespace SituationTable
{
	inline constexpr int kVersion = 2;  // the newest "version" this code reads and writes; 1 is read too

	struct Entry
	{
		std::string           name;  // as the file keeps it; DisplayName is what the window and the HUD show
		bool                  removed = false;
		std::string           key;    // a user's situation's (`user:<n>`); "" for a preset, keyed by Situations::Name
		SituationRules::Rules rules;  // every situation's but the Default's, which has none

		friend bool operator==(const Entry&, const Entry&) = default;
	};

	inline bool IsUser(const Entry& a_entry)
	{
		return !a_entry.key.empty();
	}

	// One entry per Situations::Situation, at the index of its id: the Default and the presets
	// first (Presets), then the user's in the order they were made. A removed user's situation
	// stays until the game is closed, so no id moves; the file no longer has it.
	struct Table
	{
		std::vector<Entry> entries;
		// Every situation but the Default, the window's list top to bottom (Situations::Order):
		// the lowest that applies is worn. Removed ones keep their place.
		std::vector<Situations::Situation> order;
		std::uint32_t                      nextKey = 1;  // the number the next user's situation's key takes (UserKey); never reused

		Entry&       operator[](std::size_t a_index) { return entries[a_index]; }
		const Entry& operator[](std::size_t a_index) const { return entries[a_index]; }
		std::size_t  size() const noexcept { return entries.size(); }
		auto         begin() noexcept { return entries.begin(); }
		auto         end() noexcept { return entries.end(); }
		auto         begin() const noexcept { return entries.begin(); }
		auto         end() const noexcept { return entries.end(); }

		friend bool operator==(const Table&, const Table&) = default;
	};

	constexpr std::size_t IndexOf(Situations::Situation a_situation) noexcept
	{
		return static_cast<std::size_t>(a_situation);
	}

	// A preset's rules as installed (docs/custom-situations.md §3), Allow during combat off; none
	// for the Default.
	inline SituationRules::Rules PresetRules(Situations::Situation a_situation)
	{
		using Kind = SituationRules::Kind;
		using Situation = Situations::Situation;
		switch (a_situation) {
		case Situation::kTown:
			return { .rows = { { .kind = Kind::kLocationType, .values = SituationRules::kTown } } };
		case Situation::kCastle:
			return { .rows = { { .kind = Kind::kLocationType, .values = SituationRules::kCastle } } };
		case Situation::kHome:
			return { .rows = { { .kind = Kind::kLocationType, .values = SituationRules::kPlayerHome } } };
		case Situation::kSleep:
			return { .rows = { { .kind = Kind::kState, .values = SituationRules::kSleeping } } };
		case Situation::kCold:
			return { .rows = { { .kind = Kind::kWeather, .values = SituationRules::kSnow | SituationRules::kFreezingRegion } } };
		default:
			return {};
		}
	}

	// As Livery installs it: named as its key, with its rules as installed.
	inline Entry Preset(Situations::Situation a_situation)
	{
		return { .name = Situations::Name(a_situation), .rules = PresetRules(a_situation) };
	}

	inline Table Presets()
	{
		Table table;
		table.entries.resize(Situations::kPresetCount);
		for (std::size_t i = 0; i < table.size(); ++i) {
			table[i] = Preset(static_cast<Situations::Situation>(i));
		}
		table.order.assign(Situations::kInstalledOrder.begin(), Situations::kInstalledOrder.end());
		return table;
	}

	// "user:3".
	inline std::string UserKey(std::uint32_t a_number)
	{
		return std::format("user:{}", a_number);
	}

	// The number of a user's key ("user:3"); nullopt for any other text.
	inline std::optional<std::uint32_t> UserKeyNumber(std::string_view a_key)
	{
		constexpr std::string_view kPrefix = "user:";
		if (!a_key.starts_with(kPrefix) || a_key.size() == kPrefix.size() || a_key.size() > kPrefix.size() + 9) {
			return std::nullopt;
		}
		std::uint32_t number = 0;
		for (const char c : a_key.substr(kPrefix.size())) {
			if (c < '0' || c > '9') {
				return std::nullopt;
			}
			number = number * 10 + static_cast<std::uint32_t>(c - '0');
		}
		return number > 0 ? std::optional{ number } : std::nullopt;
	}

	// Whether the co-save keeps a row stored under `a_key`, a situation this session does not
	// have: a user's situation (UserKey) while situations.json could not be read (`a_fileLoaded`
	// false), so the row comes back once the file can. Any other unknown key's row is dropped.
	inline bool KeepsUnknownRow(std::string_view a_key, bool a_fileLoaded)
	{
		return !a_fileLoaded && UserKeyNumber(a_key).has_value();
	}

	// Adds a user's situation to `a_table` under the next key (UserKey), named `a_name`, with
	// `a_rules`: in the order right after `a_after`, or last without it, as Situations::AddUser
	// puts it. Its id is the table's size before, as AddUser gives it. Returns its key.
	inline std::string AddUserEntry(Table& a_table, std::string a_name, SituationRules::Rules a_rules,
		std::optional<Situations::Situation> a_after = std::nullopt)
	{
		const auto situation = static_cast<Situations::Situation>(a_table.size());
		auto       key = UserKey(a_table.nextKey++);
		const auto after = a_after ? std::ranges::find(a_table.order, *a_after) : a_table.order.end();
		a_table.order.insert(after == a_table.order.end() ? after : after + 1, situation);
		a_table.entries.push_back({ .name = std::move(a_name), .key = key, .rules = std::move(a_rules) });
		return key;
	}

	// --- Names ------------------------------------------------------------------
	//
	// The file keeps a preset's name in English while the user has not renamed it, whatever the
	// language (the name is its key, Situations::Name), so one situations.json reads the same in
	// every language. Such a name is shown in the chosen language (DisplayName), and one the
	// window hands back unchanged is stored as the key again (StoredName). A name the user
	// entered is kept and shown as entered.

	// The presets' names as installed, at the index of each; in English, its key.
	inline constexpr std::array<Strings::Text, Situations::kPresetCount> kPresetNames{ Strings::Text::kPresetDefault, Strings::Text::kPresetTown,
		Strings::Text::kPresetCastle, Strings::Text::kPresetHome, Strings::Text::kPresetSleep, Strings::Text::kPresetCold };

	// What reads a text: Strings::Get for the chosen language, Strings::English for the file's.
	using Lookup = const char* (*)(Strings::Text);

	// The name shown for the entry at `a_index`: a preset still named as installed in the
	// language of `a_lookup`, any other as the file keeps it.
	inline std::string DisplayName(const Entry& a_entry, std::size_t a_index, Lookup a_lookup = Strings::Get)
	{
		if (a_index < Situations::kPresetCount && a_entry.name == Situations::Name(static_cast<Situations::Situation>(a_index))) {
			return a_lookup(kPresetNames[a_index]);
		}
		return a_entry.name;
	}

	// The name the file keeps for `a_name`, as the window gives it for the situation at
	// `a_index`: a preset's name as installed in the language of `a_lookup` is its key.
	inline std::string StoredName(std::size_t a_index, std::string a_name, Lookup a_lookup = Strings::Get)
	{
		if (a_index < Situations::kPresetCount && a_name == a_lookup(kPresetNames[a_index])) {
			return Situations::Name(static_cast<Situations::Situation>(a_index));
		}
		return a_name;
	}

	// Why the names break the rules, or nullopt: every name has text, the Default is never
	// removed (it may be renamed), and no two situations not removed share a name, as the file
	// keeps it or as shown in the language of `a_lookup` (ASCII case ignored). Sentences in that
	// language: for the window, and with Strings::English for the log.
	inline std::optional<std::string> CheckNames(const Table& a_table, Lookup a_lookup = Strings::Get)
	{
		if (a_table[IndexOf(Situations::Situation::kDefault)].removed) {
			return a_lookup(Strings::Text::kDefaultNotRemoved);
		}
		std::vector<std::string> shown;
		for (std::size_t i = 0; i < a_table.size(); ++i) {
			if (ExampleMod::TrimWhitespace(a_table[i].name).empty()) {
				return a_lookup(Strings::Text::kNameEverySituation);
			}
			shown.push_back(DisplayName(a_table[i], i, a_lookup));
		}
		for (std::size_t i = 0; i < a_table.size(); ++i) {
			if (a_table[i].removed) {
				continue;
			}
			for (std::size_t j = i + 1; j < a_table.size(); ++j) {
				if (!a_table[j].removed &&
					(ExampleMod::CaseInsensitiveEqual(a_table[i].name, a_table[j].name) || ExampleMod::CaseInsensitiveEqual(shown[i], shown[j]))) {
					return std::vformat(a_lookup(Strings::Text::kTwoSituationsNamed), std::make_format_args(shown[j]));
				}
			}
		}
		return std::nullopt;
	}

	// `a_order` (every situation but the Default) with the situations of `a_shown` (those the
	// window lists, not removed) put in `a_shown`'s order, each in a place one of them held: a
	// removed situation keeps its place. nullopt when `a_shown` is not exactly the situations of
	// `a_order` that are not removed (`a_removed`).
	template <class Removed>
	std::optional<std::vector<Situations::Situation>> Reordered(std::span<const Situations::Situation> a_order,
		std::span<const Situations::Situation> a_shown, Removed&& a_removed)
	{
		std::vector<Situations::Situation> order(a_order.begin(), a_order.end());
		std::size_t                        next = 0;
		for (auto& place : order) {
			if (a_removed(place)) {
				continue;
			}
			if (next == a_shown.size() || std::ranges::find(a_order, a_shown[next]) == a_order.end() || a_removed(a_shown[next])) {
				return std::nullopt;
			}
			place = a_shown[next++];
		}
		auto sorted = std::vector<Situations::Situation>(a_shown.begin(), a_shown.end());
		std::ranges::sort(sorted);
		if (next != a_shown.size() || std::ranges::adjacent_find(sorted) != sorted.end()) {
			return std::nullopt;
		}
		return order;
	}

	namespace Detail
	{
		// Reads one field the entry may leave out (the value given stays). Returns false when
		// it is there with another type.
		template <class T>
		bool ReadOptional(const nlohmann::json& a_object, const char* a_field, T& a_value)
		{
			const auto found = a_object.find(a_field);
			if (found == a_object.end()) {
				return true;
			}
			if constexpr (std::is_same_v<T, bool>) {
				if (!found->is_boolean()) {
					return false;
				}
			} else if (!found->is_string()) {
				return false;
			}
			a_value = found->get<T>();
			return true;
		}

		// A situation's applyInCombat and conditions, version 2's fields; the rules given stay when
		// `a_required` is false and the conditions are left out (a preset keeps them as installed).
		inline std::optional<std::string> ParseRules(const nlohmann::json& a_json, std::size_t a_position, bool a_required, SituationRules::Rules& a_rules)
		{
			if (!ReadOptional(a_json, "applyInCombat", a_rules.applyInCombat)) {
				return std::format("Situation {} has an applyInCombat that is not true or false.", a_position);
			}
			const auto conditions = a_json.find("conditions");
			if (conditions == a_json.end()) {
				return a_required ? std::optional{ std::format("Situation {} has no conditions.", a_position) } : std::nullopt;
			}
			if (auto problem = SituationRules::ParseRows(*conditions, a_rules.rows)) {
				return std::format("Situation {}: {}", a_position, *problem);
			}
			return std::nullopt;
		}

		// A preset's entry: its name and removed flag; in version 2 its rules too, and its place in
		// the order, the Default's excepted.
		inline std::optional<std::string> ParsePreset(const nlohmann::json& a_json, const std::string& a_key, int a_version, std::size_t a_position,
			Table& a_table, std::vector<bool>& a_seen)
		{
			const auto situation = Situations::FromKey(a_key);
			if (!situation || IndexOf(*situation) >= Situations::kPresetCount) {
				return std::format("Situation {} has the unknown key \"{}\".", a_position, a_key);
			}
			const auto index = IndexOf(*situation);
			if (a_seen[index]) {
				return std::format("Situation {} has the key of an earlier one.", a_position);
			}
			a_seen[index] = true;
			auto& entry = a_table[index];
			if (!ReadOptional(a_json, "name", entry.name) || !ReadOptional(a_json, "removed", entry.removed)) {
				return std::format("Situation {} has a name that is not text, or a removed that is not true or false.", a_position);
			}
			if (a_version < 2 || *situation == Situations::Situation::kDefault) {
				return std::nullopt;
			}
			a_table.order.push_back(*situation);
			return ParseRules(a_json, a_position, false, entry.rules);
		}

		// A user's situation, added after those read before it. Its key's number is below the
		// file's nextKey and used once.
		inline std::optional<std::string> ParseUser(const nlohmann::json& a_json, const std::string& a_key, std::uint32_t a_number,
			std::size_t a_position, Table& a_table)
		{
			if (a_number >= a_table.nextKey) {
				return std::format("Situation {} has the key \"{}\", not below nextKey.", a_position, a_key);
			}
			for (const auto& entry : a_table) {
				if (entry.key == a_key) {
					return std::format("Situation {} has the key of an earlier one.", a_position);
				}
			}
			Entry      entry{ .key = a_key };
			const auto name = a_json.find("name");
			if (name == a_json.end() || !name->is_string()) {
				return std::format("Situation {} has no name.", a_position);
			}
			entry.name = name->get<std::string>();
			if (!ReadOptional(a_json, "removed", entry.removed)) {
				return std::format("Situation {} has a removed that is not true or false.", a_position);
			}
			if (auto problem = ParseRules(a_json, a_position, true, entry.rules)) {
				return problem;
			}
			a_table.order.push_back(static_cast<Situations::Situation>(a_table.size()));
			a_table.entries.push_back(std::move(entry));
			return std::nullopt;
		}

		inline std::optional<std::string> ParseEntry(const nlohmann::json& a_json, int a_version, std::size_t a_position, Table& a_table,
			std::vector<bool>& a_seen)
		{
			const auto key = a_json.is_object() ? a_json.find("key") : a_json.end();
			if (!a_json.is_object() || key == a_json.end() || !key->is_string()) {
				return std::format("Situation {} has no key.", a_position);
			}
			const auto text = key->get<std::string>();
			if (const auto number = UserKeyNumber(text); number && a_version >= 2) {
				return ParseUser(a_json, text, *number, a_position, a_table);
			}
			return ParsePreset(a_json, text, a_version, a_position, a_table, a_seen);
		}

		// The file's version, 1 or kVersion, and with 2 its nextKey. Returns why it cannot be
		// read, or nullopt.
		inline std::optional<std::string> ParseHeader(const nlohmann::json& a_json, int& a_version, Table& a_table)
		{
			const auto version = a_json.find("version");
			if (version == a_json.end() || !version->is_number_integer() || version->get<std::int64_t>() < 1 || version->get<std::int64_t>() > kVersion) {
				return std::format("Its version is not 1 to {}.", kVersion);
			}
			a_version = static_cast<int>(version->get<std::int64_t>());
			if (a_version >= 2) {
				const auto next = a_json.find("nextKey");
				if (next == a_json.end() || !next->is_number_unsigned() || next->get<std::uint64_t>() < 1 || next->get<std::uint64_t>() > 999'999'999) {
					return "It has no nextKey.";
				}
				a_table.nextKey = static_cast<std::uint32_t>(next->get<std::uint64_t>());
			}
			return std::nullopt;
		}

		// The order a version 2 file gives: the presets it lacks first, as installed in the
		// installed order, so they win over none of its situations, then its entries' order. A
		// hand-edited file still loads, and one written before the presets had rules (it lists only
		// those renamed or removed) keeps its own situations winning over the presets.
		inline void AddMissingPresets(Table& a_table)
		{
			std::vector<Situations::Situation> missing;
			for (const auto situation : Situations::kInstalledOrder) {
				if (std::ranges::find(a_table.order, situation) == a_table.order.end()) {
					missing.push_back(situation);
				}
			}
			a_table.order.insert(a_table.order.begin(), missing.begin(), missing.end());
		}
	}

	// Reads situations.json's text into `a_table`, from the presets: version 1 gives the presets'
	// names and removed flags in the installed order; version 2 every situation's fields in the
	// file's order (Detail::AddMissingPresets). An empty file (nothing but whitespace) is read as
	// no file: the presets. All or nothing otherwise: one entry this version cannot read (an
	// unknown key or condition, a field of another type), more situations than
	// Situations::kMaxSituations, or a name rule broken (CheckNames) makes the whole file
	// unusable, so a rewrite never drops what this version could not read; `a_table` is then the
	// presets. Returns why, as a sentence, or nullopt.
	inline std::optional<std::string> Parse(std::string_view a_text, Table& a_table)
	{
		a_table = Presets();
		if (ExampleMod::TrimWhitespace(a_text).empty()) {
			return std::nullopt;
		}
		const auto                 json = nlohmann::json::parse(a_text, nullptr, false);
		std::optional<std::string> problem;
		int                        version = 1;
		if (json.is_discarded()) {
			problem = "It is not valid JSON.";
		} else if (!json.is_object()) {
			problem = "It is not a JSON object.";
		} else if (auto header = Detail::ParseHeader(json, version, a_table)) {
			problem = std::move(header);
		} else if (const auto list = json.find("situations"); list == json.end() || !list->is_array()) {
			problem = "It has no situation list.";
		} else {
			if (version >= 2) {
				a_table.order.clear();
			}
			std::vector<bool> seen(a_table.size(), false);
			for (std::size_t i = 0; i < list->size() && !problem; ++i) {
				problem = Detail::ParseEntry((*list)[i], version, i + 1, a_table, seen);
			}
			if (!problem && a_table.size() > Situations::kMaxSituations) {
				problem = std::format("It has more than {} situations.", Situations::kMaxSituations - Situations::kPresetCount);
			}
			if (!problem) {
				problem = CheckNames(a_table, Strings::English);  // for the log; the window's save checks them as shown
			}
			if (version >= 2) {
				Detail::AddMissingPresets(a_table);
			}
		}
		if (problem) {
			a_table = Presets();
		}
		return problem;
	}

	// Whether version 1 holds `a_table` whole: no user's situation was ever made, every preset
	// has its rules as installed, and the order is the installed one.
	inline bool FitsVersion1(const Table& a_table)
	{
		if (a_table.nextKey > 1 || !std::ranges::equal(a_table.order, Situations::kInstalledOrder)) {
			return false;
		}
		for (std::size_t i = 0; i < Situations::kPresetCount && i < a_table.size(); ++i) {
			if (a_table[i].rules != PresetRules(static_cast<Situations::Situation>(i))) {
				return false;
			}
		}
		return true;
	}

	// situations.json's text: version 1 while it holds the table (FitsVersion1), the presets that
	// differ from as installed; else version 2, the window's list top to bottom (Table::order),
	// every preset and every user's situation not removed, every field written. The Default is
	// in version 1 only, when renamed.
	// Contract: dump replaces bytes that are not UTF-8 instead of throwing.
	inline std::string Serialize(const Table& a_table)
	{
		constexpr int kIndent = 2;  // spaces per level, for people reading the file

		auto list = nlohmann::ordered_json::array();
		if (FitsVersion1(a_table)) {
			for (std::size_t i = 0; i < Situations::kPresetCount && i < a_table.size(); ++i) {
				const auto situation = static_cast<Situations::Situation>(i);
				if (a_table[i] != Preset(situation)) {
					list.push_back({ { "key", Situations::Name(situation) }, { "name", a_table[i].name }, { "removed", a_table[i].removed } });
				}
			}
			nlohmann::ordered_json json{ { "version", 1 }, { "situations", std::move(list) } };
			return json.dump(kIndent, ' ', false, nlohmann::ordered_json::error_handler_t::replace) + '\n';
		}
		const auto& defaults = a_table[IndexOf(Situations::Situation::kDefault)];
		if (defaults != Preset(Situations::Situation::kDefault)) {
			list.push_back({ { "key", Situations::Name(Situations::Situation::kDefault) }, { "name", defaults.name }, { "removed", false } });
		}
		for (const auto situation : a_table.order) {
			const auto& entry = a_table[IndexOf(situation)];
			if (IsUser(entry) && entry.removed) {
				continue;
			}
			nlohmann::ordered_json json{ { "key", IsUser(entry) ? entry.key : std::string{ Situations::Name(situation) } }, { "name", entry.name } };
			if (!IsUser(entry)) {
				json["removed"] = entry.removed;
			}
			json["applyInCombat"] = entry.rules.applyInCombat;
			json["conditions"] = SituationRules::ToJson(entry.rules.rows);
			list.push_back(std::move(json));
		}
		nlohmann::ordered_json json{ { "version", kVersion }, { "nextKey", a_table.nextKey }, { "situations", std::move(list) } };
		return json.dump(kIndent, ' ', false, nlohmann::ordered_json::error_handler_t::replace) + '\n';
	}

	// `a_table` with every preset as installed: its name, removed flag and rules; the order and
	// the user's situations stay as they are.
	inline Table RestorePresets(Table a_table)
	{
		for (std::size_t i = 0; i < Situations::kPresetCount && i < a_table.size(); ++i) {
			a_table[i] = Preset(static_cast<Situations::Situation>(i));
		}
		return a_table;
	}

	// What Restore Presets changes, one line per preset that differs from as installed, the
	// Default first, each under its name now (DisplayName): "Town: restored" for a removed one,
	// else "Home: name reset", "Cold: conditions reset" or "Home: name and conditions reset".
	// Empty while every preset is as installed.
	inline std::vector<std::string> RestoreChanges(const Table& a_table)
	{
		using Strings::Text;
		std::vector<std::string> lines;
		for (std::size_t i = 0; i < Situations::kPresetCount && i < a_table.size(); ++i) {
			const auto& entry = a_table[i];
			const auto  preset = Preset(static_cast<Situations::Situation>(i));
			const bool  renamed = entry.name != preset.name;
			const bool  ruled = entry.rules != preset.rules;
			if (entry.removed) {
				lines.push_back(Strings::Format(Text::kRestoreLineRestored, DisplayName(entry, i)));
			} else if (renamed && ruled) {
				lines.push_back(Strings::Format(Text::kRestoreLineBoth, DisplayName(entry, i)));
			} else if (renamed) {
				lines.push_back(Strings::Format(Text::kRestoreLineName, DisplayName(entry, i)));
			} else if (ruled) {
				lines.push_back(Strings::Format(Text::kRestoreLineConditions, DisplayName(entry, i)));
			}
		}
		return lines;
	}
}
