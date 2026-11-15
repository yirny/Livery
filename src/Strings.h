// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// FILE MAP
//   Table      - LIVERY_STRINGS: every text Livery shows the player, by id, with its English
//   Detail     - the translation file's parse and checks, with no SKSE, so the unit tests reach them
//   Strings    - Load (Strings.cpp) reads the chosen language's file once; Get and Format hand out
//                the text in it, or the English
//
// Livery's player-facing text (docs/design.md §10). A translation file,
// Data/Interface/Translations/Livery_<LANGUAGE>.txt, holds one text a line as the game's own
// translation files do: the id after "$Livery_", a tab, the text.
//   $Livery_PageDress	Dress
// The file is UTF-16 LE with its byte order mark, as the game's are, or UTF-8. In a text, "\n" is
// a line break and "\\" a backslash. A text with values names them {0}, {1}... as its English
// does, in any order, and may leave some out; "{{" and "}}" are braces. Lines that do not start
// with "$" are ignored, so they can hold notes. An id the file lacks, or a text that names a value
// its English has not, keeps the English.

// --- Table -------------------------------------------------------------------

// X(id, English text). The id names the text in the file without its "k".
#define LIVERY_STRINGS(X)            \
	X(kPageDress, "Dress")           \
	X(kPageWardrobe, "Wardrobe")     \
	X(kPageSituations, "Situations") \
	X(kPageChests, "Chests")         \
	X(kPageAppearance, "Appearance")

namespace Strings
{
	enum class Text : std::uint16_t
	{
#define LIVERY_STRING_ID(a_id, a_english) a_id,
		LIVERY_STRINGS(LIVERY_STRING_ID)
#undef LIVERY_STRING_ID
	};

	// --- Detail ----------------------------------------------------------------

	namespace Detail
	{
		inline constexpr std::string_view kKeyPrefix = "$Livery_";  // before a text's id in the file

		struct Entry
		{
			std::string_view id;       // as the file names it: the enum's name without its "k"
			const char*      english;  // shown when the file has no text for it
		};

		inline constexpr std::array kEntries{
#define LIVERY_STRING_ENTRY(a_id, a_english) Entry{ std::string_view{ #a_id }.substr(1), a_english },
			LIVERY_STRINGS(LIVERY_STRING_ENTRY)
#undef LIVERY_STRING_ENTRY
		};

		inline constexpr std::size_t kCount = kEntries.size();

		using Table = std::array<const char*, kCount>;

		inline constexpr Table kEnglish{
#define LIVERY_STRING_ENGLISH(a_id, a_english) a_english,
			LIVERY_STRINGS(LIVERY_STRING_ENGLISH)
#undef LIVERY_STRING_ENGLISH
		};

		// The number of values a text names, one more than its highest {n}; nullopt for a brace
		// that is not "{{", "}}" or {n} with n a number up to 99.
		inline std::optional<std::size_t> ValueCount(std::string_view a_text)
		{
			constexpr std::size_t kMaxIndex = 99;
			std::size_t           count = 0;
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				const char c = a_text[i];
				if (c == '}') {
					if (i + 1 < a_text.size() && a_text[i + 1] == '}') {
						++i;
						continue;
					}
					return std::nullopt;
				}
				if (c != '{') {
					continue;
				}
				if (i + 1 < a_text.size() && a_text[i + 1] == '{') {
					++i;
					continue;
				}
				const auto close = a_text.find('}', i + 1);
				if (close == std::string_view::npos || close == i + 1) {
					return std::nullopt;
				}
				std::size_t index = 0;
				for (std::size_t j = i + 1; j < close; ++j) {
					if (a_text[j] < '0' || a_text[j] > '9') {
						return std::nullopt;
					}
					index = index * 10 + static_cast<std::size_t>(a_text[j] - '0');
					if (index > kMaxIndex) {
						return std::nullopt;
					}
				}
				count = (std::max)(count, index + 1);
				i = close;
			}
			return count;
		}

