#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "Strings.h"

// The tests link no Strings.cpp, which reads the game's language: every text the tested code
// asks for is the English.
const char* Strings::Get(Text a_text)
{
	return English(a_text);
}

namespace
{
	using Strings::Detail::Translate;

	std::size_t IndexOf(Strings::Text a_text)
	{
		return static_cast<std::size_t>(a_text);
	}

	// The game's own translation file form: UTF-16 LE after its byte order mark.
	std::string Utf16File(std::u16string_view a_text)
	{
		std::string bytes = "\xFF\xFE";
		for (const char16_t unit : a_text) {
			bytes += static_cast<char>(unit & 0xFF);
			bytes += static_cast<char>(unit >> 8);
		}
		return bytes;
	}
}

TEST_CASE("Every English text has well formed values", "[Strings]")
{
	for (const auto& entry : Strings::Detail::kEntries) {
		INFO(entry.id);
		CHECK(Strings::Detail::ValueCount(entry.english).has_value());
	}
}

TEST_CASE("Translate reads a UTF-16 LE file with its byte order mark", "[Strings]")
{
	const auto result = Translate(Utf16File(u"; a note\r\n$Livery_PageDress\t\u7740\u308B\r\n$Livery_PageChests\t\tChests \U0001F4E6\r\n"));
	CHECK(result.problems.empty());
	CHECK(result.read == 2);
	CHECK(result.texts[IndexOf(Strings::Text::kPageDress)] == "\xE7\x9D\x80\xE3\x82\x8B");  // 着る
	CHECK(result.texts[IndexOf(Strings::Text::kPageChests)] == "Chests \xF0\x9F\x93\xA6");  // a surrogate pair
	CHECK(result.texts[IndexOf(Strings::Text::kPageWardrobe)].empty());                     // keeps the English
}

TEST_CASE("Translate reads UTF-8 with or without its byte order mark", "[Strings]")
{
	CHECK(Translate("$Livery_PageDress\tKleiden\n").texts[IndexOf(Strings::Text::kPageDress)] == "Kleiden");
	CHECK(Translate("\xEF\xBB\xBF$Livery_PageDress\tKleiden\n").texts[IndexOf(Strings::Text::kPageDress)] == "Kleiden");
}

TEST_CASE("Translate refuses a file in neither encoding", "[Strings]")
{
	const auto result = Translate("$Livery_PageDress\t\x93\x53\n");  // code page 932 bytes
	CHECK(result.read == 0);
	CHECK(result.problems.size() == 1);
}

TEST_CASE("Translate reads line breaks and backslashes", "[Strings]")
{
	CHECK(Translate("$Livery_PageDress\tOne\\nTwo \\\\ \\t\n").texts[IndexOf(Strings::Text::kPageDress)] == "One\nTwo \\ \\t");
}

TEST_CASE("Translate ignores unknown ids, lines without a text and other mods' keys", "[Strings]")
{
	const auto result = Translate("$Livery_NoSuchText\tx\n$Livery_PageDress\n$SkyUI_Dress\tx\n");
	CHECK(result.read == 0);
	CHECK(result.problems.size() == 1);  // only the unknown Livery id is named
}

TEST_CASE("ValueCount takes {n}, {{ and }} and refuses any other brace", "[Strings]")
{
	using Strings::Detail::ValueCount;
	CHECK(ValueCount("no values") == 0u);
	CHECK(ValueCount("{1} of {0}") == 2u);
	CHECK(ValueCount("{{literal}}") == 0u);
	CHECK_FALSE(ValueCount("{}").has_value());
	CHECK_FALSE(ValueCount("{0:08X}").has_value());
	CHECK_FALSE(ValueCount("open {0").has_value());
	CHECK_FALSE(ValueCount("close }").has_value());
	CHECK_FALSE(ValueCount("{100}").has_value());
}

TEST_CASE("Translate keeps the English for a text naming a value its English has not", "[Strings]")
{
	const auto result = Translate("$Livery_PageDress\tDress {0}\n");  // the English names no value
	CHECK(result.read == 0);
	CHECK(result.texts[IndexOf(Strings::Text::kPageDress)].empty());
	CHECK(result.problems.size() == 1);
}

TEST_CASE("Translate lets a time name the hour of 24, which its English leaves out", "[Strings]")
{
	const auto result = Translate("$Livery_TimePmMinutes\t{2}:{1}\n$Livery_TimeAm\t{2}:{1}\n$Livery_TimePm\t{3}\n");
	CHECK(result.read == 2);
	CHECK(result.texts[IndexOf(Strings::Text::kTimePmMinutes)] == "{2}:{1}");
	CHECK(result.texts[IndexOf(Strings::Text::kTimeAm)] == "{2}:{1}");
	CHECK(result.texts[IndexOf(Strings::Text::kTimePm)].empty());
	CHECK(result.problems.size() == 1);
}

