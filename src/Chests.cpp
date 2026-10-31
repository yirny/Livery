// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants - file location and version
//   State     - the chests as read, the next id, whether the file may be written
//   File      - parse (all or nothing), serialise; the piece and file helpers are PieceFile's
//   API       - Load / All / Find / FindByName / LoadedPieces / Create / Update / Remove
//
// chests.json, version 1:
//   { "version": 1, "nextId": 2, "chests": [ { "id": 1, "name": "Visors",
//     "pieces": [ { "plugin": "Skyrim.esm", "formId": "0x013921",
//     "name": "Imperial Light Armor" } ] } ] }
// "pieces" may be empty. The file is read whether or not the Item chests setting is on, so
// turning it on needs no restart; it is never written while the setting is off, as the window
// then has no chest page.
#include "PCH.h"

#include "Chests.h"

#include "PieceFile.h"
#include "Utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <utility>

namespace
{
	using PieceFile::Field;
	using PieceFile::ReadId;
	using PieceFile::ReadString;

	// --- Constants ------------------------------------------------------------

	constexpr auto        kChestsFile = L"Data/SKSE/Plugins/Livery/chests.json";
	constexpr auto        kChestsName = "chests.json";  // in the log and the messages
	constexpr int         kChestsVersion = 1;           // the "version" field this code reads and writes
	constexpr const char* kUnreadable = "Livery could not read chests.json, so it changes nothing (see Livery.log).";

	// --- State ----------------------------------------------------------------

	// Contract: game thread only. g_writable stays false until Load, and for good when
	// the file exists but could not be used.
	std::vector<Chests::Chest> g_chests;
	std::uint32_t              g_nextId = PieceFile::kFirstId;
	bool                       g_writable = false;

	std::filesystem::path ChestsPath()
	{
		return std::filesystem::current_path() / kChestsFile;
	}

	// --- File -----------------------------------------------------------------

	std::optional<std::string> ParseChest(const nlohmann::json& a_json, Chests::Chest& a_chest)
	{
		if (!a_json.is_object()) {
			return "not an object";
		}
		if (!ReadId(Field(a_json, "id"), a_chest.id)) {
			return "no id from 1 up";
		}
		if (!ReadString(Field(a_json, "name"), a_chest.name)) {
			return "no name";
		}
		const auto* pieces = Field(a_json, "pieces");
		if (!pieces || !pieces->is_array()) {
			return "no piece list";
		}
		for (const auto& piece : *pieces) {
			if (auto problem = PieceFile::ParsePiece(piece, a_chest.pieces.emplace_back())) {
				return problem;
			}
		}
		return std::nullopt;
	}

	// All or nothing: one bad chest makes the whole file unusable, so a rewrite never drops
	// what this version could not read. Returns why, or nullopt.
	std::optional<std::string> Parse(const nlohmann::json& a_json, std::vector<Chests::Chest>& a_chests, std::uint32_t& a_nextId)
	{
		if (!a_json.is_object()) {
			return "it is not a JSON object";
		}
		const auto* version = Field(a_json, "version");
		if (!version || !version->is_number_integer() || version->get<std::int64_t>() != kChestsVersion) {
			return std::format("its version is not {}", kChestsVersion);
		}
		const auto* chests = Field(a_json, "chests");
		if (!ReadId(Field(a_json, "nextId"), a_nextId) || !chests || !chests->is_array()) {
			return "it has no nextId or no chest list";
		}
		for (std::size_t i = 0; i < chests->size(); ++i) {
			Chests::Chest chest;
			if (const auto problem = ParseChest((*chests)[i], chest)) {
				return std::format("chest {} has {}", i + 1, *problem);
			}
			if (std::ranges::find(a_chests, chest.id, &Chests::Chest::id) != a_chests.end()) {
				return std::format("chest {} has the id of an earlier one", i + 1);
			}
			// An id at or past nextId (an edited file) moves nextId on, so no id is given twice.
			a_nextId = (std::max)(a_nextId, chest.id + 1);
			a_chests.push_back(std::move(chest));
		}
		return std::nullopt;
	}

	nlohmann::ordered_json ToJson(const std::vector<Chests::Chest>& a_chests, std::uint32_t a_nextId)
	{
		auto list = nlohmann::ordered_json::array();
		for (const auto& chest : a_chests) {
			auto pieces = nlohmann::ordered_json::array();
			for (const auto& piece : chest.pieces) {
				pieces.push_back(PieceFile::ToJson(piece));
			}
			list.push_back({ { "id", chest.id }, { "name", chest.name }, { "pieces", std::move(pieces) } });
		}
		return { { "version", kChestsVersion }, { "nextId", a_nextId }, { "chests", std::move(list) } };
	}

	std::optional<std::string> WriteChests(const std::vector<Chests::Chest>& a_chests, std::uint32_t a_nextId)
	{
		return PieceFile::WriteJson(ChestsPath(), ToJson(a_chests, a_nextId), kChestsName);
	}
}

