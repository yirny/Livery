// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants - file location, the refusal for a file that could not be read
//   State     - the table as read, whether the file may be written
//   File      - write through a temporary file
//   API       - Load / Name / Suffix / IsRemoved / Offered / Apply / Remove /
//               RestorePresets / RestoreChanges
//
// The file's format and rules are in SituationTable.h.
#include "PCH.h"

#include "SituationStore.h"

#include "SituationTable.h"

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

	// Writes a temporary file and moves it over situations.json, so a crash mid-write leaves
	// the old file whole. Returns why it failed, or nullopt.
	std::optional<std::string> WriteSituations(const SituationTable::Table& a_table)
	{
		const auto path = SituationsPath();
		auto       temporary = path;
		temporary += L".tmp";
		{
			std::ofstream file{ temporary, std::ios::binary | std::ios::trunc };
			file << SituationTable::Serialize(a_table);
			file.close();
			if (!file) {
				logger::error("Cannot write situations.json.tmp");
				return "Livery could not write situations.json (see Livery.log).";
			}
		}
		std::error_code error;
		std::filesystem::rename(temporary, path, error);
		if (error) {
			logger::error("Cannot replace situations.json: {}", error.message());
			std::filesystem::remove(temporary, error);
			return "Livery could not write situations.json (see Livery.log).";
		}
		return std::nullopt;
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
		const std::string        text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
		SituationTable::Table    table;
		std::vector<std::string> retired;
		if (const auto problem = SituationTable::Parse(text, table, &retired)) {
			logger::error("situations.json cannot be used: {} Every situation stays as installed and the file is not written.", *problem);
			return;
		}
		for (const auto& key : retired) {
			logger::info("situations.json: dropped the entry of {}, a situation this version no longer has", key);
		}
		g_table = std::move(table);
		g_writable = true;
		logger::info("Loaded situations.json: {} situation(s) differ from the presets", SituationTable::RestoreChanges(g_table).size());
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
		for (const auto situation : Situations::kChoosable) {
			if (!IsRemoved(situation)) {
				offered.push_back(situation);
			}
		}
		return offered;
	}

	// Contract: every refusal is checked before the write, so a refused edit changes nothing.
	std::optional<std::string> Apply(const std::vector<Edit>& a_edits)
	{
		if (!g_writable) {
			return kUnreadable;
		}
		auto table = g_table;
		for (const auto& [situation, name] : a_edits) {
			auto& entry = table[SituationTable::IndexOf(situation)];
			if (entry.removed) {
				return std::format("\"{}\" has been removed.", entry.name);
			}
			entry.name = name;
		}
		if (auto problem = SituationTable::CheckNames(table)) {
			return problem;
		}
		return Commit(std::move(table));
	}

	std::optional<std::string> Remove(Situations::Situation a_situation)
	{
		if (!g_writable) {
			return kUnreadable;
		}
		if (a_situation == Situations::Situation::kDefault) {
			return "The Default cannot be removed.";
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
		return Commit(SituationTable::Presets());
	}

	std::vector<std::string> RestoreChanges()
	{
		return SituationTable::RestoreChanges(g_table);
	}
}
