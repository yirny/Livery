#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>
#include <string>
#include <string_view>

/**
 * Pure-logic helpers with no RE::/SKSE:: usage, so the `test-linux` and
 * `test-windows` presets can unit-test them without CommonLibSSE.
 * See test/ExampleTests.cpp.
 */
namespace ExampleMod
{

	[[nodiscard]] inline std::string FormatVersion(int major, int minor, int patch)
	{
		return std::format("{}.{}.{}", major, minor, patch);
	}

	// Returns a view of `s` with leading and trailing ASCII whitespace removed.
	// The result views into `s`'s buffer, so the caller must keep that buffer alive
	// for as long as the returned view is used (do not pass a temporary std::string).
	[[nodiscard]] inline std::string_view TrimWhitespace(std::string_view s)
	{
		const auto first = s.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos) {
			return {};
		}
		return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
	}

	[[nodiscard]] inline constexpr unsigned char AsciiToLower(unsigned char c) noexcept
	{
		return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
	}

	// Case-insensitive ASCII string comparison (useful for config keys/values).
	[[nodiscard]] inline bool CaseInsensitiveEqual(std::string_view a, std::string_view b)
	{
		return std::ranges::equal(
			a, b,
			[](unsigned char x, unsigned char y) { return AsciiToLower(x) == AsciiToLower(y); });
	}

	// `text` contains `part`, ASCII case ignored (only ASCII letters fold, as typed text is ASCII
	// only); every text contains the empty part.
	[[nodiscard]] inline bool ContainsIgnoringCase(std::string_view text, std::string_view part)
	{
		const auto equal = [](unsigned char x, unsigned char y) { return AsciiToLower(x) == AsciiToLower(y); };
		return part.empty() || !std::ranges::search(text, part, equal).empty();
	}

	// Sanitises a (typically config-sourced) float: returns `defaultVal` when `value` is
	// non-finite or below `minVal`, clamps to `maxVal` when above, otherwise returns `value`.
	// Precondition: minVal <= defaultVal <= maxVal — these are caller-supplied bounds
	// (normally constants), checked by assert in debug builds.
	[[nodiscard]] inline float ClampOrDefault(float value, float defaultVal, float minVal, float maxVal)
	{
		assert(minVal <= defaultVal && defaultVal <= maxVal);
		if (!std::isfinite(value) || value < minVal) {
			return defaultVal;
		}
		if (value > maxVal) {
			return maxVal;
		}
		return value;
	}

}  // namespace ExampleMod
