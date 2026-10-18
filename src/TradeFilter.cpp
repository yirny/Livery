// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Signatures - the bytes expected at each call site, from SPID
//   Thunks     - the original call with Livery's copies marked as outfit items (Assignments)
//   Install    - per runtime, the sites; signature check, then the trampoline call
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

namespace
{
	// --- Signatures -----------------------------------------------------------
	//
	// The call itself, then the instructions after it. A site whose bytes differ belongs to a
	// game build these offsets do not fit, and is left alone.

	constexpr auto kFillListSignature = REL::make_pattern<"E8 ?? ?? ?? ?? 90 48 85 DB">();
	constexpr auto kAddEntrySignatureAE = REL::make_pattern<"E8 ?? ?? ?? ?? 48 83 C7 08 EB 9C">();
	constexpr auto kAddEntrySignatureSE = REL::make_pattern<"E8 ?? ?? ?? ?? 48 83 C7 08 48 8D 94 24 98 00 00 00">();

	// --- Thunks ---------------------------------------------------------------

	// Runs `a_fill`, the engine's filling of a menu list from `a_container`'s inventory, with
	// the copies Livery added to that actor marked as their outfit items, so the engine's own
	// rule leaves them out; the marks are gone when it returns.
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

	// Writes the call at Address Library `a_id` + `a_offset` when the bytes there match
	// `a_signature`. Returns whether it did; a mismatch is logged and the site left as it is.
	template <class Hook, class Signature>
	bool InstallAt(SKSE::Trampoline& a_trampoline, std::uint64_t a_id, std::ptrdiff_t a_offset, const Signature& a_signature)
	{
		const REL::Relocation<std::uintptr_t> site{ REL::ID(a_id), a_offset };
		if (!a_signature.match(site.address())) {
			logger::warn("Trade-menu filter: the code at {}+{:#x} is not the expected call; that menu list is not filtered", a_id, a_offset);
			return false;
		}
		Hook::func = a_trampoline.write_call<5>(site.address(), Hook::thunk);
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
		if (REL::Module::IsAE() && REL::Module::get().version() < SKSE::RUNTIME_SSE_1_6_1130) {
			logger::info("Skyrim AE before 1.6.1130: no trade-menu filter, Livery's copies show in the trade menu");
			return;
		}
		auto& trampoline = SKSE::GetTrampoline();
		if (REL::Module::IsAE()) {
			const bool sites[] = {
				InstallAt<FillList<0>>(trampoline, 51145, 0xEB, kFillListSignature),
				InstallAt<AddEntry>(trampoline, 51146, 0x15B, kAddEntrySignatureAE),
				InstallAt<FillList<1>>(trampoline, 51144, 0x10F, kFillListSignature),
			};
			LogInstalled(sites);
		} else {
			const bool sites[] = {
				InstallAt<FillList<0>>(trampoline, 50216, 0xED, kFillListSignature),
				InstallAt<AddEntry>(trampoline, 50217, 0x152, kAddEntrySignatureSE),
			};
			LogInstalled(sites);
		}
	}
}