		inline void AppendUtf8(std::string& a_out, char32_t a_code)
		{
			if (a_code < 0x80) {
				a_out += static_cast<char>(a_code);
			} else if (a_code < 0x800) {
				a_out += static_cast<char>(0xC0 | (a_code >> 6));
				a_out += static_cast<char>(0x80 | (a_code & 0x3F));
			} else if (a_code < 0x10000) {
				a_out += static_cast<char>(0xE0 | (a_code >> 12));
				a_out += static_cast<char>(0x80 | ((a_code >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_code & 0x3F));
			} else {
				a_out += static_cast<char>(0xF0 | (a_code >> 18));
				a_out += static_cast<char>(0x80 | ((a_code >> 12) & 0x3F));
				a_out += static_cast<char>(0x80 | ((a_code >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_code & 0x3F));
			}
		}

		// UTF-16 LE bytes (no byte order mark) as UTF-8; a lone surrogate becomes U+FFFD and an
		// odd last byte is dropped.
		inline std::string Utf16LeToUtf8(std::string_view a_bytes)
		{
			constexpr char32_t kReplacement = 0xFFFD;
			const auto         unitAt = [a_bytes](std::size_t a_index) {
				return static_cast<char32_t>(static_cast<unsigned char>(a_bytes[a_index * 2]) | (static_cast<unsigned char>(a_bytes[a_index * 2 + 1]) << 8));
			};
			const std::size_t units = a_bytes.size() / 2;
			std::string       utf8;
			utf8.reserve(a_bytes.size());
			for (std::size_t i = 0; i < units; ++i) {
				const auto unit = unitAt(i);
				if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < units) {
					if (const auto low = unitAt(i + 1); low >= 0xDC00 && low <= 0xDFFF) {
						AppendUtf8(utf8, 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
						++i;
						continue;
					}
				}
				AppendUtf8(utf8, unit >= 0xD800 && unit <= 0xDFFF ? kReplacement : unit);
			}
			return utf8;
		}

		// Whether every byte sequence is well formed UTF-8 (lead and continuation bytes).
		inline bool IsUtf8(std::string_view a_text)
		{
			for (std::size_t i = 0; i < a_text.size();) {
				const auto  lead = static_cast<unsigned char>(a_text[i]);
				std::size_t length = 0;
				if (lead < 0x80) {
					length = 1;
				} else if (lead >= 0xC2 && lead <= 0xDF) {
					length = 2;
				} else if (lead >= 0xE0 && lead <= 0xEF) {
					length = 3;
				} else if (lead >= 0xF0 && lead <= 0xF4) {
					length = 4;
				} else {
					return false;
				}
				if (i + length > a_text.size()) {
					return false;
				}
				for (std::size_t k = 1; k < length; ++k) {
					if ((static_cast<unsigned char>(a_text[i + k]) & 0xC0) != 0x80) {
						return false;
					}
				}
				i += length;
			}
			return true;
		}

		// A text as the file writes it, with "\n" and "\\" read.
		inline std::string Unescape(std::string_view a_text)
		{
			std::string text;
			text.reserve(a_text.size());
			for (std::size_t i = 0; i < a_text.size(); ++i) {
				if (a_text[i] == '\\' && i + 1 < a_text.size() && (a_text[i + 1] == 'n' || a_text[i + 1] == '\\')) {
					text += a_text[i + 1] == 'n' ? '\n' : '\\';
					++i;
				} else {
					text += a_text[i];
				}
			}
			return text;
		}

		// What a file gives: a text for each id it translates, the English kept for the rest.
		struct Translation
		{
			std::vector<std::string> texts = std::vector<std::string>(kCount);  // at each Text's index; empty keeps the English
			std::size_t              read = 0;                                  // texts taken from the file
			std::vector<std::string> problems;                                  // a sentence each, for the log
		};

		// Reads a whole translation file's bytes. A file that is neither UTF-16 LE with its byte
		// order mark nor UTF-8 gives nothing, with one problem.
		inline Translation Translate(std::string_view a_bytes)
		{
			constexpr std::string_view kUtf16Bom = "\xFF\xFE";
			constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

			Translation result;
			std::string decoded;
			if (a_bytes.starts_with(kUtf16Bom)) {
				decoded = Utf16LeToUtf8(a_bytes.substr(kUtf16Bom.size()));
			} else {
				decoded = a_bytes.starts_with(kUtf8Bom) ? a_bytes.substr(kUtf8Bom.size()) : a_bytes;
				if (!IsUtf8(decoded)) {
					result.problems.push_back("The file is neither UTF-16 LE with a byte order mark nor UTF-8, so it was not read.");
					return result;
				}
			}

			std::unordered_map<std::string_view, std::size_t> indexOf;
			for (std::size_t i = 0; i < kCount; ++i) {
				indexOf.emplace(kEntries[i].id, i);
			}
			std::string_view rest = decoded;
			while (!rest.empty()) {
				const auto end = rest.find('\n');
				auto       line = rest.substr(0, end);
				rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
				if (line.ends_with('\r')) {
					line.remove_suffix(1);
				}
				if (!line.starts_with('$')) {
					continue;
				}
				const auto tab = line.find('\t');
				const auto start = tab == std::string_view::npos ? tab : line.find_first_not_of('\t', tab);
				const auto key = line.substr(0, tab);
				if (start == std::string_view::npos || !key.starts_with(kKeyPrefix)) {
					continue;
				}
				const auto found = indexOf.find(key.substr(kKeyPrefix.size()));
				if (found == indexOf.end()) {
					result.problems.push_back(std::format("{} is not a text Livery has; ignored.", key));
					continue;
				}
				auto       text = Unescape(line.substr(start));
				const auto values = ValueCount(text);
				if (!values || *values > *ValueCount(kEntries[found->second].english)) {
					result.problems.push_back(std::format("{} has a brace or value its English text has not; the English is kept.", key));
					continue;
				}
				auto& slot = result.texts[found->second];
				result.read += slot.empty() ? 1 : 0;
				slot = std::move(text);
			}
			return result;
		}

		// Whether a language name can be part of a file name: letters, digits and "_" only.
		inline bool IsLanguageName(std::string_view a_name)
		{
			return !a_name.empty() && std::ranges::all_of(a_name, [](char a_c) {
				return (a_c >= 'A' && a_c <= 'Z') || (a_c >= 'a' && a_c <= 'z') || (a_c >= '0' && a_c <= '9') || a_c == '_';
			});
		}
	}

	// --- Strings ---------------------------------------------------------------

	// Reads Livery_<language>.txt from Data/Interface/Translations, `a_language` empty for the
	// game's own (its sLanguage); without the file every text stays English.
	// Contract: call once, at kDataLoaded, before anything shows text. Get may run on any thread
	// meanwhile: until the read is done it hands out the English.
	void Load(std::string_view a_language);

	// The text in the chosen language. The pointer stays valid for the whole game. Safe from any thread.
	const char* Get(Text a_text);

	// The text with its {0}, {1}... filled by `a_args`, as many as its English names. Safe from any thread.
	template <class... Args>
	std::string Format(Text a_text, const Args&... a_args)
	{
		return std::vformat(Get(a_text), std::make_format_args(a_args...));
	}
}
