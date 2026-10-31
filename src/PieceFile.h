// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace RE
{
	class TESObjectARMO;
}

// What outfits.json (src/Library.cpp) and chests.json (src/Chests.cpp) share: how a piece of
// armor is kept in a file, the JSON fields they read alike, the armor a piece names, and the
// files' read at load and write through a temporary file. The JSON part is pure, with no SKSE,
// so the unit tests build it; the armor and file parts are in PieceFile.cpp.
//
// Contract: the armor and file functions run on the game thread (SKSE task), as the stores do.
namespace PieceFile
{
	constexpr std::uint32_t kFirstId = 1;              // the first id a store gives; ids are never reused
	constexpr std::uint32_t kMaxLocalID = 0xFFFFFF;    // a plugin's own part of a FormID
	constexpr std::uint32_t kMaxLightLocalID = 0xFFF;  // the same for a light plugin

	// One armor piece as a file stores it: the file name of the plugin that defines it and its
	// FormID within that plugin, so a file survives load order changes; a piece whose plugin is
	// not loaded stays in it.
	struct PieceRef
	{
		std::string   plugin;       // file name of the plugin that defines the armor
		std::uint32_t localID = 0;  // its FormID within that plugin (12 bits for a light plugin)
		std::string   name;         // its name when saved, for people reading the file only
	};

	// --- JSON ---------------------------------------------------------------

	inline const nlohmann::json* Field(const nlohmann::json& a_object, const char* a_key)
	{
		const auto found = a_object.find(a_key);
		return found != a_object.end() ? &*found : nullptr;
	}

	// An id from kFirstId up that fits 32 bits.
	inline bool ReadId(const nlohmann::json* a_value, std::uint32_t& a_id)
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

	// A string that is not empty.
	inline bool ReadString(const nlohmann::json* a_value, std::string& a_text)
	{
		if (!a_value || !a_value->is_string()) {
			return false;
		}
		a_text = a_value->get<std::string>();
		return !a_text.empty();
	}

	// "0x" and up to six hex digits.
	inline bool ReadLocalID(const nlohmann::json* a_value, std::uint32_t& a_id)
	{
		std::string text;
		if (!ReadString(a_value, text) || text.size() < 3 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X')) {
			return false;
		}
		const char* last = text.data() + text.size();
		const auto [end, error] = std::from_chars(text.data() + 2, last, a_id, 16);
		return error == std::errc{} && end == last && a_id <= kMaxLocalID;
	}

	// Returns what is wrong with the piece, or nullopt.
	inline std::optional<std::string> ParsePiece(const nlohmann::json& a_json, PieceRef& a_piece)
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

	inline nlohmann::ordered_json ToJson(const PieceRef& a_piece)
	{
		return { { "plugin", a_piece.plugin }, { "formId", std::format("0x{:06X}", a_piece.localID) }, { "name", a_piece.name } };
	}

	// --- Armor (PieceFile.cpp) ----------------------------------------------

	// The armor's name, or its FormID in hex when it has none.
	std::string DisplayName(const RE::TESObjectARMO& a_armor);

	// The loaded armor a piece names, or nullptr.
	RE::TESObjectARMO* ArmorOf(const PieceRef& a_piece);

	// Every armor as a piece, each form once, in `a_armor`'s order. Returns why it cannot:
	// armor created in game, which has no plugin to name. An empty list is not refused here.
	std::optional<std::string> ToPieces(const std::vector<RE::TESObjectARMO*>& a_armor, std::vector<PieceRef>& a_pieces);

	// The pieces' armor that is loaded, each form once. `a_unloaded`, when given, gets the
	// number of pieces whose plugin or form is not loaded.
	std::vector<RE::TESObjectARMO*> LoadedArmor(const std::vector<PieceRef>& a_pieces, std::size_t* a_unloaded = nullptr);

	// --- File (PieceFile.cpp) -----------------------------------------------

	enum class FileState
	{
		kMissing,     // no file: an empty store that may be written
		kEmpty,       // nothing but whitespace: read as no file
		kUnreadable,  // the file exists but cannot be opened: the store stays empty and is not written
		kText         // `a_text` holds the file
	};

	FileState ReadText(const std::filesystem::path& a_path, std::string& a_text);

	// Writes a temporary file and moves it over `a_path`, so a crash mid-write leaves the old
	// file whole. `a_fileName` names the file in the log and the message. Returns why it
	// failed, or nullopt.
	// Contract: game names come in the game's code page; the dump replaces bytes that are not
	// UTF-8 instead of throwing.
	std::optional<std::string> WriteJson(const std::filesystem::path& a_path, const nlohmann::ordered_json& a_json, std::string_view a_fileName);
}