namespace Chests
{
	void Load()
	{
		g_chests.clear();
		g_nextId = PieceFile::kFirstId;
		g_writable = false;

		std::string text;
		switch (PieceFile::ReadText(ChestsPath(), text)) {
		case PieceFile::FileState::kUnreadable:
			logger::error("Cannot open chests.json; the chests stay empty and are not written");
			return;
		case PieceFile::FileState::kMissing:
			g_writable = true;
			logger::info("No chests.json yet; no chests");
			return;
		case PieceFile::FileState::kEmpty:
			g_writable = true;
			logger::info("chests.json is empty; no chests");
			return;
		case PieceFile::FileState::kText:
			break;
		}
		const auto         json = nlohmann::json::parse(text, nullptr, false);
		std::vector<Chest> chests;
		std::uint32_t      nextId = PieceFile::kFirstId;
		const auto         problem = json.is_discarded() ? std::make_optional<std::string>("it is not valid JSON") : Parse(json, chests, nextId);
		if (problem) {
			logger::error("chests.json cannot be used ({}); the chests stay empty and are not written", *problem);
			return;
		}
		g_chests = std::move(chests);
		g_nextId = nextId;
		g_writable = true;
		logger::info("Loaded {} chest(s) from chests.json", g_chests.size());
	}

	const std::vector<Chest>& All()
	{
		return g_chests;
	}

	const Chest* Find(std::uint32_t a_id)
	{
		const auto found = std::ranges::find(g_chests, a_id, &Chest::id);
		return found != g_chests.end() ? &*found : nullptr;
	}

	const Chest* FindByName(std::string_view a_name)
	{
		const auto found = std::ranges::find_if(g_chests, [a_name](const Chest& chest) { return ExampleMod::CaseInsensitiveEqual(chest.name, a_name); });
		return found != g_chests.end() ? &*found : nullptr;
	}

	std::vector<RE::TESObjectARMO*> LoadedPieces(const Chest& a_chest, std::size_t* a_unloaded)
	{
		return PieceFile::LoadedArmor(a_chest.pieces, a_unloaded);
	}

	// Contract: every refusal is checked before anything changes.
	std::optional<std::string> Create(std::string_view a_name)
	{
		if (!g_writable) {
			return kUnreadable;
		}
		if (a_name.empty()) {
			return "Enter a name.";
		}
		if (FindByName(a_name)) {
			return std::format("A chest named \"{}\" already exists.", a_name);
		}
		auto chests = g_chests;
		chests.push_back({ .id = g_nextId, .name = std::string{ a_name } });
		if (auto problem = WriteChests(chests, g_nextId + 1)) {
			return problem;
		}
		g_chests = std::move(chests);
		++g_nextId;
		return std::nullopt;
	}

	// Contract: as Create; the new pieces and the name are checked before anything changes.
	std::optional<std::string> Update(std::uint32_t a_id, std::string_view a_name, const std::vector<RE::TESObjectARMO*>& a_armor)
	{
		if (!g_writable) {
			return kUnreadable;
		}
		const auto found = std::ranges::find(g_chests, a_id, &Chest::id);
		if (found == g_chests.end()) {
			return "The chest no longer exists.";
		}
		if (a_name.empty()) {
			return "Enter a name.";
		}
		// Its own name, in another case, is free to take.
		if (const auto* named = FindByName(a_name); named && named->id != a_id) {
			return std::format("A chest named \"{}\" already exists.", a_name);
		}
		std::vector<PieceRef> pieces;
		if (auto problem = PieceFile::ToPieces(a_armor, pieces)) {
			return problem;
		}
		Chest updated = *found;
		updated.name = std::string{ a_name };
		// This load order cannot tell what a piece of a missing plugin was, so it stays.
		std::erase_if(updated.pieces, [](const PieceRef& piece) { return PieceFile::ArmorOf(piece) != nullptr; });
		updated.pieces.insert(updated.pieces.begin(), pieces.begin(), pieces.end());

		auto chests = g_chests;
		chests[static_cast<std::size_t>(found - g_chests.begin())] = std::move(updated);
		if (auto problem = WriteChests(chests, g_nextId)) {
			return problem;
		}
		g_chests = std::move(chests);
		return std::nullopt;
	}

	// Contract: as Create; nextId stays, so the id is never reused.
	std::optional<std::string> Remove(std::uint32_t a_id)
	{
		if (!g_writable) {
			return kUnreadable;
		}
		auto chests = g_chests;
		if (std::erase_if(chests, [a_id](const Chest& a_chest) { return a_chest.id == a_id; }) == 0) {
			return "The chest no longer exists.";
		}
		if (auto problem = WriteChests(chests, g_nextId)) {
			return problem;
		}
		g_chests = std::move(chests);
		return std::nullopt;
	}
}