TEST_CASE("IsLanguageName takes letters, digits and _ only", "[Strings]")
{
	using Strings::Detail::IsLanguageName;
	CHECK(IsLanguageName("JAPANESE"));
	CHECK(IsLanguageName("Chinese_Simplified2"));
	CHECK_FALSE(IsLanguageName(""));
	CHECK_FALSE(IsLanguageName("..\\ENGLISH"));
	CHECK_FALSE(IsLanguageName("EN GLISH"));
}

TEST_CASE("CutAtValues cuts a text into its words and values in order", "[Strings]")
{
	using Strings::Detail::CutAtValues;
	const auto cuts = CutAtValues("Replace {0}'s {1} items");
	REQUIRE(cuts.size() == 5);
	CHECK(cuts[0].words == "Replace ");
	CHECK(cuts[1].value == 0);
	CHECK(cuts[2].words == "'s ");
	CHECK(cuts[3].value == 1);
	CHECK(cuts[4].words == " items");
	CHECK(CutAtValues("{1}{0}").size() == 2);
	CHECK(CutAtValues("").empty());
}

TEST_CASE("CutAtValues reads doubled braces as braces", "[Strings]")
{
	const auto cuts = Strings::Detail::CutAtValues("{{a}} {0}");
	REQUIRE(cuts.size() == 2);
	CHECK(cuts[0].words == "{a} ");
	CHECK(cuts[1].value == 0);
}

TEST_CASE("FirstNonAscii finds the first letter past ASCII", "[Strings]")
{
	using Strings::Detail::FirstNonAscii;
	CHECK(FirstNonAscii("Dress") == 0);
	CHECK(FirstNonAscii("OK \xE7\x9D\x80") == 0x7740);  // 着
	CHECK(FirstNonAscii("Caf\xC3\xA9") == 0xE9);        // é
	CHECK(FirstNonAscii("\xF0\x9F\x93\xA6") == 0x1F4E6);
}

TEST_CASE("Escape writes what Unescape reads", "[Strings]")
{
	using Strings::Detail::Escape;
	using Strings::Detail::Unescape;
	CHECK(Escape("One\nTwo \\ end") == "One\\nTwo \\\\ end");
	for (const auto& entry : Strings::Detail::kEntries) {
		CHECK(Unescape(Escape(entry.english)) == entry.english);
	}
}

// --- The shipped translation files (Data/Interface/Translations) ----------------------------

namespace
{
	const std::filesystem::path kTranslations = std::filesystem::path(LIVERY_SOURCE_DIR) / "Data" / "Interface" / "Translations";

	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream in{ a_path, std::ios::binary };
		REQUIRE(in);
		return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
	}

	// Livery_ENGLISH.txt as Livery ships it: the translators' template, every text in the table's
	// order with its English, UTF-16 LE with its byte order mark and CRLF line ends as the game's
	// translation files.
	std::string EnglishTemplate()
	{
		constexpr std::string_view kHeader =
			"; Livery - the texts as Livery ships them, for translators: copy this file as Livery_<LANGUAGE>.txt\r\n"
			"; (the game's language names: FRENCH, GERMAN, ITALIAN, SPANISH, RUSSIAN, POLISH, CZECH, JAPANESE,\r\n"
			"; CHINESE, PORTUGUESE...) and translate the text after each tab. Keep {0}, {1}... (they may change\r\n"
			"; order), \\n is a line break. Save as UTF-16 LE with a byte order mark, or UTF-8.\r\n";
		std::u16string text;
		const auto     append = [&text](std::string_view a_utf8) {
			for (std::size_t i = 0; i < a_utf8.size();) {
				const auto lead = static_cast<unsigned char>(a_utf8[i]);
				REQUIRE(lead < 0x80);  // the English is ASCII
				text += static_cast<char16_t>(lead);
				++i;
			}
		};
		append(kHeader);
		for (const auto& entry : Strings::Detail::kEntries) {
			append(std::string(Strings::Detail::kKeyPrefix) + std::string(entry.id) + "\t" + Strings::Detail::Escape(entry.english) + "\r\n");
		}
		return Utf16File(text);
	}
}

TEST_CASE("Livery_ENGLISH.txt is the table as it ships", "[Strings]")
{
	CHECK(ReadFile(kTranslations / "Livery_ENGLISH.txt") == EnglishTemplate());
}

// Rewrites Livery_ENGLISH.txt from the table: `liveryTests "[.write-template]"` after a text changes.
TEST_CASE("Write Livery_ENGLISH.txt", "[.write-template]")
{
	std::ofstream out{ kTranslations / "Livery_ENGLISH.txt", std::ios::binary | std::ios::trunc };
	REQUIRE(out);
	out << EnglishTemplate();
}

TEST_CASE("Livery_JAPANESE.txt translates every text and reads clean", "[Strings]")
{
	const auto result = Strings::Detail::Translate(ReadFile(kTranslations / "Livery_JAPANESE.txt"));
	CHECK(result.problems.empty());
	CHECK(result.read == Strings::Detail::kCount);
	for (std::size_t i = 0; i < Strings::Detail::kCount; ++i) {
		INFO(Strings::Detail::kEntries[i].id);
		CHECK_FALSE(result.texts[i].empty());
	}
}
