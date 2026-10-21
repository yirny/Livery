// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Situations.h"
#include "Utils.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// The situations' names, descriptions and removed flags as situations.json keeps them
// (docs/situation-management-ui.md §2), and the rules that need no game: the presets, the
// file's parse and write, the name rules, Restore Presets' change lines. SituationStore
// keeps the table and writes the file; the unit tests reach this header.
//
// situations.json, version 1, only the situations that differ from the presets:
//   { "version": 1, "situations": [ { "key": "Town", "name": "City",
//     "description": "in the city", "removed": false } ] }
namespace SituationTable
{
	inline constexpr int kVersion = 1;  // the "version" field this code reads and writes

	struct Entry
	{
		std::string name;         // shown wherever the window and the HUD name the situation
		std::string description;  // ends the sentences that say what is worn; "" adds nothing
		bool        removed = false;

		friend bool operator==(const Entry&, const Entry&) = default;
	};

	// One entry per Situations::Situation, at the index of its value: the Default first.
	inline constexpr std::size_t kCount = Situations::kChoosable.size() + 1;
	using Table = std::array<Entry, kCount>;

	constexpr std::size_t IndexOf(Situations::Situation a_situation) noexcept
	{
		return static_cast<std::size_t>(a_situation);
	}

	// As Livery installs it: named as its key, described for the sentences; the Default has
	// no description, since it is worn in towns without a Town outfit and in fights too.
	inline Entry Preset(Situations::Situation a_situation)
	{
		using Situations::Situation;
		const char* description = "";
		switch (a_situation) {
		case Situation::kTown:
			description = "in Town";
			break;
		case Situation::kHome:
			description = "at Home";
			break;
		case Situation::kSleep:
			description = "ready for Sleep";
			break;
		case Situation::kSwimming:
			description = "swimming";
			break;
		case Situation::kWarm:
			description = "keeping Warm";
			break;
		default:
			break;
		}
		return { .name = Situations::Name(a_situation), .description = description };
	}

	inline Table Presets()
	{
		Table table;
		for (std::size_t i = 0; i < table.size(); ++i) {
			table[i] = Preset(static_cast<Situations::Situation>(i));
		}
		return table;
	}

	// ", in Town" for the end of a sentence; "" for an empty description.
	inline std::string Suffix(const Entry& a_entry)
	{
		return a_entry.description.empty() ? std::string{} : ", " + a_entry.description;
	}

	// Why the names break the rules, or nullopt: every name has text, the Default keeps its
	// name and is never removed, and no two situations not removed share a name (ASCII case
	// ignored). Sentences, for the window and the log.
	inline std::optional<std::string> CheckNames(const Table& a_table)
	{
		const auto& defaultEntry = a_table[IndexOf(Situations::Situation::kDefault)];
		if (defaultEntry.removed || defaultEntry.name != Preset(Situations::Situation::kDefault).name) {
			return "The Default cannot be renamed or removed.";
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
		inline std::optional<Situations::Situation> SituationOf(std::string_view a_key)
		{
			for (std::size_t i = 0; i < kCount; ++i) {
				const auto situation = static_cast<Situations::Situation>(i);
				if (a_key == Situations::Name(situation)) {
					return situation;
				}
			}
			return std::nullopt;
		}

		// Reads one field the entry may leave out (the preset's value stays). Returns false
		// when it is there with another type.
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

		inline std::optional<std::string> ParseEntry(const nlohmann::json& a_json, std::size_t a_position, Table& a_table, std::vector<bool>& a_seen)
		{
			const auto key = a_json.is_object() ? a_json.find("key") : a_json.end();
			if (!a_json.is_object() || key == a_json.end() || !key->is_string()) {
				return std::format("Situation {} has no key.", a_position);
			}
			const auto situation = SituationOf(key->get<std::string>());
			if (!situation) {
				return std::format("Situation {} has the unknown key \"{}\".", a_position, key->get<std::string>());
			}
			const auto index = IndexOf(*situation);
			if (a_seen[index]) {
				return std::format("Situation {} has the key of an earlier one.", a_position);
			}
			a_seen[index] = true;
			auto& entry = a_table[index];
			if (!ReadOptional(a_json, "name", entry.name) || !ReadOptional(a_json, "description", entry.description) ||
				!ReadOptional(a_json, "removed", entry.removed)) {
				return std::format("Situation {} has a name or description that is not text, or a removed that is not true or false.", a_position);
			}
			return std::nullopt;
		}
	}

	// Reads situations.json's text into `a_table`, from the presets: an entry replaces the
	// fields it has of its situation's preset. All or nothing: one entry this version cannot
	// read (an unknown key, a field of another type) or a name rule broken (CheckNames)
	// makes the whole file unusable, so a rewrite never drops what this version could not
	// read; `a_table` is then the presets. Returns why, as a sentence, or nullopt.
	inline std::optional<std::string> Parse(std::string_view a_text, Table& a_table)
	{
		a_table = Presets();
		const auto                 json = nlohmann::json::parse(a_text, nullptr, false);
		std::optional<std::string> problem;
		if (json.is_discarded()) {
			problem = "It is not valid JSON.";
		} else if (!json.is_object()) {
			problem = "It is not a JSON object.";
		} else if (const auto version = json.find("version"); version == json.end() || !version->is_number_integer() || version->get<std::int64_t>() != kVersion) {
			problem = std::format("Its version is not {}.", kVersion);
		} else if (const auto list = json.find("situations"); list == json.end() || !list->is_array()) {
			problem = "It has no situation list.";
		} else {
			std::vector<bool> seen(kCount, false);
			for (std::size_t i = 0; i < list->size() && !problem; ++i) {
				problem = Detail::ParseEntry((*list)[i], i + 1, a_table, seen);
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

	// situations.json's text: the entries that differ from the presets, every field written.
	// Contract: dump replaces bytes that are not UTF-8 instead of throwing.
	inline std::string Serialize(const Table& a_table)
	{
		constexpr int kIndent = 2;  // spaces per level, for people reading the file

		auto list = nlohmann::ordered_json::array();
		for (std::size_t i = 0; i < a_table.size(); ++i) {
			const auto  situation = static_cast<Situations::Situation>(i);
			const auto& entry = a_table[i];
			if (entry != Preset(situation)) {
				list.push_back({ { "key", Situations::Name(situation) }, { "name", entry.name }, { "description", entry.description }, { "removed", entry.removed } });
			}
		}
		const nlohmann::ordered_json json{ { "version", kVersion }, { "situations", std::move(list) } };
		return json.dump(kIndent, ' ', false, nlohmann::ordered_json::error_handler_t::replace) + '\n';
	}

	// What Restore Presets changes, one line per situation that differs from its preset, the
	// Default first, each under its name now: "Town: restored" for a removed one, else "Home:
	// name reset", "Warm: name and description reset" or "Sleep: description reset". Empty
	// while every preset is as installed.
	inline std::vector<std::string> RestoreChanges(const Table& a_table)
	{
		std::vector<std::string> lines;
		for (std::size_t i = 0; i < a_table.size(); ++i) {
			const auto& entry = a_table[i];
			const auto  preset = Preset(static_cast<Situations::Situation>(i));
			const bool  renamed = entry.name != preset.name;
			const bool  redescribed = entry.description != preset.description;
			if (entry.removed) {
				lines.push_back(entry.name + ": restored");
			} else if (renamed && redescribed) {
				lines.push_back(entry.name + ": name and description reset");
			} else if (renamed) {
				lines.push_back(entry.name + ": name reset");
			} else if (redescribed) {
				lines.push_back(entry.name + ": description reset");
			}
		}
		return lines;
	}
}
