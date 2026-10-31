// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants - file location and version
//   State     - the outfits as read, the next id, whether the file may be written
//   File      - parse (all or nothing), serialise; the piece and file helpers are PieceFile's
//   API       - Load / Outfits / Find / FindByName / LoadedPieces / Create / Update / Remove
//
// outfits.json, version 1:
//   { "version": 1, "nextId": 3, "outfits": [ { "id": 1, "name": "Guard",
//     "icon": "skyui/icons_item_psychosteve/armor_body", "color": "#eaeaea",
//     "pieces": [ { "plugin": "Skyrim.esm", "formId": "0x013921",
//     "name": "Imperial Light Armor" } ] } ] }
// An outfit with no "icon" or "color", or one that is not a string ("color": "#rrggbb"), gets
// the default (OutfitIcons), so files older versions wrote read as they are.
// An outfit's "categories" and "gender" in a file older versions wrote are read past and gone
// at the next write.
#include "PCH.h"

#include "Library.h"

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

	constexpr auto        kLibraryFile = L"Data/SKSE/Plugins/Livery/outfits.json";
	constexpr auto        kLibraryName = "outfits.json";  // in the log and the messages
	constexpr int         kLibraryVersion = 1;            // the "version" field this code reads and writes
	constexpr const char* kUnreadable = "Livery could not read outfits.json, so it changes nothing (see Livery.log).";

	// --- State ----------------------------------------------------------------

	// Contract: game thread only. g_writable stays false until Load, and for good when
	// the file exists but could not be used.
	std::vector<Library::Outfit> g_outfits;
	std::uint32_t                g_nextId = PieceFile::kFirstId;
	bool                         g_writable = false;

	std::filesystem::path LibraryPath()
	{
		return std::filesystem::current_path() / kLibraryFile;
	}

	// --- File -----------------------------------------------------------------

	// The outfit's icon key and colour; each that is missing or unreadable stays as `a_icon` has it.
	void ReadIcon(const nlohmann::json& a_json, OutfitIcons::Icon& a_icon)
	{
		if (std::string key; ReadString(Field(a_json, "icon"), key)) {
			// PieceIcons' keys are in ASCII lower case.
			std::ranges::transform(key, key.begin(), [](unsigned char a_c) { return static_cast<char>(a_c >= 'A' && a_c <= 'Z' ? a_c - 'A' + 'a' : a_c); });
			a_icon.key = std::move(key);
		}
		if (const auto* color = Field(a_json, "color"); color && color->is_string()) {
			if (const auto rgb = OutfitIcons::FromHex(color->get<std::string>())) {
				a_icon.rgb = *rgb;
			}
		}
	}

	std::optional<std::string> ParseOutfit(const nlohmann::json& a_json, Library::Outfit& a_outfit)
	{
		if (!a_json.is_object()) {
			return "not an object";
		}
		if (!ReadId(Field(a_json, "id"), a_outfit.id)) {
			return "no id from 1 up";
		}
		if (!ReadString(Field(a_json, "name"), a_outfit.name)) {
			return "no name";
		}
		ReadIcon(a_json, a_outfit.icon);
		const auto* pieces = Field(a_json, "pieces");
		if (!pieces || !pieces->is_array() || pieces->empty()) {
			return "no pieces";
		}
		for (const auto& piece : *pieces) {
			if (auto problem = PieceFile::ParsePiece(piece, a_outfit.pieces.emplace_back())) {
				return problem;
			}
		}
		return std::nullopt;
	}

	// All or nothing: one bad outfit makes the whole file unusable, so a rewrite never
	// drops what this version could not read. Returns why, or nullopt.
	std::optional<std::string> Parse(const nlohmann::json& a_json, std::vector<Library::Outfit>& a_outfits, std::uint32_t& a_nextId)
	{
		if (!a_json.is_object()) {
			return "it is not a JSON object";
		}
		const auto* version = Field(a_json, "version");
		if (!version || !version->is_number_integer() || version->get<std::int64_t>() != kLibraryVersion) {
			return std::format("its version is not {}", kLibraryVersion);
		}
		const auto* outfits = Field(a_json, "outfits");
		if (!ReadId(Field(a_json, "nextId"), a_nextId) || !outfits || !outfits->is_array()) {
			return "it has no nextId or no outfit list";
		}
		for (std::size_t i = 0; i < outfits->size(); ++i) {
			Library::Outfit outfit;
			if (const auto problem = ParseOutfit((*outfits)[i], outfit)) {
				return std::format("outfit {} has {}", i + 1, *problem);
			}
			if (std::ranges::find(a_outfits, outfit.id, &Library::Outfit::id) != a_outfits.end()) {
				return std::format("outfit {} has the id of an earlier one", i + 1);
			}
			// An id at or past nextId (an edited file) moves nextId on, so no id is given twice.
			a_nextId = (std::max)(a_nextId, outfit.id + 1);
			a_outfits.push_back(std::move(outfit));
		}
		return std::nullopt;
	}

	nlohmann::ordered_json ToJson(const std::vector<Library::Outfit>& a_outfits, std::uint32_t a_nextId)
	{
		auto list = nlohmann::ordered_json::array();
		for (const auto& outfit : a_outfits) {
			auto pieces = nlohmann::ordered_json::array();
			for (const auto& piece : outfit.pieces) {
				pieces.push_back(PieceFile::ToJson(piece));
			}
			list.push_back({ { "id", outfit.id }, { "name", outfit.name }, { "icon", outfit.icon.key }, { "color", OutfitIcons::ToHex(outfit.icon.rgb) },
				{ "pieces", std::move(pieces) } });
		}
		return { { "version", kLibraryVersion }, { "nextId", a_nextId }, { "outfits", std::move(list) } };
	}

	std::optional<std::string> WriteLibrary(const std::vector<Library::Outfit>& a_outfits, std::uint32_t a_nextId)
	{
		return PieceFile::WriteJson(LibraryPath(), ToJson(a_outfits, a_nextId), kLibraryName);
	}

	// The pieces an outfit may take: ToPieces' refusals, then at least one.
	std::optional<std::string> OutfitPieces(const std::vector<RE::TESObjectARMO*>& a_armor, std::vector<Library::PieceRef>& a_pieces)
	{
		if (auto problem = PieceFile::ToPieces(a_armor, a_pieces)) {
			return problem;
		}
		if (a_pieces.empty()) {
			return "Tick at least one item.";
		}
		return std::nullopt;
	}
}

