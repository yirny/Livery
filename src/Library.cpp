// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants - file location and version, the Armor Classes the window offers, old class names
//   State     - the outfits as read, the next id, whether the file may be written
//   Helpers   - armor to piece and back
//   File      - parse (all or nothing), serialise, write through a temporary file
//   API       - Load / Categories / Outfits / Find / FindByName / LoadedPieces / Create / Update
//
// outfits.json, version 1:
//   { "version": 1, "nextId": 3, "outfits": [ { "id": 1, "name": "Guard", "gender": -1,
//     "categories": [ "Light" ], "pieces": [ { "plugin": "Skyrim.esm",
//     "formId": "0x013921", "name": "Imperial Light Armor" } ] } ] }
#include "PCH.h"

#include "Library.h"

#include "ArmorInfo.h"
#include "CaptureWindow.h"
#include "Utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace
{
	// --- Constants ------------------------------------------------------------

	constexpr auto          kLibraryFile = L"Data/SKSE/Plugins/Livery/outfits.json";
	constexpr int           kLibraryVersion = 1;  // the "version" field this code reads and writes
	constexpr std::uint32_t kFirstId = 1;
	constexpr RE::FormID    kMaxLocalID = 0xFFFFFF;    // a plugin's own part of a FormID
	constexpr RE::FormID    kMaxLightLocalID = 0xFFF;  // the same for a light plugin
	constexpr const char*   kUnreadable = "Livery could not read outfits.json, so it changes nothing (see Livery.log).";

	// The Armor Classes: SkyUI's armor classes (ArmorInfo) but Jewelry, which only the piece
	// list shows. Other stored categories (older files' Adventuring, Town, ...) are custom
	// categories, which the window shows but does not edit; they stay as they are.
	using ArmorInfo::ArmorClass;
	constexpr std::array<Library::Category, 3> kCategories{ {
		{ CaptureWindow::kClassGroup, ArmorInfo::ClassName(ArmorClass::kLight) },
		{ CaptureWindow::kClassGroup, ArmorInfo::ClassName(ArmorClass::kHeavy) },
		{ CaptureWindow::kClassGroup, ArmorInfo::ClassName(ArmorClass::kClothing) },
	} };

	// Class names files written before the SkyUI classes hold, and what Load reads them as.
	// "Clothing" kept its name.
	struct RenamedCategory
	{
		std::string_view stored;
		const char*      current;
	};
	constexpr std::array<RenamedCategory, 2> kRenamedCategories{ {
		{ "Light Armor", ArmorInfo::ClassName(ArmorClass::kLight) },
		{ "Heavy Armor", ArmorInfo::ClassName(ArmorClass::kHeavy) },
	} };

	// --- State ----------------------------------------------------------------

	// Contract: game thread only. g_writable stays false until Load, and for good when
	// the file exists but could not be used.
	std::vector<Library::Outfit> g_outfits;
	std::uint32_t                g_nextId = kFirstId;
	bool                         g_writable = false;

	// --- Helpers --------------------------------------------------------------

	std::filesystem::path LibraryPath()
	{
		return std::filesystem::current_path() / kLibraryFile;
	}

	bool IsOffered(std::string_view a_category)
	{
		return std::ranges::any_of(kCategories, [a_category](const Library::Category& category) { return category.name == a_category; });
	}

	std::string DisplayName(const RE::TESObjectARMO& a_armor)
	{
		const auto* name = a_armor.GetName();
		return name && *name ? name : std::format("{:08X}", a_armor.GetFormID());
	}

	// The loaded armor a piece names, or nullptr.
	RE::TESObjectARMO* ArmorOf(const Library::PieceRef& a_piece)
	{
		auto*       dataHandler = RE::TESDataHandler::GetSingleton();
		const auto* file = dataHandler ? dataHandler->LookupModByName(a_piece.plugin) : nullptr;
		// A light plugin owns only 12 bits; a larger id would reach into another plugin's forms.
		if (!file || (file->IsLight() && a_piece.localID > kMaxLightLocalID)) {
			return nullptr;
		}
		return dataHandler->LookupForm<RE::TESObjectARMO>(a_piece.localID, a_piece.plugin);
	}

	// Every armor as a piece, each form once. Returns why it cannot: no armor, or armor
	// created in game, which has no plugin to name.
	std::optional<std::string> ToPieces(const std::vector<RE::TESObjectARMO*>& a_armor, std::vector<Library::PieceRef>& a_pieces)
	{
		a_pieces.clear();
		for (const auto* armor : a_armor) {
			if (!armor) {
				continue;
			}
			const auto* file = armor->IsDynamicForm() ? nullptr : armor->GetFile(0);
			if (!file) {
				return std::format("{} was created in game and cannot be saved.", DisplayName(*armor));
			}
			Library::PieceRef piece{ std::string{ file->GetFilename() }, armor->GetLocalFormID(), DisplayName(*armor) };
			const bool        listed = std::ranges::any_of(a_pieces, [&piece](const Library::PieceRef& other) {
				return other.localID == piece.localID && ExampleMod::CaseInsensitiveEqual(other.plugin, piece.plugin);
			});
			if (!listed) {
				a_pieces.push_back(std::move(piece));
			}
		}
		if (a_pieces.empty()) {
			return "Tick at least one piece.";
		}
		return std::nullopt;
	}

	// --- File -----------------------------------------------------------------

	const nlohmann::json* Field(const nlohmann::json& a_object, const char* a_key)
	{
		const auto found = a_object.find(a_key);
		return found != a_object.end() ? &*found : nullptr;
	}

	bool ReadId(const nlohmann::json* a_value, std::uint32_t& a_id)
	{
		if (!a_value || !a_value->is_number_unsigned()) {
			return false;
		}
		const auto value = a_value->get<std::uint64_t>();
		if (value < kFirstId || value > (std::numeric_limits<std::uint32_t>::max)()) {
			return false;
		}
		a_id = static_cast<std::uint32_t>(value);
		return true;
	}

	bool ReadString(const nlohmann::json* a_value, std::string& a_text)
	{
		if (!a_value || !a_value->is_string()) {
			return false;
		}
		a_text = a_value->get<std::string>();
		return !a_text.empty();
	}

	// "0x" and up to six hex digits.
	bool ReadLocalID(const nlohmann::json* a_value, RE::FormID& a_id)
	{
		std::string text;
		if (!ReadString(a_value, text) || text.size() < 3 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X')) {
			return false;
		}
		const char* last = text.data() + text.size();
		const auto [end, error] = std::from_chars(text.data() + 2, last, a_id, 16);
		return error == std::errc{} && end == last && a_id <= kMaxLocalID;
	}

	// An old class name as its current one, listed once.
	void RenameOldCategories(std::vector<std::string>& a_categories)
	{
		std::vector<std::string> renamed;
		for (auto& category : a_categories) {
			const auto  old = std::ranges::find(kRenamedCategories, std::string_view{ category }, &RenamedCategory::stored);
			std::string name = old != kRenamedCategories.end() ? old->current : std::move(category);
			if (std::ranges::find(renamed, name) == renamed.end()) {
				renamed.push_back(std::move(name));
			}
		}
		a_categories = std::move(renamed);
	}

	std::optional<std::string> ParsePiece(const nlohmann::json& a_json, Library::PieceRef& a_piece)
	{
		if (!a_json.is_object() || !ReadString(Field(a_json, "plugin"), a_piece.plugin)) {
			return "a piece without a plugin";
		}
		if (!ReadLocalID(Field(a_json, "formId"), a_piece.localID)) {
			return "a piece whose formId is not 0x and up to six hex digits";
		}
		if (const auto* name = Field(a_json, "name"); name && name->is_string()) {
			a_piece.name = name->get<std::string>();
		}
		return std::nullopt;
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
		const auto* gender = Field(a_json, "gender");
		if (!gender || !gender->is_number_integer() || gender->get<std::int64_t>() < -1 || gender->get<std::int64_t>() > 1) {
			return "no gender of -1, 0 or 1";
		}
		a_outfit.gender = gender->get<int>();
		const auto* categories = Field(a_json, "categories");
		if (!categories || !categories->is_array()) {
			return "no category list";
		}
		for (const auto& category : *categories) {
			if (!ReadString(&category, a_outfit.categories.emplace_back())) {
				return "a category that is not a name";
			}
		}
		// In memory only: the file keeps the old names, and Jewelry, which is no longer a
		// category, until the next Create or Update writes it.
		RenameOldCategories(a_outfit.categories);
		std::erase(a_outfit.categories, ArmorInfo::ClassName(ArmorClass::kJewelry));
		const auto* pieces = Field(a_json, "pieces");
		if (!pieces || !pieces->is_array() || pieces->empty()) {
			return "no pieces";
		}
		for (const auto& piece : *pieces) {
			if (auto problem = ParsePiece(piece, a_outfit.pieces.emplace_back())) {
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
				pieces.push_back({ { "plugin", piece.plugin }, { "formId", std::format("0x{:06X}", piece.localID) }, { "name", piece.name } });
			}
			list.push_back({ { "id", outfit.id }, { "name", outfit.name }, { "gender", outfit.gender }, { "categories", outfit.categories },
				{ "pieces", std::move(pieces) } });
		}
		return { { "version", kLibraryVersion }, { "nextId", a_nextId }, { "outfits", std::move(list) } };
	}

	// Writes a temporary file and moves it over outfits.json, so a crash mid-write leaves
	// the old file whole. Returns why it failed, or nullopt.
	// Contract: game names come in the game's code page; dump replaces bytes that are not
	// UTF-8 instead of throwing.
	std::optional<std::string> WriteLibrary(const std::vector<Library::Outfit>& a_outfits, std::uint32_t a_nextId)
	{
		constexpr int kIndent = 2;  // spaces per level, for people reading the file

		const auto path = LibraryPath();
		auto       temporary = path;
		temporary += L".tmp";
		{
			std::ofstream file{ temporary, std::ios::binary | std::ios::trunc };
			file << ToJson(a_outfits, a_nextId).dump(kIndent, ' ', false, nlohmann::ordered_json::error_handler_t::replace) << '\n';
			file.close();
			if (!file) {
				logger::error("Cannot write outfits.json.tmp");
				return "Livery could not write outfits.json (see Livery.log).";
			}
		}
		std::error_code error;
		std::filesystem::rename(temporary, path, error);
		if (error) {
			logger::error("Cannot replace outfits.json: {}", error.message());
			std::filesystem::remove(temporary, error);
			return "Livery could not write outfits.json (see Livery.log).";
		}
		return std::nullopt;
	}
}

