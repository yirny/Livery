// SPDX-License-Identifier: GPL-3.0-or-later
//
// tailor-export.json, the format Tailor 3.0.1 imports (Tailor src/outfit/OutfitTransfer.cpp):
//   { "format": "TailorOutfitExport", "version": 1, "outfits": [ { "name": "Guard", "sex": -1,
//     "categories": [ "light" ], "items": [ { "formId": 80161, "name": "Imperial Light Armor",
//     "plugin": "Skyrim.esm" } ] } ] }
// Tailor takes each outfit on its own: one with a missing armor or a taken name is skipped
// there, the rest are imported. An imported outfit is never updated by a later import.
#include "PCH.h"

#include "TailorExport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
	constexpr auto kExportFile = L"Data/SKSE/Plugins/Livery/tailor-export.json";

	// Tailor's key for a Livery category, or nullptr for one Tailor has no built-in category for.
	const char* TailorKey(std::string_view a_category)
	{
		constexpr std::array<std::pair<std::string_view, const char*>, 9> kKeys{ {
			{ "Clothing", "clothing" },
			{ "Light Armor", "light" },
			{ "Heavy Armor", "heavy" },
			{ "Adventuring", "adventuring" },
			{ "Town", "town" },
			{ "Home", "home" },
			{ "Sleep", "sleep" },
			{ "Swimming", "swimming" },
			{ "Warm", "warm" },
		} };
		const auto                                                        found = std::ranges::find(kKeys, a_category, &std::pair<std::string_view, const char*>::first);
		return found != kKeys.end() ? found->second : nullptr;
	}

	// The outfit as Tailor imports it, or a null value when it has no Tailor category.
	nlohmann::ordered_json ToTailor(const Library::Outfit& a_outfit)
	{
		auto categories = nlohmann::ordered_json::array();
		for (const auto& category : a_outfit.categories) {
			const auto* key = TailorKey(category);
			if (key && std::ranges::none_of(categories, [key](const nlohmann::ordered_json& listed) { return listed == key; })) {
				categories.push_back(key);
			}
		}
		if (categories.empty()) {
			return nullptr;
		}
		auto items = nlohmann::ordered_json::array();
		for (const auto& piece : a_outfit.pieces) {
			items.push_back({ { "formId", piece.localID }, { "name", piece.name }, { "plugin", piece.plugin } });
		}
		return { { "name", a_outfit.name }, { "sex", a_outfit.gender }, { "categories", std::move(categories) }, { "items", std::move(items) } };
	}

	// Writes a temporary file and moves it over the export, so a failed write leaves the old one.
	// Contract: game names come in the game's code page; dump replaces bytes that are not UTF-8
	// instead of throwing, so Tailor's strict parse accepts the file.
	bool WriteFile(const nlohmann::ordered_json& a_json)
	{
		constexpr int kIndent = 2;  // spaces per level, for people reading the file

		const auto path = std::filesystem::current_path() / kExportFile;
		auto       temporary = path;
		temporary += L".tmp";
		{
			std::ofstream file{ temporary, std::ios::binary | std::ios::trunc };
			file << a_json.dump(kIndent, ' ', false, nlohmann::ordered_json::error_handler_t::replace) << '\n';
			file.close();
			if (!file) {
				logger::error("Cannot write tailor-export.json.tmp");
				return false;
			}
		}
		std::error_code error;
		std::filesystem::rename(temporary, path, error);
		if (error) {
			logger::error("Cannot replace tailor-export.json: {}", error.message());
			std::filesystem::remove(temporary, error);
			return false;
		}
		return true;
	}
}

namespace TailorExport
{
	void Write(const std::vector<Library::Outfit>& a_outfits)
	{
		constexpr const char* kFormat = "TailorOutfitExport";  // Tailor refuses any other "format"
		constexpr int         kVersion = 1;                    // the only version Tailor 3.0.1 imports

		auto        outfits = nlohmann::ordered_json::array();
		std::size_t leftOut = 0;
		for (const auto& outfit : a_outfits) {
			if (auto json = ToTailor(outfit); json.is_null()) {
				++leftOut;
			} else {
				outfits.push_back(std::move(json));
			}
		}
		const auto written = outfits.size();
		if (WriteFile({ { "format", kFormat }, { "version", kVersion }, { "outfits", std::move(outfits) } })) {
			logger::info("Wrote {} outfit(s) to tailor-export.json ({} without a Tailor category left out)", written, leftOut);
		}
	}
}