namespace Library
{
	void Load()
	{
		g_outfits.clear();
		g_nextId = PieceFile::kFirstId;
		g_writable = false;

		std::string text;
		switch (PieceFile::ReadText(LibraryPath(), text)) {
		case PieceFile::FileState::kUnreadable:
			logger::error("Cannot open outfits.json; the outfit library stays empty and is not written");
			return;
		case PieceFile::FileState::kMissing:
			g_writable = true;
			logger::info("No outfits.json yet; the outfit library is empty");
			return;
		case PieceFile::FileState::kEmpty:
			// An empty file (nothing but whitespace) is read as no file: an empty library that
			// may be written, as situations.json is (SituationTable::Parse).
			g_writable = true;
			logger::info("outfits.json is empty; the outfit library is empty");
			return;
		case PieceFile::FileState::kText:
			break;
		}
		const auto          json = nlohmann::json::parse(text, nullptr, false);
		std::vector<Outfit> outfits;
		std::uint32_t       nextId = PieceFile::kFirstId;
		const auto          problem = json.is_discarded() ? std::make_optional<std::string>("it is not valid JSON") : Parse(json, outfits, nextId);
		if (problem) {
			logger::error("outfits.json cannot be used ({}); the outfit library stays empty and is not written", *problem);
			return;
		}
		g_outfits = std::move(outfits);
		g_nextId = nextId;
		g_writable = true;
		logger::info("Loaded {} outfit(s) from outfits.json", g_outfits.size());
	}

