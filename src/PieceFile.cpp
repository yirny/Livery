// SPDX-License-Identifier: GPL-3.0-or-later
#include "PCH.h"

#include "PieceFile.h"

#include "Utils.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace PieceFile
{
	// --- Armor --------------------------------------------------------------

	std::string DisplayName(const RE::TESObjectARMO& a_armor)
	{
		const auto* name = a_armor.GetName();
		return name && *name ? name : std::format("{:08X}", a_armor.GetFormID());
	}

	RE::TESObjectARMO* ArmorOf(const PieceRef& a_piece)
	{
		auto*       dataHandler = RE::TESDataHandler::GetSingleton();
		const auto* file = dataHandler ? dataHandler->LookupModByName(a_piece.plugin) : nullptr;
		// A light plugin owns only 12 bits; a larger id would reach into another plugin's forms.
		if (!file || (file->IsLight() && a_piece.localID > kMaxLightLocalID)) {
			return nullptr;
		}
		return dataHandler->LookupForm<RE::TESObjectARMO>(a_piece.localID, a_piece.plugin);
	}

	std::optional<std::string> ToPieces(const std::vector<RE::TESObjectARMO*>& a_armor, std::vector<PieceRef>& a_pieces)
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
			PieceRef   piece{ std::string{ file->GetFilename() }, armor->GetLocalFormID(), DisplayName(*armor) };
			const bool listed = std::ranges::any_of(a_pieces, [&piece](const PieceRef& other) {
				return other.localID == piece.localID && ExampleMod::CaseInsensitiveEqual(other.plugin, piece.plugin);
			});
			if (!listed) {
				a_pieces.push_back(std::move(piece));
			}
		}
		return std::nullopt;
	}

	std::vector<RE::TESObjectARMO*> LoadedArmor(const std::vector<PieceRef>& a_pieces, std::size_t* a_unloaded)
	{
		std::vector<RE::TESObjectARMO*> armor;
		std::size_t                     unloaded = 0;
		for (const auto& piece : a_pieces) {
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

	// --- File ---------------------------------------------------------------

	FileState ReadText(const std::filesystem::path& a_path, std::string& a_text)
	{
		std::ifstream file{ a_path, std::ios::binary };
		if (!file) {
			std::error_code error;
			return std::filesystem::exists(a_path, error) || error ? FileState::kUnreadable : FileState::kMissing;
		}
		a_text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		return ExampleMod::TrimWhitespace(a_text).empty() ? FileState::kEmpty : FileState::kText;
	}

	std::optional<std::string> WriteJson(const std::filesystem::path& a_path, const nlohmann::ordered_json& a_json, std::string_view a_fileName)
	{
		constexpr int kIndent = 2;  // spaces per level, for people reading the file

		auto temporary = a_path;
		temporary += L".tmp";
		{
			std::ofstream file{ temporary, std::ios::binary | std::ios::trunc };
			file << a_json.dump(kIndent, ' ', false, nlohmann::ordered_json::error_handler_t::replace) << '\n';
			file.close();
			if (!file) {
				logger::error("Cannot write {}.tmp", a_fileName);
				return std::format("Livery could not write {} (see Livery.log).", a_fileName);
			}
		}
		std::error_code error;
		std::filesystem::rename(temporary, a_path, error);
		if (error) {
			logger::error("Cannot replace {}: {}", a_fileName, error.message());
			std::filesystem::remove(temporary, error);
			return std::format("Livery could not write {} (see Livery.log).", a_fileName);
		}
		return std::nullopt;
	}
}
