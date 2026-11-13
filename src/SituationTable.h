// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SituationRules.h"
#include "Situations.h"
#include "Utils.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// The situations as situations.json keeps them (docs/situation-management-ui.md §2,
// docs/custom-situations.md §6): the presets' names and removed flags, the user's situations
// with their rules, and the rules that need no game: the presets, the file's parse and write,
// the name rules, Restore Presets' change lines. SituationStore keeps the table and writes the
// file; the unit tests reach this header.
//
// situations.json holds the presets that differ from as installed and every user's situation
// not removed; an entry's other fields are ignored. Version 1 has presets only and is written
// while no user's situation was ever made; version 2 adds `nextKey` and the user's:
//   { "version": 2, "nextKey": 3, "situations": [ { "key": "Town", "name": "City", "removed": false },
//     { "key": "user:2", "name": "Rainy town", "applyInCombat": false, "conditions": [ ... ] } ] }
// (the conditions: SituationRules::ToJson).
namespace SituationTable
{
	inline constexpr int kVersion = 2;  // the newest "version" this code reads and writes; 1 is read too

	struct Entry
	{
		std::string           name;  // shown wherever the window and the HUD name the situation
		bool                  removed = false;
		std::string           key;    // a user's situation's (`user:<n>`); "" for a preset, keyed by Situations::Name
		SituationRules::Rules rules;  // a user's situation's

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
		std::uint32_t      nextKey = 1;  // the number the next user's situation's key takes (UserKey); never reused

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

	// As Livery installs it: named as its key.
	inline Entry Preset(Situations::Situation a_situation)
	{
		return { .name = Situations::Name(a_situation) };
	}

	inline Table Presets()
	{
		Table table;
		table.entries.resize(Situations::kPresetCount);
		for (std::size_t i = 0; i < table.size(); ++i) {
			table[i] = Preset(static_cast<Situations::Situation>(i));
		}
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

	// Why the names break the rules, or nullopt: every name has text, the Default is never
	// removed (it may be renamed), and no two situations not removed share a name (ASCII case
	// ignored). Sentences, for the window and the log.
	inline std::optional<std::string> CheckNames(const Table& a_table)
	{
		if (a_table[IndexOf(Situations::Situation::kDefault)].removed) {
			return "The default outfit cannot be removed.";
		}
		for (std::size_t i = 0; i < a_table.size(); ++i) {
			if (ExampleMod::TrimWhitespace(a_table[i].name).empty()) {
				return "Enter a name for every situation.";
			}
			if (a_table[i].removed) {
				continue;
			}
			for (std::size_t j = i + 1; j < a_table.size(); ++j) {
				if (!a_table[j].removed && ExampleMod::CaseInsensitiveEqual(a_table[i].name, a_table[j].name)) {
					return std::format("Two situations are named \"{}\".", a_table[j].name);
				}
			}
		}
		return std::nullopt;
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

		inline std::optional<std::string> ParsePreset(const nlohmann::json& a_json, const std::string& a_key, std::size_t a_position, Table& a_table,
			std::vector<bool>& a_seen)
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
			return std::nullopt;
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
			if (!ReadOptional(a_json, "removed", entry.removed) || !ReadOptional(a_json, "applyInCombat", entry.rules.applyInCombat)) {
				return std::format("Situation {} has a removed or applyInCombat that is not true or false.", a_position);
			}
			const auto conditions = a_json.find("conditions");
			if (conditions == a_json.end()) {
				return std::format("Situation {} has no conditions.", a_position);
			}
			if (auto problem = SituationRules::ParseRows(*conditions, entry.rules.rows)) {
				return std::format("Situation {}: {}", a_position, *problem);
			}
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
			return ParsePreset(a_json, text, a_position, a_table, a_seen);
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
	}

	// Reads situations.json's text into `a_table`, from the presets: a preset's entry replaces
	// the fields it has, a user's situation is added after the presets in the file's order. An
	// empty file (nothing but whitespace) is read as no file: the presets. All or nothing
	// otherwise: one entry this version cannot read (an unknown key or condition, a field of
	// another type), more situations than Situations::kMaxSituations, or a name rule broken
	// (CheckNames) makes the whole file unusable, so a rewrite never drops what this version
	// could not read; `a_table` is then the presets. Returns why, as a sentence, or nullopt.
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
			std::vector<bool> seen(a_table.size(), false);
			for (std::size_t i = 0; i < list->size() && !problem; ++i) {
				problem = Detail::ParseEntry((*list)[i], version, i + 1, a_table, seen);
			}
			if (!problem && a_table.size() > Situations::kMaxSituations) {
				problem = std::format("It has more than {} situations.", Situations::kMaxSituations - Situations::kPresetCount);
			}
			if (!problem) {
				problem = CheckNames(a_table);
			}
		}
		if (problem) {
			a_table = Presets();
		}
		return problem;
	}

	// situations.json's text: the presets that differ from as installed, then the user's
	// situations not removed, every field written. Version 1 while no user's situation was ever
	// made (nextKey 1), so a Livery without them can still read it.
	// Contract: dump replaces bytes that are not UTF-8 instead of throwing.
	inline std::string Serialize(const Table& a_table)
	{
		constexpr int kIndent = 2;  // spaces per level, for people reading the file

		auto list = nlohmann::ordered_json::array();
		for (std::size_t i = 0; i < a_table.size(); ++i) {
			const auto& entry = a_table[i];
			if (IsUser(entry)) {
				if (!entry.removed) {
					list.push_back({ { "key", entry.key },
						{ "name", entry.name },
						{ "applyInCombat", entry.rules.applyInCombat },
						{ "conditions", SituationRules::ToJson(entry.rules.rows) } });
				}
				continue;
			}
			const auto situation = static_cast<Situations::Situation>(i);
			if (entry != Preset(situation)) {
				list.push_back({ { "key", Situations::Name(situation) }, { "name", entry.name }, { "removed", entry.removed } });
			}
		}
		nlohmann::ordered_json json;
		if (a_table.nextKey > 1) {
			json = { { "version", kVersion }, { "nextKey", a_table.nextKey }, { "situations", std::move(list) } };
		} else {
			json = { { "version", 1 }, { "situations", std::move(list) } };
		}
		return json.dump(kIndent, ' ', false, nlohmann::ordered_json::error_handler_t::replace) + '\n';
	}

	// `a_table` with every preset as installed; the user's situations stay as they are.
	inline Table RestorePresets(Table a_table)
	{
		for (std::size_t i = 0; i < Situations::kPresetCount && i < a_table.size(); ++i) {
			a_table[i] = Preset(static_cast<Situations::Situation>(i));
		}
		return a_table;
	}

	// What Restore Presets changes, one line per preset that differs from as installed, the
	// Default first, each under its name now: "Town: restored" for a removed one, else "Home:
	// name reset". Empty while every preset is as installed.
	inline std::vector<std::string> RestoreChanges(const Table& a_table)
	{
		std::vector<std::string> lines;
		for (std::size_t i = 0; i < Situations::kPresetCount && i < a_table.size(); ++i) {
			const auto& entry = a_table[i];
			if (entry.removed) {
				lines.push_back(entry.name + ": restored");
			} else if (entry.name != Preset(static_cast<Situations::Situation>(i)).name) {
				lines.push_back(entry.name + ": name reset");
			}
		}
		return lines;
	}
}