	const std::vector<Outfit>& Outfits()
	{
		return g_outfits;
	}

	const Outfit* Find(std::uint32_t a_id)
	{
		const auto found = std::ranges::find(g_outfits, a_id, &Outfit::id);
		return found != g_outfits.end() ? &*found : nullptr;
	}

	const Outfit* FindByName(std::string_view a_name)
	{
		const auto found = std::ranges::find_if(g_outfits, [a_name](const Outfit& outfit) { return ExampleMod::CaseInsensitiveEqual(outfit.name, a_name); });
		return found != g_outfits.end() ? &*found : nullptr;
	}

	std::vector<RE::TESObjectARMO*> LoadedPieces(const Outfit& a_outfit, std::size_t* a_unloaded)
	{
		return PieceFile::LoadedArmor(a_outfit.pieces, a_unloaded);
	}

	// Contract: every refusal is checked before anything changes, so no outfit is ever
	// left without pieces.
	std::optional<std::string> Create(std::string_view a_name, const std::vector<RE::TESObjectARMO*>& a_armor, const OutfitIcons::Icon& a_icon)
	{
		if (!g_writable) {
			return kUnreadable;
		}
		if (a_name.empty()) {
			return "Enter a name.";
		}
		if (FindByName(a_name)) {
			return std::format("An outfit named \"{}\" already exists.", a_name);
		}
		Outfit outfit{ .id = g_nextId, .name = std::string{ a_name }, .icon = a_icon };
		if (auto problem = OutfitPieces(a_armor, outfit.pieces)) {
			return problem;
		}
		auto outfits = g_outfits;
		outfits.push_back(std::move(outfit));
		if (auto problem = WriteLibrary(outfits, g_nextId + 1)) {
			return problem;
		}
		g_outfits = std::move(outfits);
		++g_nextId;
		return std::nullopt;
	}

	// Contract: as Create; the new pieces and the name are checked before anything changes.
	std::optional<std::string> Update(std::uint32_t a_id, std::string_view a_name, const std::vector<RE::TESObjectARMO*>& a_armor, const OutfitIcons::Icon& a_icon)
	{
		if (!g_writable) {
			return kUnreadable;
		}
		const auto found = std::ranges::find(g_outfits, a_id, &Outfit::id);
		if (found == g_outfits.end()) {
			return "The outfit no longer exists.";
		}
		if (a_name.empty()) {
			return "Enter a name.";
		}
		// Its own name, in another case, is free to take.
		if (const auto* named = FindByName(a_name); named && named->id != a_id) {
			return std::format("An outfit named \"{}\" already exists.", a_name);
		}
		std::vector<PieceRef> pieces;
		if (auto problem = OutfitPieces(a_armor, pieces)) {
			return problem;
		}
		Outfit updated = *found;
		updated.name = std::string{ a_name };
		updated.icon = a_icon;
		// This load order cannot tell what a piece of a missing plugin was, so it stays.
		std::erase_if(updated.pieces, [](const PieceRef& piece) { return PieceFile::ArmorOf(piece) != nullptr; });
		updated.pieces.insert(updated.pieces.begin(), pieces.begin(), pieces.end());

		auto outfits = g_outfits;
		outfits[static_cast<std::size_t>(found - g_outfits.begin())] = std::move(updated);
		if (auto problem = WriteLibrary(outfits, g_nextId)) {
			return problem;
		}
		g_outfits = std::move(outfits);
		return std::nullopt;
	}

	// Contract: as Create; nextId stays, so the id is never reused.
	std::optional<std::string> Remove(std::uint32_t a_id)
	{
		if (!g_writable) {
			return kUnreadable;
		}
		auto outfits = g_outfits;
		if (std::erase_if(outfits, [a_id](const Outfit& a_outfit) { return a_outfit.id == a_id; }) == 0) {
			return "The outfit no longer exists.";
		}
		if (auto problem = WriteLibrary(outfits, g_nextId)) {
			return problem;
		}
		g_outfits = std::move(outfits);
		return std::nullopt;
	}
}
