// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

// Finds a call inside an engine function by its byte signature: the call itself, then the
// instructions after it. The Steam executable is DRM-wrapped, so the bytes can only be checked
// at install. The call's target is a wildcard, so a call another plugin hooked first matches too.
namespace CallSite
{
	inline constexpr std::size_t kScanBytes = 0x400;

	template <SKSE::stl::nttp::string S>
	struct Signature
	{
		static constexpr auto        pattern = REL::make_pattern<S>();
		static constexpr std::size_t length = (S.length() + 1) / 3;  // "E8 ?? 90" is 3 bytes
	};

	// Whether the signature is at `a_address`, every byte of it inside the executable's code.
	template <class Sig>
	bool MatchesAt(std::uintptr_t a_address)
	{
		const auto text = REL::Module::get().segment(REL::Segment::textx);
		return a_address >= text.address() && a_address + Sig::length <= text.address() + text.size() &&
		       Sig::pattern.match(a_address);
	}

	// The call's offset in the function at `a_function`: `a_expected` when the signature is
	// there, else the only offset in the first kScanBytes where it is. nullopt when it is
	// nowhere, or in several places.
	template <class Sig>
	std::optional<std::ptrdiff_t> Find(std::uintptr_t a_function, std::ptrdiff_t a_expected)
	{
		if (MatchesAt<Sig>(a_function + a_expected)) {
			return a_expected;
		}
		std::optional<std::ptrdiff_t> found;
		for (std::size_t offset = 0; offset < kScanBytes; ++offset) {
			if (!MatchesAt<Sig>(a_function + offset)) {
				continue;
			}
			if (found) {
				return std::nullopt;
			}
			found = static_cast<std::ptrdiff_t>(offset);
		}
		return found;
	}
}
