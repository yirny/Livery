// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants - file location, the refusal for a file that could not be read
//   State     - the table as read, whether the file may be written
//   File      - write through a temporary file
//   Adding    - the checks and the write that Create and Duplicate share
//   Forms     - the FormIDs of the Place and Spell rows' forms in this game's load order
//   API       - Load / WasLoaded / Name / Suffix / IsRemoved / Offered / RulesOf / Apply / Create /
//               Duplicate / Remove / RestorePresets / RestoreChanges
//
// The file's format and rules are in SituationTable.h.
#include "PCH.h"

#include "SituationStore.h"

#include "PieceFile.h"
#include "SituationTable.h"
#include "Strings.h"

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
	constexpr const char* kSituationsName = "situations.json";

	// Why nothing is written while the file could not be used (g_writable).
	std::string Unreadable()
	{
		return Strings::Format(Strings::Text::kFileUnreadable, kSituationsName);
	}

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
		return PieceFile::WriteText(SituationsPath(), SituationTable::Serialize(a_table), kSituationsName);
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

	// --- Adding ---------------------------------------------------------------

	// Why a user's situation cannot be added now, or nullopt.
	std::optional<std::string> CheckRoom()
	{
		if (!g_writable) {
			return Unreadable();
		}
		if (g_table.size() >= Situations::kMaxSituations) {
			const bool removed = std::ranges::any_of(g_table, [](const SituationTable::Entry& a_entry) { return a_entry.removed && SituationTable::IsUser(a_entry); });
			return Strings::Get(removed ? Strings::Text::kTooManySituationsRestart : Strings::Text::kTooManySituations);
		}
		return std::nullopt;
	}

	// Writes `a_table`, which holds one user's situation more under `a_key` (AddUserEntry), then
	// adds it to the registry as `a_created` and gives the registry the table's order.
	// Contract: the registry changes only after the write took, and every refusal comes before
	// the write (CheckRoom, the names), so the table and the registry keep the same ids.
	std::optional<std::string> CommitAdded(SituationTable::Table a_table, std::string a_key, Situations::Situation& a_created)
	{
		if (auto problem = SituationTable::CheckNames(a_table)) {
			return problem;
		}
		if (auto problem = Commit(std::move(a_table))) {
			return problem;
		}
		a_created = Situations::AddUser(std::move(a_key)).value_or(Situations::Situation::kDefault);  // never refuses: CheckRoom checked the size
		if (!Situations::Reorder(g_table.order)) {                                                    // never: the table and the registry hold the same situations
			logger::error("The situations' order was written but not taken; it takes effect when the game is restarted");
		}
		return std::nullopt;
	}

	// --- Forms ----------------------------------------------------------------

	// Gives every place and spell of the user's situations its FormID in this game's load order
	// (SituationRules::FormRef::formID), and every spell its visible effects (effects), which the
	// poll compares. Returns how many have no FormID: their plugin is not loaded.
	// Contract: game thread, after kDataLoaded.
	std::size_t ResolveForms(SituationTable::Table& a_table)
	{
		std::size_t unloaded = 0;
		for (auto& entry : a_table) {
			for (auto& row : entry.rules.rows) {
				for (auto* forms : { &row.places, &row.spells }) {
					for (auto& form : *forms) {
						form.formID = PieceFile::FormIDOf(form.ref);
						unloaded += form.formID == 0 ? 1 : 0;
					}
				}
				for (auto& spell : row.spells) {
					spell.effects = Situations::VisibleEffectsOf(spell.formID);
				}
			}
		}
		return unloaded;
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
		if (const auto unloaded = ResolveForms(table); unloaded > 0) {
			logger::warn("situations.json: {} place(s) or spell(s) from plugins not loaded hold nowhere", unloaded);
		}
		g_table = std::move(table);
		if (!Situations::Reorder(g_table.order)) {  // never: Parse orders every situation it read, and RegisterUsers added them
			logger::error("situations.json: its order does not match the situations; they keep the installed order");
		}
		g_writable = true;
		logger::info("Loaded situations.json: {} preset(s) changed, {} situation(s) of your own", SituationTable::RestoreChanges(g_table).size(),
			g_table.size() - Situations::kPresetCount);
	}

	bool WasLoaded()
	{
		return g_writable;
	}

	std::string Name(Situations::Situation a_situation)
	{
		return SituationTable::DisplayName(EntryOf(a_situation), SituationTable::IndexOf(a_situation));
	}

	std::string Suffix(Situations::Situation a_situation)
	{
		return a_situation == Situations::Situation::kDefault ? std::string{} : " for " + SituationStore::Name(a_situation);
	}

	bool IsRemoved(Situations::Situation a_situation)
	{
		return EntryOf(a_situation).removed;
	}

	std::vector<Situations::Situation> Offered()
	{
		std::vector<Situations::Situation> offered;
		for (const auto situation : Situations::Order()) {
			if (!IsRemoved(situation)) {
				offered.push_back(situation);
			}
		}
		return offered;
	}

	const SituationRules::Rules* RulesOf(Situations::Situation a_situation)
	{
		const auto index = SituationTable::IndexOf(a_situation);
		return index > 0 && index < g_table.size() ? &g_table[index].rules : nullptr;
	}

	// Contract: every refusal is checked before the write, so a refused edit changes nothing;
	// the registry takes the order only after the write took.
	std::optional<std::string> Apply(const std::vector<Edit>& a_edits, std::span<const Situations::Situation> a_order)
	{
		if (!g_writable) {
			return Unreadable();
		}
		auto table = g_table;
		for (const auto& [situation, name, rules] : a_edits) {
			const auto index = SituationTable::IndexOf(situation);
			auto&      entry = table[index];
			if (entry.removed) {
				return Strings::Format(Strings::Text::kSituationRemoved, SituationTable::DisplayName(entry, index));
			}
			if (rules && situation == Situations::Situation::kDefault) {
				return Strings::Get(Strings::Text::kDefaultNoConditions);
			}
			entry.name = SituationTable::StoredName(index, name);
			if (rules) {
				entry.rules = *rules;
			}
		}
		if (auto problem = SituationTable::CheckNames(table)) {
			return problem;
		}
		if (!a_order.empty()) {
			const auto removed = [&table](Situations::Situation a_situation) { return table[SituationTable::IndexOf(a_situation)].removed; };
			auto       order = SituationTable::Reordered(table.order, a_order, removed);
			if (!order) {
				return Strings::Get(Strings::Text::kSituationOrderMismatch);
			}
			table.order = std::move(*order);
		}
		ResolveForms(table);
		if (auto problem = Commit(std::move(table))) {
			return problem;
		}
		if (!Situations::Reorder(g_table.order)) {  // never: the table and the registry hold the same situations
			logger::error("The situations' order was written but not taken; it takes effect when the game is restarted");
		}
		return std::nullopt;
	}

	std::optional<std::string> Create(const std::string& a_name, Situations::Situation& a_created)
	{
		if (auto problem = CheckRoom()) {
			return problem;
		}
		auto table = g_table;
		auto key = SituationTable::AddUserEntry(table, std::string(ExampleMod::TrimWhitespace(a_name)), {});
		return CommitAdded(std::move(table), std::move(key), a_created);
	}

	std::optional<std::string> Duplicate(Situations::Situation a_source, const std::string& a_name, Situations::Situation& a_created)
	{
		if (auto problem = CheckRoom()) {
			return problem;
		}
		if (a_source == Situations::Situation::kDefault) {
			return Strings::Get(Strings::Text::kDefaultNotDuplicated);
		}
		const auto& source = EntryOf(a_source);
		if (source.removed) {
			return Strings::Format(Strings::Text::kSituationRemoved, SituationStore::Name(a_source));
		}
		auto table = g_table;
		auto key = SituationTable::AddUserEntry(table, std::string(ExampleMod::TrimWhitespace(a_name)), source.rules, a_source);
		return CommitAdded(std::move(table), std::move(key), a_created);
	}

	std::optional<std::string> Remove(Situations::Situation a_situation)
	{
		if (!g_writable) {
			return Unreadable();
		}
		if (a_situation == Situations::Situation::kDefault) {
			return Strings::Get(Strings::Text::kDefaultNotRemoved);
		}
		auto  table = g_table;
		auto& entry = table[SituationTable::IndexOf(a_situation)];
		if (entry.removed) {
			return Strings::Format(Strings::Text::kSituationRemovedAlready, SituationStore::Name(a_situation));
		}
		entry.removed = true;
		return Commit(std::move(table));
	}

	std::optional<std::string> RestorePresets()
	{
		if (!g_writable) {
			return Unreadable();
		}
		return Commit(SituationTable::RestorePresets(g_table));
	}

	std::vector<std::string> RestoreChanges()
	{
		return SituationTable::RestoreChanges(g_table);
	}
}
