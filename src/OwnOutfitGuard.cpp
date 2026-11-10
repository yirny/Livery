// SPDX-License-Identifier: GPL-3.0-or-later
//
// UpdateWornGear, the engine's worn-gear pass, starts by putting the actor's outfit items on:
// their own, or another mod's distributed outfit (SPID). Those are items the trade menu hides, and
// an NPC Livery dresses never wears them (docs/design.md §2): they would also take a locked piece
// in the same slot off and clear its lock (§6). For such an NPC the hook leaves that one call out
// (their outfit items then stayed off, §6); the rest of the pass, its best-armor step included,
// runs as before, and what that step puts on them that Livery keeps off comes off after the
// pass (AfterPass). The ID, offsets and signatures are SPID's
// (SPID/src/Outfits/OutfitManager+Hooks.cpp, UpdateWornGear_AddWornOutfit).
#include "PCH.h"

#include "OwnOutfitGuard.h"

#include "Assignments.h"
#include "CallSite.h"
#include "OutfitPreview.h"

namespace
{
	using SignatureSE = CallSite::Signature<"E8 ?? ?? ?? ?? 41 BF 01 00 00 00 4C 89 7C 24 60">;
	using SignatureAE = CallSite::Signature<"E8 ?? ?? ?? ?? 41 BC 01 00 00 00 4C 89 64 24 60">;

	// Install's argument. Contract: set once at Install, before the hook goes in.
	OwnOutfitGuard::TakenOff g_takenOff = nullptr;

	// After the pass on an NPC Livery has pieces on, the armor its best-armor step put on them
	// that Livery keeps off comes off: while the window's Live Preview shows them, by the
	// preview dressing them in its pieces again; else by Assignments, as Livery's rules say,
	// and when it took any off g_takenOff hears of it.
	// Contract: game thread (the SKSE task the thunk queues).
	void AfterPass(RE::FormID a_actor)
	{
		if (!OutfitPreview::Reapply(a_actor) && Assignments::TakeOffAfterPass(a_actor) && g_takenOff) {
			g_takenOff(a_actor);
		}
	}

	// Contract: the pass's thread is not documented, so this only reads the guarded list of
	// NPCs Livery has pieces on (Assignments::IsDressedNpc) and queues an SKSE task.
	struct AddWornOutfit
	{
		static void thunk(RE::Actor* a_actor, RE::BGSOutfit* a_outfit, bool a_forceUpdate)
		{
			if (a_actor && Assignments::IsDressedNpc(a_actor->GetFormID())) {
				// The best-armor step after this call can still put on their outfit items, or
				// headgear in a slot the outfit leaves empty: they come off after the pass.
				SKSE::GetTaskInterface()->AddTask([id = a_actor->GetFormID()] { AfterPass(id); });
				return;
			}
			func(a_actor, a_outfit, a_forceUpdate);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace OwnOutfitGuard
{
	void Install(TakenOff a_takenOff)
	{
		g_takenOff = a_takenOff;
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
