// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Signatures - the bytes expected at each call site, from SPID, and the search for them
//   Thunks     - the original call with Livery's copies marked as outfit items (Assignments)
//   Install    - per runtime, the sites and SPID's offsets; signature check, then the
//                trampoline call
//
// The functions called at these sites fill a menu's item list from an inventory and skip an
// entry whose ExtraOutfitItem names the actor base's default outfit: that is how the engine
// hides an NPC's own outfit in trade. SPID hooks the same sites for its distributed outfits
// (SPID/src/Outfits/OutfitManager+Hooks.cpp, FilterInventoryItems*), and the IDs, offsets
// and signatures here are its. docs/design.md §3.
#include "PCH.h"

#include "TradeFilter.h"

#include "Assignments.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>

namespace
{
	// --- Signatures -----------------------------------------------------------
	//
	// The call itself, then the instructions after it. A site is hooked only where its
	// signature matches: at SPID's offset, else at the one place in the function's first
	// kScanBytes where it matches, so a game build that moved the call is still filtered and
	// the log says where the call is now. SPID's offsets are all under 0x160.

	constexpr std::size_t kScanBytes = 0x400;

	template <SKSE::stl::nttp::string S>
	struct CallSignature
	{
		static constexpr auto        pattern = REL::make_pattern<S>();
		static constexpr std::size_t length = (S.length() + 1) / 3;  // "E8 ?? 90" is 3 bytes
	};

	using FillListSignature = CallSignature<"E8 ?? ?? ?? ?? 90 48 85 DB">;
	using AddEntrySignatureAE = CallSignature<"E8 ?? ?? ?? ?? 48 83 C7 08 EB 9C">;
	using AddEntrySignatureSE = CallSignature<"E8 ?? ?? ?? ?? 48 83 C7 08 48 8D 94 24 98 00 00 00">;  // AE up to 1.6.629 too

	// Whether the signature is at `a_address`, every byte of it inside the executable's code.
	template <class Signature>
	bool MatchesAt(std::uintptr_t a_address)
	{
		const auto text = REL::Module::get().segment(REL::Segment::textx);
		return a_address >= text.address() && a_address + Signature::length <= text.address() + text.size() &&
		       Signature::pattern.match(a_address);
	}

	// The call's offset in the function at `a_function`: `a_expected` when the signature is
	// there, else the only offset in the first kScanBytes where it is. nullopt when it is
	// nowhere, or in several places.
	template <class Signature>
	std::optional<std::ptrdiff_t> FindCall(std::uintptr_t a_function, std::ptrdiff_t a_expected)
	{
		if (MatchesAt<Signature>(a_function + a_expected)) {
			return a_expected;
		}
		std::optional<std::ptrdiff_t> found;
		for (std::size_t offset = 0; offset < kScanBytes; ++offset) {
			if (!MatchesAt<Signature>(a_function + offset)) {
				continue;
			}
			if (found) {
				return std::nullopt;
			}
			found = static_cast<std::ptrdiff_t>(offset);
		}
		return found;
	}

	// --- Thunks ---------------------------------------------------------------

	// Runs `a_fill`, the engine's filling of a menu list from `a_container`'s inventory, with
	// the copies Livery added to that actor marked as their outfit items, so the engine's own
	// rule leaves them out; the marks are gone when it returns. The actor is noted, so what the
	// player takes from them there goes on them again as the menu closes.
	// Contract: game thread. The menus fill their lists while the engine processes UI messages,
	// on the game thread, where Assignments reads its records.
	void FillHidingCopies(RE::NiPointer<RE::TESObjectREFR>& a_container, auto a_fill)
	{
		auto* ref = a_container.get();
		auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
		if (!actor) {
			a_fill();
			return;
		}
		Assignments::NoteShownInMenu(*actor);
		const auto marked = Assignments::MarkAddedCopies(*actor);
		a_fill();
		Assignments::UnmarkAddedCopies(*actor, marked);
	}