namespace Library
{
	void Load()
	{
		g_outfits.clear();
		g_nextId = kFirstId;
		g_writable = false;

		const auto    path = LibraryPath();
		std::ifstream file{ path, std::ios::binary };
		if (!file) {
			std::error_code error;
			if (std::filesystem::exists(path, error) || error) {
				logger::error("Cannot open outfits.json; the outfit library stays empty and is not written");
				return;
			}
			g_writable = true;
			logger::info("No outfits.json yet; the outfit library is empty");
			return;
		}
		const auto          json = nlohmann::json::parse(file, nullptr, false);
		std::vector<Outfit> outfits;
		std::uint32_t       nextId = kFirstId;
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

	std::span<const Category> Categories()
	{
		return kCategories;
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
		std::vector<RE::TESObjectARMO*> armor;
		std::size_t                     unloaded = 0;
		for (const auto& piece : a_outfit.pieces) {
			auto* loaded = ArmorOf(piece);
			if (!loaded) {
				++unloaded;
			} else if (std::ranges::find(armor, loaded) == armor.end()) {
				armor.push_back(loaded);
			}
		}
		if (a_unloaded) {
			*a_unloaded = unloaded;
		}
		return armor;
	}

	// Contract: every refusal is checked before anything changes, so no outfit is ever
	// left without pieces.
	std::optional<std::string> Create(std::string_view a_name, int a_gender, const std::vector<RE::TESObjectARMO*>& a_armor,
		const std::vector<std::string>& a_categories)
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
		Outfit outfit{ .id = g_nextId, .name = std::string{ a_name }, .gender = a_gender, .categories = a_categories };
		if (auto problem = ToPieces(a_armor, outfit.pieces)) {
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
	std::optional<std::string> Update(std::uint32_t a_id, std::string_view a_name, int a_gender, const std::vector<RE::TESObjectARMO*>& a_armor,
		const std::vector<std::string>& a_categories)
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
		if (auto problem = ToPieces(a_armor, pieces)) {
			return problem;
		}
		Outfit updated = *found;
		updated.name = std::string{ a_name };
		updated.gender = a_gender;
		// This load order cannot tell what a piece of a missing plugin was, so it stays.
		std::erase_if(updated.pieces, [](const PieceRef& piece) { return ArmorOf(piece) != nullptr; });
		updated.pieces.insert(updated.pieces.begin(), pieces.begin(), pieces.end());
		std::erase_if(updated.categories, IsOffered);
		updated.categories.insert(updated.categories.begin(), a_categories.begin(), a_categories.end());

		auto outfits = g_outfits;
		outfits[static_cast<std::size_t>(found - g_outfits.begin())] = std::move(updated);
		if (auto problem = WriteLibrary(outfits, g_nextId)) {
			return problem;
		}
		g_outfits = std::move(outfits);
		return std::nullopt;
	}
}
