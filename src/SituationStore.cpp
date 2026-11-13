// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants - file location, the refusal for a file that could not be read
//   State     - the table as read, whether the file may be written
//   File      - write through a temporary file
//   API       - Load / Name / Suffix / IsRemoved / Offered / RulesOf / Apply / Create /
//               Remove / RestorePresets / RestoreChanges
//
// The file's format and rules are in SituationTable.h.
#include "PCH.h"

#include "SituationStore.h"

#include "PieceFile.h"
#include "SituationTable.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
	// --- Constants ------------------------------------------------------------

	constexpr auto        kSituationsFile = L"Data/SKSE/Plugins/Livery/situations.json";
	constexpr const char* kUnreadable = "Livery could not read situations.json, so it changes nothing (see Livery.log).";

	// --- State ----------------------------------------------------------------

	// Contract: game thread only. g_writable stays false until Load, and for good when the
	// file exists but could not be used.
	SituationTable::Table g_table = SituationTable::Presets();
	bool                  g_writable = false;

	const SituationTable::Entry& EntryOf(Situations::Situation a_situation)
	{
		return g_table[SituationTable::IndexOf(a_situation)];
	}

	// --- File -----------------------------------------------------------------

	std::filesystem::path SituationsPath()
	{
		return std::filesystem::current_path() / kSituationsFile;
	}

	// Writes through a temporary file (PieceFile::WriteText), so a crash mid-write leaves the
	// old file whole. Returns why it failed, or nullopt.
	std::optional<std::string> WriteSituations(const SituationTable::Table& a_table)
	{
		return PieceFile::WriteText(SituationsPath(), SituationTable::Serialize(a_table), "situations.json");
	}

	// Writes `a_table`, and only once it took makes it the store's. Returns why it refused.
	std::optional<std::string> Commit(SituationTable::Table a_table)
	{
		if (auto problem = WriteSituations(a_table)) {
			return problem;
		}
		g_table = std::move(a_table);
		return std::nullopt;
	}

	// Adds the table's user's situations to the registry, which holds the presets alone, so
	// each takes the id of its entry. Returns false, having added none, when the registry
	// holds more than the presets or the table more than it can hold.
	// Contract: Load only, once.
	bool RegisterUsers(const SituationTable::Table& a_table)
	{
		if (Situations::Count() != Situations::kPresetCount || a_table.size() > Situations::kMaxSituations) {
			return false;
		}
		for (std::size_t i = Situations::kPresetCount; i < a_table.size(); ++i) {
			Situations::AddUser(a_table[i].key);  // cannot refuse: the size was checked
		}
		return true;
	}
}

namespace SituationStore
{
	void Load()
	{
		g_table = SituationTable::Presets();
		g_writable = false;

		const auto    path = SituationsPath();
		std::ifstream file{ path, std::ios::binary };
		if (!file) {
			std::error_code error;
			if (std::filesystem::exists(path, error) || error) {
				logger::error("Cannot open situations.json; every situation stays as installed and the file is not written");
				return;
			}
			g_writable = true;
			logger::info("No situations.json yet; every situation is as installed");
			return;
		}
		const std::string     text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
		SituationTable::Table table;
		if (const auto problem = SituationTable::Parse(text, table)) {
			logger::error("situations.json cannot be used: {} Every situation stays as installed and the file is not written.", *problem);
			return;
		}
		if (!RegisterUsers(table)) {
			logger::error("situations.json was loaded twice; every situation stays as it was and the file is not written");
			return;
		}
		g_table = std::move(table);
		g_writable = true;
		logger::info("Loaded situations.json: {} preset(s) changed, {} situation(s) of your own", SituationTable::RestoreChanges(g_table).size(),
			g_table.size() - Situations::kPresetCount);
	}

	const std::string& Name(Situations::Situation a_situation)
	{
		return EntryOf(a_situation).name;
	}

	std::string Suffix(Situations::Situation a_situation)
	{
		return a_situation == Situations::Situation::kDefault ? std::string{} : " for " + EntryOf(a_situation).name;
	}

	bool IsRemoved(Situations::Situation a_situation)
	{
		return EntryOf(a_situation).removed;
	}

	std::vector<Situations::Situation> Offered()
	{
		std::vector<Situations::Situation> offered;
		for (const auto situation : Situations::Choosable()) {
			if (!IsRemoved(situation)) {
				offered.push_back(situation);
			}
		}
		return offered;
	}

	const SituationRules::Rules* RulesOf(Situations::Situation a_situation)
	{
		const auto index = SituationTable::IndexOf(a_situation);
		return index < g_table.size() && SituationTable::IsUser(g_table[index]) ? &g_table[index].rules : nullptr;
	}

	// Contract: every refusal is checked before the write, so a refused edit changes nothing.
	std::optional<std::string> Apply(const std::vector<Edit>& a_edits)
	{
		if (!g_writable) {
			return kUnreadable;
		}
		auto table = g_table;
		for (const auto& [situation, name, rules] : a_edits) {
			auto& entry = table[SituationTable::IndexOf(situation)];
			if (entry.removed) {
				return std::format("\"{}\" has been removed.", entry.name);
			}
			if (rules && !SituationTable::IsUser(entry)) {
				return std::format("The conditions of \"{}\" cannot be changed.", entry.name);
			}
			entry.name = name;
			if (rules) {
				entry.rules = *rules;
			}
		}
		if (auto problem = SituationTable::CheckNames(table)) {
			return problem;
		}
		return Commit(std::move(table));
	}

	// Contract: the registry adds the situation only after the write took, and every refusal
	// comes before the write, so the table and the registry keep the same ids.
	std::optional<std::string> Create(const std::string& a_name, Situations::Situation& a_created)
	{
		if (!g_writable) {
			return kUnreadable;
		}
		if (g_table.size() >= Situations::kMaxSituations) {
			const bool removed = std::ranges::any_of(g_table, [](const SituationTable::Entry& a_entry) { return a_entry.removed && SituationTable::IsUser(a_entry); });
			return removed ? "Livery cannot hold more situations; removed ones still count until the game is restarted." : "Livery cannot hold more situations.";
		}
		auto       table = g_table;
		const auto key = SituationTable::UserKey(table.nextKey++);
		table.entries.push_back({ .name = std::string(ExampleMod::TrimWhitespace(a_name)), .key = key });
		if (auto problem = SituationTable::CheckNames(table)) {
			return problem;
		}
		if (auto problem = Commit(std::move(table))) {
			return problem;
		}
		a_created = Situations::AddUser(key).value_or(Situations::Situation::kDefault);  // never refuses: the size was checked
		return std::nullopt;
	}

	std::optional<std::string> Remove(Situations::Situation a_situation)
	{
		if (!g_writable) {
			return kUnreadable;
		}
		if (a_situation == Situations::Situation::kDefault) {
			return "The default outfit cannot be removed.";
		}
		auto  table = g_table;
		auto& entry = table[SituationTable::IndexOf(a_situation)];
		if (entry.removed) {
			return std::format("\"{}\" has been removed already.", entry.name);
		}
		entry.removed = true;
		return Commit(std::move(table));
	}

	std::optional<std::string> RestorePresets()
	{
		if (!g_writable) {
			return kUnreadable;
		}
		return Commit(SituationTable::RestorePresets(g_table));
	}

	std::vector<std::string> RestoreChanges()
	{
		return SituationTable::RestoreChanges(g_table);
	}
}