	// The call that fills a list from the whole inventory. Two AE functions make it, one of
	// them with the caller inlined, so each site keeps its own original.
	template <std::size_t Site>
	struct FillList
	{
		static void thunk(RE::ItemList* a_list, RE::InventoryChanges* a_changes, RE::NiPointer<RE::TESObjectREFR>& a_container)
		{
			FillHidingCopies(a_container, [&] { func(a_list, a_changes, a_container); });
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// The call that adds one inventory entry to a list.
	struct AddEntry
	{
		static void thunk(RE::ItemList* a_list, RE::InventoryChanges* a_changes, RE::InventoryEntryData* a_entry,
			RE::NiPointer<RE::TESObjectREFR>& a_container)
		{
			FillHidingCopies(a_container, [&] { func(a_list, a_changes, a_entry, a_container); });
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	// --- Install --------------------------------------------------------------

	// Writes the call into the function at Address Library `a_id`, at `a_offset` or where
	// FindCall finds it. Returns whether it did; a call found nowhere is logged and the site
	// left as it is.
	template <class Hook, class Signature>
	bool InstallAt(SKSE::Trampoline& a_trampoline, std::uint64_t a_id, std::ptrdiff_t a_offset)
	{
		const REL::Relocation<std::uintptr_t> function{ REL::ID(a_id) };
		const auto                            offset = FindCall<Signature>(function.address(), a_offset);
		if (!offset) {
			logger::warn("Trade-menu filter: no single call matching the signature in the first {:#x} bytes of {} (SPID's offset {:#x}); that menu list is not filtered",
				kScanBytes, a_id, a_offset);
			return false;
		}
		if (*offset != a_offset) {
			logger::info("Trade-menu filter: the call in {} is at {:#x}, not at SPID's {:#x}; this game build moved it", a_id, *offset, a_offset);
		}
		Hook::func = a_trampoline.write_call<5>(function.address() + *offset, Hook::thunk);
		return true;
	}

	template <std::size_t N>
	void LogInstalled(const bool (&a_sites)[N])
	{
		logger::info("Trade-menu filter installed at {} of {} call sites", std::ranges::count(a_sites, true), N);
	}
}

namespace TradeFilter
{
	void Install()
	{
		if (REL::Module::IsVR()) {
			logger::info("Skyrim VR: no trade-menu filter, Livery's copies show in the trade menu");
			return;
		}
		auto& trampoline = SKSE::GetTrampoline();
		if (!REL::Module::IsAE()) {
			const bool sites[] = {
				InstallAt<FillList<0>, FillListSignature>(trampoline, 50216, 0xED),
				InstallAt<AddEntry, AddEntrySignatureSE>(trampoline, 50217, 0x152),
			};
			LogInstalled(sites);
			return;
		}
		// SPID's AE offsets: from 1.6.1130 (1.7.x included) all three sites; up to 1.6.629 the
		// first two, the add-entry call still followed by SE's instructions; for 1.6.640 the third
		// only. A site SPID has no offset for starts its search from the 1.6.1130 one.
		const auto version = REL::Module::get().version();
		if (version <= SKSE::RUNTIME_SSE_1_6_629) {
			const bool sites[] = {
				InstallAt<FillList<0>, FillListSignature>(trampoline, 51145, 0xBE),
				InstallAt<AddEntry, AddEntrySignatureSE>(trampoline, 51146, 0x12E),
				InstallAt<FillList<1>, FillListSignature>(trampoline, 51144, 0x10F),
			};
			LogInstalled(sites);
			return;
		}
		const bool sites[] = {
			InstallAt<FillList<0>, FillListSignature>(trampoline, 51145, 0xEB),
			InstallAt<AddEntry, AddEntrySignatureAE>(trampoline, 51146, 0x15B),
			InstallAt<FillList<1>, FillListSignature>(trampoline, 51144, version == SKSE::RUNTIME_SSE_1_6_640 ? 0xE2 : 0x10F),
		};
		LogInstalled(sites);
	}
}
