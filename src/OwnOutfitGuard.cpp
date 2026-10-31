// SPDX-License-Identifier: GPL-3.0-or-later
//
// UpdateWornGear, the engine's worn-gear pass, starts by putting the actor's outfit items on. That
// takes a locked piece in the same slot off and clears its lock (docs/design.md §6), after which
// the pass's best-armor step is free to fill the slot. The hook leaves that first step out while
// it would do so. The ID, offsets and signatures are SPID's
// (SPID/src/Outfits/OutfitManager+Hooks.cpp, UpdateWornGear_AddWornOutfit).
#include "PCH.h"

#include "OwnOutfitGuard.h"

#include "Assignments.h"
#include "CallSite.h"
#include "Dressing.h"

namespace
{
	using SignatureSE = CallSite::Signature<"E8 ?? ?? ?? ?? 41 BF 01 00 00 00 4C 89 7C 24 60">;
	using SignatureAE = CallSite::Signature<"E8 ?? ?? ?? ?? 41 BC 01 00 00 00 4C 89 64 24 60">;

	// Contract: the pass's thread is not documented, so this reads only the guarded list of
	// assigned NPCs and this actor's inventory, which the pass itself reads next.
	struct AddWornOutfit
	{
		static void thunk(RE::Actor* a_actor, RE::BGSOutfit* a_outfit, bool a_forceUpdate)
		{
			if (a_actor && Assignments::IsManagedNpc(a_actor->GetFormID()) && Dressing::OutfitItemContestsLock(*a_actor)) {
				return;
			}
			func(a_actor, a_outfit, a_forceUpdate);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace OwnOutfitGuard
{
	void Install()
	{
		if (REL::Module::IsVR()) {
			logger::info("Skyrim VR: no own-outfit guard, an NPC's own outfit items can take their locked pieces off");
			return;
		}
		const bool isAE = REL::Module::IsAE();
		// A missing Address Library ID is a fatal error at lookup. SPID, this ID's source, covers AE
		// from 1.6.640, and the tables at hand hold it from 1.6.1170 on.
		if (isAE && REL::Module::get().version() < SKSE::RUNTIME_SSE_1_6_640) {
			logger::info("AE before 1.6.640: no own-outfit guard, an NPC's own outfit items can take their locked pieces off");
			return;
		}
		const REL::Relocation<std::uintptr_t> function{ REL::RelocationID(24234, 418622) };
		const auto                            offset = isAE ? CallSite::Find<SignatureAE>(function.address(), 0x25E) :
		                                                      CallSite::Find<SignatureSE>(function.address(), 0x1C3);
		if (!offset) {
			logger::warn("Own-outfit guard: no single call matching the signature in the first {:#x} bytes of UpdateWornGear; an NPC's own outfit items can take their locked pieces off",
				CallSite::kScanBytes);
			return;
		}
		AddWornOutfit::func = SKSE::GetTrampoline().write_call<5>(function.address() + *offset, AddWornOutfit::thunk);
		logger::info("Own-outfit guard installed at UpdateWornGear+{:#x}", *offset);
	}
}
