#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "Utils.h"

using ExampleMod::ClampOrDefault;
using ExampleMod::TrimWhitespace;

TEST_CASE("FormatVersion produces correct string", "[utils]")
{
	CHECK(ExampleMod::FormatVersion(1, 2, 3) == "1.2.3");
	CHECK(ExampleMod::FormatVersion(0, 0, 1) == "0.0.1");
	CHECK(ExampleMod::FormatVersion(10, 0, 0) == "10.0.0");
}

TEST_CASE("AsciiToLower lowercases A-Z only", "[utils]")
{
	using ExampleMod::AsciiToLower;

	CHECK(AsciiToLower('A') == 'a');
	CHECK(AsciiToLower('Z') == 'z');
	CHECK(AsciiToLower('M') == 'm');
	CHECK(AsciiToLower('a') == 'a');
	CHECK(AsciiToLower('z') == 'z');
	CHECK(AsciiToLower('0') == '0');
	CHECK(AsciiToLower('!') == '!');
	CHECK(AsciiToLower(static_cast<unsigned char>(0xFF)) == static_cast<unsigned char>(0xFF));
}

TEST_CASE("CaseInsensitiveEqual matches ASCII case-insensitively", "[utils]")
{
	using ExampleMod::CaseInsensitiveEqual;

	CHECK(CaseInsensitiveEqual("None", "none"));
	CHECK(CaseInsensitiveEqual("NONE", "None"));
	CHECK(CaseInsensitiveEqual("MCM", "mcm"));
	CHECK(CaseInsensitiveEqual("Map", "MAP"));
	CHECK(CaseInsensitiveEqual("", ""));
	CHECK_FALSE(CaseInsensitiveEqual("Map", "Mcm"));
	CHECK_FALSE(CaseInsensitiveEqual("None", ""));
	CHECK_FALSE(CaseInsensitiveEqual("abc", "abcd"));
}

TEST_CASE("TrimWhitespace removes leading and trailing whitespace", "[utils]")
{
	CHECK(TrimWhitespace("  hello  ") == "hello");
	CHECK(TrimWhitespace("\t hello\t") == "hello");
	CHECK(TrimWhitespace("hello") == "hello");
	CHECK(TrimWhitespace("  ") == "");
	CHECK(TrimWhitespace("") == "");
	CHECK(TrimWhitespace("  a b c  ") == "a b c");
	CHECK(TrimWhitespace("hello\r\n") == "hello");
	CHECK(TrimWhitespace("\r\n  hello  \r\n") == "hello");
}

TEST_CASE("CopyName names a copy as Windows does, within the bytes given", "[utils]")
{
	const auto CopyName = [](std::string_view a_name, std::size_t a_maxBytes, const auto& a_taken) {
		return ExampleMod::CopyName(a_name, a_maxBytes, "{0} - Copy", "{0} - Copy {1}", a_taken);
	};

	const auto none = [](const std::string&) { return false; };
	CHECK(CopyName(" Rain ", 63, none) == "Rain - Copy");

	const std::vector<std::string> names{ "Rain - Copy", "rain - copy 2" };
	const auto                     taken = [&names](const std::string& a_name) {
		return std::ranges::any_of(names, [&a_name](const std::string& a_taken) { return ExampleMod::CaseInsensitiveEqual(a_taken, a_name); });
	};
	CHECK(CopyName("Rain", 63, taken) == "Rain - Copy 3");
	CHECK(CopyName("Rain - Copy", 63, taken) == "Rain - Copy - Copy");

	// Cut short to keep the ending, never inside a character, nor before a space.
	CHECK(CopyName("Abcdefgh", 12, none) == "Abcde - Copy");
	CHECK(CopyName("Ab cdefgh", 10, none) == "Ab - Copy");
	CHECK(CopyName("A\xC3\xA9\xC3\xA9", 11, none) == "A\xC3\xA9 - Copy");

	// Worded another way, the number before the name.
	const auto once = [](const std::string& a_name) { return a_name == "Kopie von Rain"; };
	CHECK(ExampleMod::CopyName("Rain", 63, "Kopie von {0}", "Kopie {1} von {0}", once) == "Kopie 2 von Rain");
	CHECK(ExampleMod::CopyName("Abcdefgh", 12, "Kopie von {0}", "Kopie {1} von {0}", none) == "Kopie von Ab");
}

TEST_CASE("ClampOrDefault clamps and validates values", "[utils]")
{
	constexpr float kDefault = 0.5F;
	constexpr float kMin = 0.1F;
	constexpr float kMax = 5.0F;

	// In-range values pass through unchanged
	CHECK(ClampOrDefault(1.0F, kDefault, kMin, kMax) == 1.0F);
	CHECK(ClampOrDefault(2.5F, kDefault, kMin, kMax) == 2.5F);
	CHECK(ClampOrDefault(5.0F, kDefault, kMin, kMax) == 5.0F);
	CHECK(ClampOrDefault(0.1F, kDefault, kMin, kMax) == 0.1F);

	// Below minimum (or non-finite) falls back to the default
	CHECK(ClampOrDefault(0.05F, kDefault, kMin, kMax) == kDefault);
	CHECK(ClampOrDefault(-1.0F, kDefault, kMin, kMax) == kDefault);
	CHECK(ClampOrDefault(0.0F, kDefault, kMin, kMax) == kDefault);

	// Above maximum clamps to max
	CHECK(ClampOrDefault(5.1F, kDefault, kMin, kMax) == kMax);
	CHECK(ClampOrDefault(100.0F, kDefault, kMin, kMax) == kMax);

	// Non-finite values fall back to the default
	CHECK(ClampOrDefault(std::numeric_limits<float>::quiet_NaN(), kDefault, kMin, kMax) == kDefault);
	CHECK(ClampOrDefault(std::numeric_limits<float>::infinity(), kDefault, kMin, kMax) == kDefault);
	CHECK(ClampOrDefault(-std::numeric_limits<float>::infinity(), kDefault, kMin, kMax) == kDefault);
}
