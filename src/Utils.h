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

	// A copy's name as Windows names one: "<name> - Copy", else "<name> - Copy 2", "- Copy 3", ...,
	// the first for which `taken` is false, in `maxBytes` bytes of UTF-8 at most: a long name is
	// cut short, at a character's start, to keep the words around it whole. `first` words the
	// first ("{0} - Copy", {0} the name) and `numbered` the others ("{0} - Copy {1}", {1} the
	// number), so a language words them its own way.
	// Contract: `taken` is false for some name sooner or later, or this never returns; `first`
	// and `numbered` are format strings naming no value past {0} and {1}.
	template <class Taken>
	[[nodiscard]] std::string CopyName(std::string_view name, std::size_t maxBytes, std::string_view first, std::string_view numbered, Taken&& taken)
	{
		const auto base = TrimWhitespace(name);
		const auto named = [&](std::string_view kept, int number) {
			return std::vformat(number == 1 ? first : numbered, std::make_format_args(kept, number));
		};
		for (int number = 1;; ++number) {
			const auto added = named({}, number).size();  // the bytes the words and the number take
			auto       kept = base;
			if (kept.size() + added > maxBytes) {
				auto cut = maxBytes > added ? maxBytes - added : 0;
				while (cut > 0 && (static_cast<unsigned char>(kept[cut]) & 0xC0) == 0x80) {  // a UTF-8 continuation byte
					--cut;
				}
				kept = TrimWhitespace(kept.substr(0, cut));
			}
			auto copy = named(kept, number);
			if (!taken(copy)) {
				return copy;
			}
		}
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
