// SPDX-License-Identifier: GPL-3.0-or-later
//
// The captured NPC's reaction to the capture window; see NpcReaction.h.
#include "PCH.h"

#include "NpcReaction.h"

#include "Settings.h"

#include <utility>

// wingdi.h rewrites any GetObject call to GetObjectA/W; BSScript::Variable::GetObject is not one.
#undef GetObject

namespace
{
	// Kept as a handle: the window stays open for many frames, and the actor may unload meanwhile.
	RE::ActorHandle g_actor;  // none while no head is turned

	RE::HighProcessData* HighProcessOf(RE::Actor& a_actor)
	{
		auto* process = a_actor.GetActorRuntimeData().currentProcess;
		return process ? process->high : nullptr;
	}

	// Papyrus ObjectReference.Say(akTopicToSay, akActorToSpeakAs = None, abSpeakInPlayersHead = false),
	// queued on the script VM: CommonLibSSE-NG has no native call for it.
	// Contract: the VM owns the arguments once they are passed in, as SKSE's own event
	// dispatch assumes (CommonLibSSE-NG's SKSE/RegistrationSet.h never deletes them).
	void Say(RE::Actor& a_actor, RE::TESTopic* a_topic)
	{
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) {
			return;
		}
		// Packing finds the actor's script object, or creates and binds one.
		RE::BSScript::Variable self;
		self.Pack(&a_actor);
		auto object = self.GetObject();
		if (!object) {
			logger::warn("NPC Reaction: no script object for {:08X}", a_actor.GetFormID());
			return;
		}
		auto*                                                    args = RE::MakeFunctionArguments(std::move(a_topic), static_cast<RE::Actor*>(nullptr), false);
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
		if (!vm->DispatchMethodCall(object, "Say", args, callback)) {
			logger::warn("NPC Reaction: Say could not be called on {:08X}", a_actor.GetFormID());
		}
	}
}

namespace NpcReaction
{
	void Begin(RE::Actor& a_actor)
	{
		// Skyrim.esm's DialogueGenericHello (quest DialogueGeneric): the greetings an NPC says
		// when the player walks up, each voice type its own. Skyrim.esm always loads at index
		// 00, so its FormID is fixed.
		constexpr RE::FormID kGenericHello = 0x000142B5;

		if (!Settings::NpcReaction() || a_actor.IsPlayerRef() || a_actor.IsDead()) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* high = HighProcessOf(a_actor);
		if (!player || !high) {
			return;
		}
		g_actor = a_actor.GetHandle();
		high->SetHeadtrackTarget(RE::HighProcessData::HEAD_TRACK_TYPES::kScript, player);

		if (auto* topic = RE::TESForm::LookupByID<RE::TESTopic>(kGenericHello)) {
			Say(a_actor, topic);
		} else {
			logger::warn("NPC Reaction: DialogueGenericHello ({:08X}) not found", kGenericHello);
		}
	}

	void End()
	{
		const auto actor = std::exchange(g_actor, {}).get();
		if (auto* high = actor ? HighProcessOf(*actor) : nullptr) {
			high->ClearHeadtrackTarget(RE::HighProcessData::HEAD_TRACK_TYPES::kScript, false);
		}
	}
}
