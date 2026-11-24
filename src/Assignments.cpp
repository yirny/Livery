// SPDX-License-Identifier: GPL-3.0-or-later
//
// FILE MAP
//   Constants   - the player's reference, co-save ids
//   Records     - one assignment per actor, with its rows (OutfitRows); g_managed mirrors the
//                 NPCs for the event sinks, g_dressed those with pieces on for OwnOutfitGuard
//   Dressing    - which rows a situation resolves to for the base and the layer and the
//                 outfits they wear (a random row's pick), and putting them on in place of
//                 those worn
//   Release     - Livery's locks and copies off, the actor's own pieces back on
//   Load, death - pieces put back on, and armor worn over them taken off, when an assigned NPC
//                 loads, or their own clothes back on when every piece is gone; outfit copies
//                 that came since locked; locks released when one dies; armor put on one in a
//                 menu, or by the engine's worn-gear pass, taken off at once; pieces taken from
//                 one in a menu replaced as it closes
//   Co-save     - save, load (version 6; the rows of situations removed in situations.json
//                 or unknown to this version dropped, but a user's situation's kept while
//                 situations.json cannot be read),
//                 the items the rows' outfits leave off, their selection chances and the layers
//                 in records of their own, revert
//   Event sinks - TESObjectLoadedEvent, TESDeathEvent, TESEquipEvent (while a menu lists an
//                 inventory), MenuOpenCloseEvent (also logs menus opening or closing while
//                 the window is open)
//   API         - RegisterCoSave / RegisterSinks / ReleaseEveryone / Assign / Change / SetRows / SetLayerExcluded / TakeOffLayer / Refresh / PickAnother /
//                 WearFor /
//                 ActorsWithSituations / UsesOf / HoldersOf / ForgetOutfit / ForgetSituation / Unassign / ChoicesOf /
//                 SuspendLocks / ResumeLocks / MarkAddedCopies / UnmarkAddedCopies / NoteShownInMenu / IsDressedNpc /
//                 TakeOffAfterPass
#include "PCH.h"

#include "Assignments.h"

#include "CaptureWindow.h"
#include "Dressing.h"
#include "Library.h"
#include "Settings.h"
#include "SituationStore.h"
#include "SituationTable.h"
#include "Strings.h"

#include <algorithm>
#include <atomic>
#include <format>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
	using OutfitRows::RowOf;
	using Situations::Situation;

	// --- Constants ------------------------------------------------------------

	// Skyrim.esm always loads at index 00, so the player's reference is always 0x14.
	constexpr RE::FormID kPlayerRef = 0x14;

	constexpr std::uint32_t kCoSaveID = 'LVRY';
	constexpr std::uint32_t kAssignmentsRecord = 'ASGN';
	constexpr std::uint32_t kAssignmentsVersion = 6;  // the only version read
	constexpr std::uint32_t kExclusionsRecord = 'EXCL';
	constexpr std::uint32_t kExclusionsVersion = 2;  // the only version read
	constexpr std::uint32_t kChancesRecord = 'CHNC';
	constexpr std::uint32_t kChancesVersion = 1;  // the only version read
	constexpr std::uint32_t kLayersRecord = 'LAYR';
	constexpr std::uint32_t kLayersVersion = 1;  // the only version read

	// --- Records --------------------------------------------------------------

	// A row stored under the key (Situations::Name) of a situation this session does not have.
	struct KeptRow
	{
		std::string     key;
		OutfitRows::Row row;
	};

	// An actor Livery dresses: the pieces it has on them, their own pieces it took off, and
	// their rows, the Default and the situations. NPCs wear the pieces locked, and their
	// outfit copies carry a lock while they wear pieces (Dressing::LockOutfitCopies); the
	// player does not, since a locked piece cannot be taken off in the inventory menu.
	struct Assignment
	{
		RE::FormID                         actor = 0;
		std::vector<Dressing::Piece>       pieces;        // on them now, for `worn`
		std::vector<Dressing::InstanceKey> takenOff;      // their own copies, put back on at release
		std::vector<Dressing::InstanceKey> lockedCopies;  // their outfit copies Livery locked unworn, unlocked at release
		OutfitRows::Rows                   rows;          // the Default, then the situations
		// The Default's armor not in the wardrobe, worn while its row wears no outfit; empty
		// for none (their own clothes).
		std::vector<RE::FormID> defaultPieces;
		Situation               worn = Situation::kDefault;  // the row the base of `pieces` is for
		std::uint32_t           wornOutfit = 0;              // the library outfit the base is from; 0 for none or the Default's pieces
		// The row the layer of `pieces` is from (OutfitRows::ResolveLayer); nullopt while no row
		// has a layer for them. With `layer` 0 that row drew none, which it keeps while it stays
		// the layer row, as a random row keeps its pick.
		std::optional<Situation> layerRow;
		std::uint32_t            layer = 0;  // the layer worn over the base; 0 for none
		// The layer is on over their own clothes (no row has a base for them): their other armor
		// stays on (Dressing::Dress, TakeOffExtras's `keepOthers`).
		bool overOwn = false;
		// The rows of user's situations this session does not have, as the co-save held them
		// while situations.json could not be read (SituationTable::KeepsUnknownRow), written
		// back on save; never worn or shown. Each holds something (HoldsSomething).
		std::vector<KeptRow> kept;
	};

	// Contract: game thread only.
	std::vector<Assignment> g_assignments;

	// Contract: game thread only. Draws the random rows' picks.
	std::mt19937 g_random{ std::random_device{}() };

	// Contract: the assigned NPCs, for the event sinks, and those Livery has pieces on, for
	// OwnOutfitGuard. The thread that sends TESObjectLoadedEvent and TESDeathEvent is not
	// documented, nor is the worn-gear pass's, so the sinks and the hook read only these, under
	// their lock, and the sinks queue the work as SKSE tasks.
	std::mutex              g_managedLock;
	std::vector<RE::FormID> g_managed;
	std::vector<RE::FormID> g_dressed;

	// Contract: game thread only. The NPC whose locks are off for the preview, or 0.
	RE::FormID g_suspended = 0;

	// The assigned NPC whose inventory the open trade, container or pickpocket menu lists, or
	// 0. Contract: written on the game thread (NoteShownInMenu), taken by MenuSink, whose
	// thread is not documented, hence atomic.
	std::atomic<RE::FormID> g_shownInMenu{ 0 };

	Assignment* FindAssignment(RE::FormID a_actor)
	{
		const auto found = std::ranges::find(g_assignments, a_actor, &Assignment::actor);
		return found != g_assignments.end() ? &*found : nullptr;
	}

	// Contract: the pointer goes stale at the next change of g_assignments.
	Assignment& FindOrAddAssignment(RE::FormID a_actor)
	{
		if (auto* found = FindAssignment(a_actor)) {
			return *found;
		}
		return g_assignments.emplace_back(Assignment{ .actor = a_actor });
	}

	bool HasSituations(const Assignment& a_assignment)
	{
		return std::ranges::any_of(Situations::Order(), [&a_assignment](Situation a_situation) { return OutfitRows::HoldsAny(a_assignment.rows[RowOf(a_situation)]); });
	}

	// A row keeps the record while it holds an outfit, even one that puts nothing on (none of
	// its items loaded), or is random with nothing on its list yet, so the window shows the row
	// as the user left it.
	bool HoldsSomething(const OutfitRows::Row& a_row)
	{
		return a_row.random || OutfitRows::HoldsAny(a_row);
	}

	// An assignment with nothing on the actor and no row, kept ones included, holding
	// something is no assignment: they keep what they wear.
	// Contract: every record dropped as empty goes through this test (DropIfEmpty, the
	// co-save load, ForgetOutfit, ForgetSituation).
	bool IsEmpty(const Assignment& a_assignment)
	{
		return a_assignment.pieces.empty() && a_assignment.kept.empty() && std::ranges::none_of(a_assignment.rows, HoldsSomething);
	}

	void DropIfEmpty(RE::FormID a_actor)
	{
		std::erase_if(g_assignments, [a_actor](const Assignment& a_assignment) { return a_assignment.actor == a_actor && IsEmpty(a_assignment); });
	}

	// Contract: game thread, after every change of g_assignments, each dressing (Redress)
	// included, since it can leave an assignment with pieces or with none. The player is left
	// out: nothing is put back on them when they load.
	void SyncManaged()
	{
		std::lock_guard lock{ g_managedLock };
		g_managed.clear();
		g_dressed.clear();
		for (const auto& assignment : g_assignments) {
			if (assignment.actor == kPlayerRef) {
				continue;
			}
			g_managed.push_back(assignment.actor);
			if (!assignment.pieces.empty()) {
				g_dressed.push_back(assignment.actor);
			}
		}
	}

	bool IsManaged(RE::FormID a_actor)
	{
		std::lock_guard lock{ g_managedLock };
		return std::ranges::find(g_managed, a_actor) != g_managed.end();
	}

	void Notify(const std::string& text)
	{
		RE::SendHUDMessage::ShowHUDMessage(text.c_str());
		logger::info("{}", text);
	}

	std::string NameOf(RE::Actor& actor)
	{
		const auto* name = actor.GetDisplayFullName();
		return name && *name ? name : std::format("{:08X}", actor.GetFormID());
	}

	std::string JoinNames(const std::vector<RE::TESObjectARMO*>& armors)
	{
		std::string joined;
		for (const auto* armor : armors) {
			const auto* name = armor->GetName();
			joined += (joined.empty() ? "" : Strings::Get(Strings::Text::kListSeparator)) + (name && *name ? std::string{ name } : std::format("{:08X}", armor->GetFormID()));
		}
		return joined;
	}

	// The HUD's sentences are built in the language's own order (LIVERY_STRINGS_HUD_DRESSING):
	// a main clause, the notes below, each taking the sentence so far, and EndOf.

	// `a_text` with what the HUD adds about a dressing: the pieces left out and those the engine
	// refused.
	std::string WithDressingNotes(std::string a_text, const Dressing::Result& a_result)
	{
		if (!a_result.notCarried.empty()) {
			a_text = Strings::Format(Strings::Text::kHudNoteNotCarried, a_text, JoinNames(a_result.notCarried));
		}
		if (!a_result.refused.empty()) {
			a_text = Strings::Format(Strings::Text::kHudNoteRefused, a_text, JoinNames(a_result.refused));
		}
		return a_text;
	}

	std::string EndOf(const std::string& a_text)
	{
		return Strings::Format(Strings::Text::kHudEnd, a_text);
	}

	std::string JoinSituations(const std::vector<Situation>& a_situations)
	{
		std::string joined;
		for (const auto situation : a_situations) {
			joined += (joined.empty() ? "" : Strings::Get(Strings::Text::kListSeparator)) + SituationStore::Name(situation);
		}
		return joined;
	}

	// The outfit's name in the language's quotes; its words for one the Wardrobe no longer holds.
	std::string QuotedOutfitName(std::uint32_t a_outfit)
	{
		const auto* stored = Library::Find(a_outfit);
		return stored ? Strings::Format(Strings::Text::kQuotedName, stored->name) : Strings::Get(Strings::Text::kOutfitGone);
	}

	// What they wear as the HUD names it while a layer is on, `"Cloak" over "Guard"`: the layer
	// over the base's outfit, the default items, or their own clothes. nullopt with no layer on.
	std::optional<std::string> LayeredText(RE::Actor& a_actor, const Assignment& a_assignment)
	{
		using Strings::Text;
		if (a_assignment.layer == 0 || a_assignment.pieces.empty()) {
			return std::nullopt;
		}
		std::string base = Strings::Get(Text::kHudDefaultItems);
		if (a_assignment.overOwn) {
			base = Strings::Get(a_actor.IsPlayerRef() ? Text::kOwnClothesYour : Text::kOwnClothesTheir);
		} else if (a_assignment.wornOutfit != 0) {
			base = QuotedOutfitName(a_assignment.wornOutfit);
		}
		return Strings::Format(Text::kOutfitOver, QuotedOutfitName(a_assignment.layer), base);
	}

	// The HUD line for a redress because a situation began or ended (WearFor): what they wear
	// now, and the situation it is for; with a layer on, both outfits, for the base's situation.
	std::string SituationChangeText(RE::Actor& a_actor, const Assignment& a_assignment)
	{
		using Strings::Text;
		const bool player = a_actor.IsPlayerRef();
		const auto name = NameOf(a_actor);
		if (a_assignment.pieces.empty()) {
			return Strings::Format(player ? Text::kHudYouWearOwn : Text::kHudWearsOwn, name);
		}
		if (const auto layered = LayeredText(a_actor, a_assignment)) {
			if (a_assignment.worn == Situation::kDefault || a_assignment.overOwn) {
				return Strings::Format(player ? Text::kHudYouWearNow : Text::kHudWearsNow, name, *layered);
			}
			return Strings::Format(player ? Text::kHudYouWearFor : Text::kHudWearsFor, name, *layered, SituationStore::Name(a_assignment.worn));
		}
		if (a_assignment.worn == Situation::kDefault) {
			return Strings::Format(player ? Text::kHudYouWearDefault : Text::kHudWearsDefault, name);
		}
		return Strings::Format(player ? Text::kHudYouWearFor : Text::kHudWearsFor, name, QuotedOutfitName(a_assignment.wornOutfit),
			SituationStore::Name(a_assignment.worn));
	}

	std::vector<RE::FormID> FormIDsOf(const std::vector<RE::TESObjectARMO*>& a_armor)
	{
		std::vector<RE::FormID> ids;
		for (const auto* armor : a_armor) {
			ids.push_back(armor->GetFormID());
		}
		return ids;
	}

	// --- Dressing -------------------------------------------------------------
	//
	// Contract: game thread, all.

	// A number below `a_count`, for OutfitRows::Pick's draw.
	// Contract: `a_count` is above 0.
	std::size_t RandomBelow(std::size_t a_count)
	{
		return std::uniform_int_distribution<std::size_t>{ 0, a_count - 1 }(g_random);
	}

	// Locks the outfit copies of an NPC Livery has pieces on that are not locked yet: after a
	// dressing, and those that came since (another mod's outfit change, the copies a take-off
	// just took off). A log line when it locked any.
	void LockNewOutfitCopies(Assignment& a_assignment, RE::Actor& a_actor)
	{
		if (a_assignment.pieces.empty() || a_actor.IsPlayerRef()) {
			return;
		}
		if (const auto count = Dressing::LockOutfitCopies(a_actor, a_assignment.pieces, a_assignment.lockedCopies); count > 0) {
			logger::info("Locked {} of their outfit item(s) on {:08X}", count, a_actor.GetFormID());
		}
	}

	// How Redress dresses them, besides the situations they are in.
	struct RedressOptions
	{
		bool                            force = false;  // dress them even when they wear what is due
		std::vector<RE::TESObjectARMO*> leaveOut;       // stays out of this dressing only
		// Outfits just given to `givenRows` (Assign, Change), or picked for them (Refresh): a
		// random one of them worn now wears `givenBase` as its base, and `givenLayer` as its
		// layer (0 for a draw of none).
		std::uint32_t                givenBase = 0;
		std::optional<std::uint32_t> givenLayer;
		std::vector<Situation>       givenRows;
	};

	bool InLibrary(std::uint32_t a_outfit)
	{
		return Library::Find(a_outfit) != nullptr;
	}

	// Whether library outfit `a_outfit` is a layer now (OutfitRows' `a_isLayer`); one gone from
	// the library is not.
	bool IsLayerOutfit(std::uint32_t a_outfit)
	{
		const auto* outfit = Library::Find(a_outfit);
		return outfit && outfit->layer;
	}

	// Whether outfit `a_outfit` has an item loaded (OutfitRows' `a_wearable`): a layer with none
	// is passed over, so another can be worn in its place.
	bool WearableLayer(std::uint32_t a_outfit)
	{
		const auto* outfit = Library::Find(a_outfit);
		return outfit && Library::HasLoadedPiece(*outfit);
	}

	std::uint32_t SlotsOf(const RE::TESObjectARMO* a_armor)
	{
		return a_armor->GetSlotMask().underlying();
	}

	// RedressOptions for outfit `a_outfit` just given to `a_rows`: as their base, or as their
	// layer when it is one.
	RedressOptions GivenTo(std::uint32_t a_outfit, std::vector<Situation> a_rows)
	{
		RedressOptions options{ .givenRows = std::move(a_rows) };
		if (IsLayerOutfit(a_outfit)) {
			options.givenLayer = a_outfit;
		} else {
			options.givenBase = a_outfit;
		}
		return options;
	}

	// Their rows as dressing sees them (OutfitRows::WithoutGone): an outfit another save's row
	// names after it left the library is never picked, and a situation row with only such
	// outfits is passed over. The record keeps them.
	OutfitRows::Rows LiveRows(const Assignment& a_assignment)
	{
		auto rows = a_assignment.rows;
		for (auto& row : rows) {
			row = OutfitRows::WithoutGone(std::move(row), InLibrary);
		}
		return rows;
	}

	bool IsGivenTo(const RedressOptions& a_options, Situation a_row)
	{
		return std::ranges::find(a_options.givenRows, a_row) != a_options.givenRows.end();
	}

	// The base row `a_row` of `a_rows` (LiveRows) wears now, 0 for none (OutfitRows::Wears): a
	// random row keeps the base they wear for it while it is on the list, or wears the one just
	// given.
	std::uint32_t OutfitFor(const Assignment& a_assignment, const OutfitRows::Rows& a_rows, Situation a_row, const RedressOptions& a_options)
	{
		const bool given = a_options.givenBase != 0 && IsGivenTo(a_options, a_row);
		const auto kept = given ? a_options.givenBase : (a_assignment.worn == a_row ? a_assignment.wornOutfit : 0);
		return OutfitRows::Wears(a_rows[RowOf(a_row)], kept, RandomBelow, IsLayerOutfit);
	}

	// The layer row `a_row` of `a_rows` (LiveRows) wears now, 0 for none (OutfitRows::WearsLayer):
	// a random row keeps what it drew while it stays the layer row (none included), or wears the
	// one just given.
	std::uint32_t LayerFor(const Assignment& a_assignment, const OutfitRows::Rows& a_rows, Situation a_row, const RedressOptions& a_options)
	{
		std::optional<std::uint32_t> kept;
		if (a_options.givenLayer && IsGivenTo(a_options, a_row)) {
			kept = a_options.givenLayer;
		} else if (a_assignment.layerRow == a_row) {
			kept = a_assignment.layer;
		}
		return OutfitRows::WearsLayer(a_rows[RowOf(a_row)], kept, RandomBelow, IsLayerOutfit, WearableLayer);
	}

	// What a dressing puts on: the base from row `row` (`outfit`, 0 for none or the Default's
	// pieces) and, with `layerRow`, the layer from that row over it (`layer`, 0 for a draw of
	// none).
	struct Due
	{
		Situation                row = Situation::kDefault;
		std::uint32_t            outfit = 0;
		std::optional<Situation> layerRow;
		std::uint32_t            layer = 0;
	};

	// What is due for `a_now`, the rows as LiveRows sees them.
	Due DueFor(const Assignment& a_assignment, const OutfitRows::Rows& a_rows, const Situations::Applicable& a_now, const RedressOptions& a_options)
	{
		Due due{ .row = OutfitRows::Resolve(a_rows, a_now, IsLayerOutfit) };
		due.outfit = OutfitFor(a_assignment, a_rows, due.row, a_options);
		due.layerRow = OutfitRows::ResolveLayer(a_rows, a_now, IsLayerOutfit, WearableLayer);
		if (due.layerRow) {
			due.layer = LayerFor(a_assignment, a_rows, *due.layerRow, a_options);
		}
		return due;
	}

	bool IsWorn(const Assignment& a_assignment, const Due& a_due)
	{
		return a_due.row == a_assignment.worn && a_due.outfit == a_assignment.wornOutfit && a_due.layerRow == a_assignment.layerRow &&
		       a_due.layer == a_assignment.layer;
	}

	// The armor row `a_row` wears as `a_outfit`: the library outfit's loaded pieces less those
	// it leaves off on the row (OutfitRows::WornPieces), or with none the Default's pieces.
	// Empty for an outfit gone from the library or nothing to wear.
	std::vector<RE::TESObjectARMO*> ArmorFor(const Assignment& a_assignment, Situation a_row, std::uint32_t a_outfit)
	{
		if (a_outfit != 0) {
			const auto* outfit = Library::Find(a_outfit);
			if (!outfit) {
				return {};
			}
			const auto excluded = OutfitRows::ExcludedOf(a_assignment.rows[RowOf(a_row)], a_outfit);
			return OutfitRows::WornPieces(Library::LoadedPieces(*outfit), excluded, [](const RE::TESObjectARMO* a_piece) { return a_piece->GetFormID(); });
		}
		std::vector<RE::TESObjectARMO*> armor;
		if (a_row != Situation::kDefault) {
			return armor;
		}
		for (const auto id : a_assignment.defaultPieces) {
			if (auto* loaded = RE::TESForm::LookupByID<RE::TESObjectARMO>(id)) {
				armor.push_back(loaded);
			}
		}
		return armor;
	}

	// A dressing's result, and how its base fared: Redress falls back to the Default's base
	// when a situation's base put nothing on.
	struct Worn
	{
		Dressing::Result result;
		bool             baseOn = false;      // a piece of the base went on, or the layer covers all of them
		std::size_t      baseNotCarried = 0;  // the base's pieces left out: not carried and adding was off
		std::size_t      baseRefused = 0;     // the base's pieces the engine would not put on
	};

	// Puts `a_due` on them in place of what Livery has on them, less `a_leaveOut`: the base's
	// pieces whose body slots no layer piece takes, then the layer's (OutfitRows::Uncovered).
	// With no base but a layer, the layer goes over their own clothes, which stay on. Their own
	// pieces from before go back on first, so they are what this dressing takes off and notes.
	// With no armor for the rows they end up in their own clothes. An NPC's outfit copies are
	// locked again once pieces are on. A random row's picks are recorded as its `chosen` and
	// `chosenLayer`, which turning random off keeps on (OutfitRows::SetRandom).
	Worn Wear(Assignment& a_assignment, RE::Actor& a_actor, const Due& a_due, const std::vector<RE::TESObjectARMO*>& a_leaveOut)
	{
		Dressing::Release(a_actor, a_assignment.pieces, true);
		// Before PutBack and Dress, which skip a locked unworn copy: their outfit items can be
		// among the copies taken off, or a piece of the new outfit.
		Dressing::UnlockOutfitCopies(a_actor, a_assignment.lockedCopies);
		a_assignment.lockedCopies.clear();
		Dressing::PutBack(a_actor, a_assignment.takenOff);
		const auto without = [&a_leaveOut](std::vector<RE::TESObjectARMO*> a_armor) {
			std::erase_if(a_armor, [&a_leaveOut](const RE::TESObjectARMO* a_piece) { return std::ranges::find(a_leaveOut, a_piece) != a_leaveOut.end(); });
			return a_armor;
		};
		const auto base = without(ArmorFor(a_assignment, a_due.row, a_due.outfit));
		const auto layer = a_due.layerRow && a_due.layer != 0 ? without(ArmorFor(a_assignment, *a_due.layerRow, a_due.layer)) : std::vector<RE::TESObjectARMO*>{};
		const auto uncovered = OutfitRows::Uncovered(base, layer, SlotsOf);
		auto       armor = uncovered;
		armor.insert(armor.end(), layer.begin(), layer.end());
		const bool overOwn = base.empty() && !layer.empty();
		const bool isPlayer = a_actor.IsPlayerRef();
		Worn       worn{ .result = Dressing::Dress(a_actor, armor, !isPlayer, !isPlayer || Settings::PlayerAddsMissingPieces(), overOwn) };
		const auto inBase = [&uncovered](const RE::TESObjectARMO* a_piece) { return std::ranges::find(uncovered, a_piece) != uncovered.end(); };
		const auto baseWorn = [&uncovered](const Dressing::Piece& a_piece) {
			return std::ranges::any_of(uncovered, [&a_piece](const RE::TESObjectARMO* a_armor) { return a_armor->GetFormID() == a_piece.key.armor; });
		};
		worn.baseOn = uncovered.empty() ? !base.empty() : std::ranges::any_of(worn.result.worn, baseWorn);
		worn.baseNotCarried = static_cast<std::size_t>(std::ranges::count_if(worn.result.notCarried, inBase));
		worn.baseRefused = static_cast<std::size_t>(std::ranges::count_if(worn.result.refused, inBase));
		a_assignment.pieces = worn.result.worn;
		a_assignment.takenOff = worn.result.takenOff;
		a_assignment.worn = a_due.row;
		a_assignment.wornOutfit = a_due.outfit;
		a_assignment.layerRow = a_due.layerRow;
		a_assignment.layer = a_due.layer;
		a_assignment.overOwn = overOwn && !worn.result.worn.empty();
		LockNewOutfitCopies(a_assignment, a_actor);
		if (auto& row = a_assignment.rows[RowOf(a_due.row)]; row.random && a_due.outfit != 0) {
			row.chosen = a_due.outfit;  // on the list: OutfitRows::Wears picks from it
		}
		if (a_due.layerRow && a_due.layer != 0) {
			if (auto& row = a_assignment.rows[RowOf(*a_due.layerRow)]; row.random) {
				row.chosenLayer = a_due.layer;  // a draw of none leaves the last one
			}
		}
		return worn;
	}

	// Why a situation outfit put nothing on, so the Default went on instead (Redress).
	struct Fallback
	{
		Situation     row = Situation::kDefault;
		std::uint32_t outfit = 0;
		std::size_t   notCarried = 0;  // its pieces left out: not carried and adding was off
		std::size_t   refused = 0;     // its pieces the engine would not put on
		bool          loaded = false;  // any of its pieces is loaded
	};

	struct Redressed
	{
		Dressing::Result        result;    // the last dressing's: the Default's after a fallback
		std::optional<Fallback> fallback;  // set when the situation outfit put nothing on
	};

	// The HUD's clause for a fallback, without "Livery: " or the full stop: "you carry none of
	// the items of "Noble Robes" for Castle".
	std::string FallbackText(RE::Actor& a_actor, const Fallback& a_fallback)
	{
		using Strings::Text;
		auto text = Text::kHudFallbackRefused;
		if (!a_fallback.loaded) {
			text = Text::kHudFallbackNotLoaded;
		} else if (a_actor.IsPlayerRef()) {
			text = a_fallback.notCarried > 0 && a_fallback.refused == 0 ? Text::kHudFallbackNotCarried : Text::kHudFallbackRefusedYou;
		}
		return Strings::Format(text, QuotedOutfitName(a_fallback.outfit), SituationStore::Name(a_fallback.row), NameOf(a_actor));
	}

	// `a_text` with the notes of the last dressing (WithDressingNotes), then why the situation
	// outfit gave way, if it did.
	std::string WithRedressNotes(std::string a_text, RE::Actor& a_actor, const Redressed& a_redressed)
	{
		a_text = WithDressingNotes(std::move(a_text), a_redressed.result);
		if (a_redressed.fallback) {
			a_text = Strings::Format(Strings::Text::kHudNoteJoin, a_text, FallbackText(a_actor, *a_redressed.fallback));
		}
		return a_text;
	}

	// Dresses them for `a_now` unless they already wear what it is due (DueFor; `force` dresses
	// them anyway), the rows as LiveRows sees them. A situation's base that puts nothing on falls
	// back to the Default's, under the same layer, with a log line that says why. Returns the
	// dressing's result, or nullopt when nothing changed; the assignment's `worn`, `wornOutfit`,
	// `layerRow` and `layer` tell what went on.
	std::optional<Redressed> Redress(Assignment& a_assignment, RE::Actor& a_actor, const Situations::Applicable& a_now, const RedressOptions& a_options = {})
	{
		const auto rows = LiveRows(a_assignment);
		auto       due = DueFor(a_assignment, rows, a_now, a_options);
		if (!a_options.force && IsWorn(a_assignment, due)) {
			return std::nullopt;
		}
		// Read before Wear, which changes what they wear and so a random Default's kept pick.
		const auto defaultOutfit = due.row != Situation::kDefault ? OutfitFor(a_assignment, rows, Situation::kDefault, a_options) : due.outfit;
		auto       worn = Wear(a_assignment, a_actor, due, a_options.leaveOut);
		Redressed  redressed{ .result = std::move(worn.result) };
		if (!worn.baseOn && due.row != Situation::kDefault) {
			const auto* stored = Library::Find(due.outfit);
			redressed.fallback = Fallback{ .row = due.row,
				.outfit = due.outfit,
				.notCarried = worn.baseNotCarried,
				.refused = worn.baseRefused,
				.loaded = stored && Library::HasLoadedPiece(*stored) };
			logger::info("No piece of the {} outfit \"{}\" went on {:08X}: {} not carried, {} could not be put on; the default outfit instead", Situations::Name(due.row),
				stored ? stored->name : "no longer in the Wardrobe", a_actor.GetFormID(), worn.baseNotCarried, worn.baseRefused);
			due.row = Situation::kDefault;
			due.outfit = defaultOutfit;
			redressed.result = Wear(a_assignment, a_actor, due, a_options.leaveOut).result;
		}
		return redressed;
	}

	// Children and the dead are never dressed; a HUD message says why.
	bool CanBeDressed(RE::Actor& a_actor)
	{
		if (a_actor.IsChild()) {
			Notify(Strings::Get(Strings::Text::kHudChildrenNotDressed));
			return false;
		}
		if (a_actor.IsDead()) {
			Notify(Strings::Format(Strings::Text::kHudDead, NameOf(a_actor)));
			return false;
		}
		return true;
	}

	// Row `a_row` gets library outfit `a_outfit`, leaving `a_excluded` off (OutfitRows::Give);
	// a base made the Default's `chosen` with random off drops the Default's pieces (a random
	// Default keeps them for an empty list; a layer goes over them). Nobody is dressed here.
	void GiveRow(Assignment& a_assignment, Situation a_row, std::uint32_t a_outfit, const std::vector<RE::FormID>& a_excluded = {})
	{
		auto& row = a_assignment.rows[RowOf(a_row)];
		OutfitRows::Give(row, a_outfit, a_excluded, IsLayerOutfit);
		if (a_row == Situation::kDefault && !row.random && row.chosen == a_outfit) {
			a_assignment.defaultPieces.clear();
		}
	}

	// The rows Change makes: the removed ones lose the outfit, the assigned ones get it.
	// Nobody is dressed here.
	// Contract: no row is in both lists.
	void ApplyChanges(Assignment& a_assignment, const Assignments::Changes& a_changes)
	{
		for (const auto row : a_changes.remove) {
			OutfitRows::Take(a_assignment.rows[RowOf(row)], a_changes.outfit);
		}
		for (const auto row : a_changes.assign) {
			GiveRow(a_assignment, row, a_changes.outfit, a_changes.excluded);
		}
	}

	// The outfit of one kind (a layer with `a_layer`, else a base) row `a_row` gains first in
	// `a_rows` while it is random; 0 for none.
	std::uint32_t Gained(const Assignment& a_assignment, const OutfitRows::Rows& a_rows, Situation a_row, bool a_layer)
	{
		const auto& before = a_assignment.rows[RowOf(a_row)].outfits;
		const auto& after = a_rows[RowOf(a_row)];
		if (!after.random) {
			return 0;
		}
		const auto gained = std::ranges::find_if(after.outfits, [&before, a_layer](std::uint32_t a_outfit) {
			return IsLayerOutfit(a_outfit) == a_layer && std::ranges::find(before, a_outfit) == before.end();
		});
		return gained != after.outfits.end() ? *gained : 0;
	}

	// The outfits the random rows they wear now gain first in `a_rows`, which their next
	// dressing wears as the picks (docs/situation-management-ui.md §1): the base the row of
	// their base gains, and the layer that row gains, else the row of their layer; none while
	// nothing is on them.
	Assignments::Picks GainedPicks(const Assignment& a_assignment, const OutfitRows::Rows& a_rows)
	{
		Assignments::Picks picks;
		if (a_assignment.pieces.empty()) {
			return picks;
		}
		picks.base = Gained(a_assignment, a_rows, a_assignment.worn, false);
		auto layer = Gained(a_assignment, a_rows, a_assignment.worn, true);
		if (layer == 0 && a_assignment.layerRow) {
			layer = Gained(a_assignment, a_rows, *a_assignment.layerRow, true);
		}
		if (layer != 0) {
			picks.layer = layer;
		}
		return picks;
	}

	// The library outfit's loaded pieces not in `a_armor`.
	std::vector<RE::TESObjectARMO*> PiecesNotIn(std::uint32_t a_outfit, const std::vector<RE::TESObjectARMO*>& a_armor)
	{
		const auto* outfit = Library::Find(a_outfit);
		auto        pieces = outfit ? Library::LoadedPieces(*outfit) : std::vector<RE::TESObjectARMO*>{};
		std::erase_if(pieces, [&a_armor](const RE::TESObjectARMO* a_piece) { return std::ranges::find(a_armor, a_piece) != a_armor.end(); });
		return pieces;
	}

	// --- Release --------------------------------------------------------------

	// Takes off what Livery did to them and forgets them. `removeCopies` also takes back
	// the copies it added; `putBack` puts their own pieces it took off on again, which a
	// corpse does not get: it behaves like vanilla. Returns how many went back on.
	// Contract: game thread.
	std::size_t ReleaseActor(RE::Actor& actor, bool removeCopies, bool putBack)
	{
		const auto found = std::ranges::find(g_assignments, actor.GetFormID(), &Assignment::actor);
		if (found == g_assignments.end()) {
			return 0;
		}
		const auto assignment = std::move(*found);
		g_assignments.erase(found);
		SyncManaged();
		Dressing::Release(actor, assignment.pieces, removeCopies);
		Dressing::UnlockOutfitCopies(actor, assignment.lockedCopies);  // before PutBack, which skips a locked copy
		return putBack ? Dressing::PutBack(actor, assignment.takenOff) : 0;
	}

	// --- Load, death ----------------------------------------------------------
	//
	// Contract: game thread (SKSE tasks from the event sinks), all.

	// None of the pieces Livery put on them is in their inventory any more (an outfit change,
	// an inventory reset, a script that took their items): the guard would keep their outfit
	// items off and leave them naked, so their own clothes go back on, as Restore leaves them.
	// The record keeps their rows: a situation row dresses them again at their next change of
	// situation; with no row holding an outfit or random it ends (DropIfEmpty), and the next
	// Apply dresses them.
	// Contract: `a_assignment` is stale after this.
	void ReturnToOwnClothes(Assignment& a_assignment, RE::Actor& a_actor)
	{
		const auto id = a_actor.GetFormID();
		Dressing::UnlockOutfitCopies(a_actor, a_assignment.lockedCopies);  // before PutBack, which skips a locked copy
		const auto putBack = Dressing::PutBack(a_actor, a_assignment.takenOff);
		const auto outfitItems = Dressing::PutOnOutfitItems(a_actor);
		a_assignment.pieces.clear();
		a_assignment.takenOff.clear();
		a_assignment.lockedCopies.clear();
		a_assignment.overOwn = false;
		logger::info("None of Livery's pieces on {:08X} is in their inventory any more; {} of their own piece(s) and {} outfit item(s) went back on", id,
			putBack, outfitItems);
		DropIfEmpty(id);
		SyncManaged();
	}

	// When they carry none of the pieces Livery put on them any more, their own clothes go back
	// on (ReturnToOwnClothes). A follower going through a door with the player, or an item
	// taken from them, need not reload their 3D (docs/design.md §6), so this runs at their 3D
	// load, at a menu's close after the replenish, and at each take-off check after the
	// worn-gear pass. Returns whether it did; `a_assignment` is stale then.
	bool ReturnIfPiecesGone(Assignment& a_assignment, RE::Actor& a_actor)
	{
		if (a_assignment.pieces.empty() || Dressing::CarriesAny(a_actor, a_assignment.pieces)) {
			return false;
		}
		ReturnToOwnClothes(a_assignment, a_actor);
		return true;
	}

	void PutPiecesBackOn(RE::FormID a_actor)
	{
		if (a_actor == g_suspended) {
			return;  // the preview has them off on purpose
		}
		auto* assignment = FindAssignment(a_actor);
		auto* actor = assignment ? RE::TESForm::LookupByID<RE::Actor>(a_actor) : nullptr;
		if (!actor) {
			return;
		}
		if (ReturnIfPiecesGone(*assignment, *actor)) {
			return;
		}
		if (const auto count = Dressing::PutBackOn(*actor, assignment->pieces); count > 0) {
			logger::info("Put {} piece(s) back on or locked again on {:08X}", count, a_actor);
		}
		// After PutBackOn, so the pieces it put on again count as worn.
		if (const auto count = Dressing::TakeOffExtras(*actor, assignment->pieces, assignment->lockedCopies, assignment->overOwn); count > 0) {
			logger::info("Took off {} item(s) worn outside Livery's pieces on {:08X}", count, a_actor);
		}
		LockNewOutfitCopies(*assignment, *actor);
	}

	// The pieces the player took from them in the menu that just closed go on again, as
	// copies when they carry none, so the outfit does not change by itself; with none of the
	// pieces left after that (only Livery's copies were taken, which are not replaced), their
	// own clothes go back on. Then a better copy the player gave them goes on in place of a
	// worn piece, and armor the engine put on them outside the pieces (a helmet given for a
	// slot the outfit leaves empty) comes off.
	void ReplenishTaken(RE::FormID a_actor)
	{
		if (a_actor == g_suspended) {
			return;  // the preview has them off on purpose
		}
		auto* assignment = FindAssignment(a_actor);
		auto* actor = assignment ? RE::TESForm::LookupByID<RE::Actor>(a_actor) : nullptr;
		if (!actor || actor->IsDead()) {
			return;
		}
		if (const auto count = Dressing::Replenish(*actor, assignment->pieces); count > 0) {
			logger::info("Replenished {} piece(s) on {:08X} taken in a menu", count, a_actor);
		}
		if (ReturnIfPiecesGone(*assignment, *actor)) {
			return;
		}
		if (const auto count = Dressing::Upgrade(*actor, assignment->pieces); count > 0) {
			logger::info("Upgraded {} piece(s) on {:08X} to a better copy", count, a_actor);
		}
		// After Upgrade, so a better copy it put on counts as a piece.
		if (const auto count = Dressing::TakeOffExtras(*actor, assignment->pieces, assignment->lockedCopies, assignment->overOwn); count > 0) {
			logger::info("Took off {} item(s) worn outside Livery's pieces on {:08X}", count, a_actor);
		}
		LockNewOutfitCopies(*assignment, *actor);
	}

	// Armor the engine put on them that Livery keeps off comes off at once: given while the
	// trade, container or pickpocket menu is open (a helmet for a slot the outfit leaves empty),
	// not as it closes, or put on by the worn-gear pass's best-armor step (their outfit items or
	// a hat after a dressing took them off). With none of the pieces left, their own clothes go
	// back on instead (ReturnIfPiecesGone), except while a trade or container menu lists them:
	// the pieces the player took there go on again as it closes (ReplenishTaken). `a_when` ends
	// the log line. Returns whether what they wear changed.
	bool TakeOffGiven(RE::FormID a_actor, std::string_view a_when)
	{
		if (a_actor == g_suspended) {
			return false;  // the preview has them off on purpose
		}
		auto* assignment = FindAssignment(a_actor);
		auto* actor = assignment ? RE::TESForm::LookupByID<RE::Actor>(a_actor) : nullptr;
		if (!actor || actor->IsDead()) {
			return false;
		}
		// Only a halted game needs the rebuild: it costs, and its NiNodeUpdate event makes
		// body-morph mods refit, for a change a running game's actor update shows anyway.
		const auto refreshIfHalted = [actor] {
			if (Dressing::GameHalted()) {
				Dressing::RefreshModel(*actor);
			}
		};
		if (g_shownInMenu.load() != a_actor && ReturnIfPiecesGone(*assignment, *actor)) {
			refreshIfHalted();
			return true;
		}
		const auto count = Dressing::TakeOffExtras(*actor, assignment->pieces, assignment->lockedCopies, assignment->overOwn);
		if (count == 0) {
			return false;  // nothing changed: no lock pass either, this runs after every worn-gear pass
		}
		logger::info("Took off {} item(s) worn outside Livery's pieces on {:08X} {}", count, a_actor, a_when);
		LockNewOutfitCopies(*assignment, *actor);
		refreshIfHalted();
		return true;
	}

	// The corpse behaves like vanilla: the locks go, the copies stay lootable, and their
	// own pieces stay in the inventory.
	void ReleaseDead(RE::FormID a_actor)
	{
		auto* actor = FindAssignment(a_actor) ? RE::TESForm::LookupByID<RE::Actor>(a_actor) : nullptr;
		if (!actor) {
			return;
		}
		ReleaseActor(*actor, false, false);
		logger::info("Released {:08X}, who died", a_actor);
	}

	// --- Co-save --------------------------------------------------------------
	//
	// Record ASGN v6: assignment count; per assignment the actor FormID, the library outfit
	// worn (wornOutfit), the piece count, per piece its instance key (armor FormID, the
	// ExtraUniqueID's baseID and uniqueID), added and locked; the count of their own copies
	// taken off and their instance keys; the count of their outfit copies Livery locked and
	// their instance keys; the Default's piece count and their armor FormIDs;
	// the situation of the row worn; the row count (one byte), per row its situation, random
	// (one byte), chosen, the outfit count and the outfit ids. A situation is stored by its key
	// (Situations::Name): its length (one byte) and its characters, so situations can be
	// added and removed without a new version. A record of another version is not read.
	// Contract: SKSE calls the three callbacks from the game's save, load and revert, on
	// the game thread.

	template <class T>
	bool Write(const SKSE::SerializationInterface& a_intfc, const T& a_value)
	{
		return a_intfc.WriteRecordData(a_value);
	}

	template <class T>
	bool Read(const SKSE::SerializationInterface& a_intfc, T& a_value)
	{
		return a_intfc.ReadRecordData(a_value) == sizeof(T);
	}

	bool WriteKey(const SKSE::SerializationInterface& a_intfc, const Dressing::InstanceKey& a_key)
	{
		return Write(a_intfc, a_key.armor) && Write(a_intfc, a_key.baseID) && Write(a_intfc, a_key.uniqueID);
	}

	// Reads one instance key; its FormIDs are fixed for the current load order. Returns
	// false when the record ends early; `resolved` is false when a form's plugin is gone.
	bool ReadKey(const SKSE::SerializationInterface& a_intfc, Dressing::InstanceKey& a_key, bool& a_resolved)
	{
		if (!Read(a_intfc, a_key.armor) || !Read(a_intfc, a_key.baseID) || !Read(a_intfc, a_key.uniqueID)) {
			return false;
		}
		a_resolved = a_intfc.ResolveFormID(a_key.armor, a_key.armor) && a_intfc.ResolveFormID(a_key.baseID, a_key.baseID);
		return true;
	}

	// A key count and the keys.
	bool WriteKeys(const SKSE::SerializationInterface& a_intfc, const std::vector<Dressing::InstanceKey>& a_keys)
	{
		bool written = Write(a_intfc, static_cast<std::uint32_t>(a_keys.size()));
		for (const auto& key : a_keys) {
			written = written && WriteKey(a_intfc, key);
		}
		return written;
	}

	// Reads what WriteKeys wrote. A key whose plugin is gone is dropped. Returns false when the
	// record ends early.
	bool ReadKeys(const SKSE::SerializationInterface& a_intfc, std::vector<Dressing::InstanceKey>& a_keys)
	{
		std::uint32_t count = 0;
		if (!Read(a_intfc, count)) {
			return false;
		}
		for (; count > 0; --count) {
			Dressing::InstanceKey key;
			bool                  resolved = false;
			if (!ReadKey(a_intfc, key, resolved)) {
				return false;
			}
			if (resolved) {
				a_keys.push_back(key);
			}
		}
		return true;
	}

	bool WriteSituationKey(const SKSE::SerializationInterface& a_intfc, std::string_view a_key)
	{
		return Write(a_intfc, static_cast<std::uint8_t>(a_key.size())) && a_intfc.WriteRecordData(a_key.data(), static_cast<std::uint32_t>(a_key.size()));
	}

	bool WriteSituation(const SKSE::SerializationInterface& a_intfc, Situation a_situation)
	{
		return WriteSituationKey(a_intfc, Situations::Name(a_situation));
	}

	bool WriteRow(const SKSE::SerializationInterface& a_intfc, std::string_view a_key, const OutfitRows::Row& a_row)
	{
		bool written = WriteSituationKey(a_intfc, a_key) && Write(a_intfc, static_cast<std::uint8_t>(a_row.random)) &&
		               Write(a_intfc, a_row.chosen) && Write(a_intfc, static_cast<std::uint32_t>(a_row.outfits.size()));
		for (const auto outfit : a_row.outfits) {
			written = written && Write(a_intfc, outfit);
		}
		return written;
	}

	bool WriteAssignment(const SKSE::SerializationInterface& a_intfc, const Assignment& a_assignment)
	{
		bool written = Write(a_intfc, a_assignment.actor) && Write(a_intfc, a_assignment.wornOutfit) &&
		               Write(a_intfc, static_cast<std::uint32_t>(a_assignment.pieces.size()));
		for (const auto& [key, added, locked] : a_assignment.pieces) {
			written = written && WriteKey(a_intfc, key) && Write(a_intfc, static_cast<std::uint8_t>(added)) && Write(a_intfc, static_cast<std::uint8_t>(locked));
		}
		written = written && WriteKeys(a_intfc, a_assignment.takenOff) && WriteKeys(a_intfc, a_assignment.lockedCopies);
		written = written && Write(a_intfc, static_cast<std::uint32_t>(a_assignment.defaultPieces.size()));
		for (const auto armor : a_assignment.defaultPieces) {
			written = written && Write(a_intfc, armor);
		}
		// Every situation's row, an empty one for a row never stored (OutfitRows::Rows), then the
		// kept rows.
		// Contract: rows are kept only while situations.json cannot be read, when Count() is the
		// presets alone; the record they were read from held the presets' rows too, so the
		// count still fits in a byte.
		const auto rows = Situations::Count();
		written = written && WriteSituation(a_intfc, a_assignment.worn) && Write(a_intfc, static_cast<std::uint8_t>(rows + a_assignment.kept.size()));
		for (std::size_t i = 0; i < rows; ++i) {
			written = written && WriteRow(a_intfc, Situations::Name(static_cast<Situation>(i)), a_assignment.rows[i]);
		}
		for (const auto& [key, row] : a_assignment.kept) {
			written = written && WriteRow(a_intfc, key, row);
		}
		return written;
	}

	// Calls `a_call(actor, key, row)` for every row stored, kept rows included, as the records
	// of a row's outfits (EXCL, CHNC, LAYR) name them: by the actor and the situation's key.
	template <class Call>
	void ForEachStoredRow(Call&& a_call)
	{
		for (const auto& assignment : g_assignments) {
			for (std::size_t i = 0; i < assignment.rows.size(); ++i) {
				a_call(assignment.actor, Situations::Name(static_cast<Situation>(i)), assignment.rows[i]);
			}
			for (const auto& [key, row] : assignment.kept) {
				a_call(assignment.actor, std::string_view{ key }, row);
			}
		}
	}

	// The entries of every stored row's `a_list` (Row::excluded, Row::chances).
	template <class Entry>
	std::uint32_t CountRowEntries(std::vector<Entry> OutfitRows::Row::* a_list)
	{
		std::uint32_t count = 0;
		ForEachStoredRow([&count, a_list](RE::FormID, std::string_view, const OutfitRows::Row& a_row) { count += static_cast<std::uint32_t>((a_row.*a_list).size()); });
		return count;
	}

	// Record EXCL v2, written after ASGN while a row's outfit leaves items off
	// (OutfitRows::Exclusion): entry count; per entry the actor FormID, the row's situation (by
	// its key, as in ASGN), the outfit id, the armor count and the armor FormIDs. A record
	// of its own, so a Livery that does not know EXCL skips it and dresses every item.

	void SaveExclusions(SKSE::SerializationInterface* a_intfc)
	{
		const auto count = CountRowEntries(&OutfitRows::Row::excluded);
		if (count == 0) {
			return;
		}
		if (!a_intfc->OpenRecord(kExclusionsRecord, kExclusionsVersion)) {
			logger::error("Could not open the co-save record of the items left off");
			return;
		}
		bool written = Write(*a_intfc, count);
		ForEachStoredRow([&written, a_intfc](RE::FormID a_actor, std::string_view a_key, const OutfitRows::Row& a_row) {
			for (const auto& [outfit, armor] : a_row.excluded) {
				written = written && Write(*a_intfc, a_actor) && WriteSituationKey(*a_intfc, a_key) && Write(*a_intfc, outfit) &&
				          Write(*a_intfc, static_cast<std::uint32_t>(armor.size()));
				for (const auto id : armor) {
					written = written && Write(*a_intfc, id);
				}
			}
		});
		if (!written) {
			logger::error("Could not write the co-save record of the items left off");
		}
	}

	// Record CHNC v1, written after ASGN while a row's outfit has a selection chance other than
	// the default (OutfitRows::Chance): entry count; per entry the actor FormID, the row's
	// situation (by its key, as in ASGN), the outfit id and the chance (one byte). A record of
	// its own, so a Livery that does not know CHNC skips it and every outfit has the default.

	void SaveChances(SKSE::SerializationInterface* a_intfc)
	{
		const auto count = CountRowEntries(&OutfitRows::Row::chances);
		if (count == 0) {
			return;
		}
		if (!a_intfc->OpenRecord(kChancesRecord, kChancesVersion)) {
			logger::error("Could not open the co-save record of the selection chances");
			return;
		}
		bool written = Write(*a_intfc, count);
		ForEachStoredRow([&written, a_intfc](RE::FormID a_actor, std::string_view a_key, const OutfitRows::Row& a_row) {
			for (const auto& [outfit, value] : a_row.chances) {
				written = written && Write(*a_intfc, a_actor) && WriteSituationKey(*a_intfc, a_key) && Write(*a_intfc, outfit) && Write(*a_intfc, value);
			}
		});
		if (!written) {
			logger::error("Could not write the co-save record of the selection chances");
		}
	}

	// Record LAYR v1, written after ASGN while an assignment has a layer row or wears a layer
	// over their own clothes (HasLayerState), or a row has a layer chosen (Row::chosenLayer):
	// the count of those assignments; per assignment the actor FormID, whether `layerRow` is
	// set (one byte), `overOwn` (one byte), and with `layerRow` its situation (by its key, as in
	// ASGN) and `layer`; then the count of rows with a layer chosen; per row the actor FormID,
	// the row's situation and `chosenLayer`. A record of its own, so a Livery that does not know
	// LAYR skips it: no layer is worn, and the layers wait on their rows' lists.

	bool HasLayerState(const Assignment& a_assignment)
	{
		return a_assignment.layerRow.has_value() || a_assignment.overOwn;
	}

	void SaveLayers(SKSE::SerializationInterface* a_intfc)
	{
		const auto    states = static_cast<std::uint32_t>(std::ranges::count_if(g_assignments, HasLayerState));
		std::uint32_t chosen = 0;
		ForEachStoredRow([&chosen](RE::FormID, std::string_view, const OutfitRows::Row& a_row) { chosen += a_row.chosenLayer != 0 ? 1u : 0u; });
		if (states == 0 && chosen == 0) {
			return;
		}
		if (!a_intfc->OpenRecord(kLayersRecord, kLayersVersion)) {
			logger::error("Could not open the co-save record of the layers");
			return;
		}
		bool written = Write(*a_intfc, states);
		for (const auto& assignment : g_assignments) {
			if (!HasLayerState(assignment)) {
				continue;
			}
			const auto& row = assignment.layerRow;
			written = written && Write(*a_intfc, assignment.actor) && Write(*a_intfc, static_cast<std::uint8_t>(row.has_value())) &&
			          Write(*a_intfc, static_cast<std::uint8_t>(assignment.overOwn));
			if (row) {
				written = written && WriteSituation(*a_intfc, *row) && Write(*a_intfc, assignment.layer);
			}
		}
		written = written && Write(*a_intfc, chosen);
		ForEachStoredRow([&written, a_intfc](RE::FormID a_actor, std::string_view a_key, const OutfitRows::Row& a_row) {
			if (a_row.chosenLayer != 0) {
				written = written && Write(*a_intfc, a_actor) && WriteSituationKey(*a_intfc, a_key) && Write(*a_intfc, a_row.chosenLayer);
			}
		});
		if (!written) {
			logger::error("Could not write the co-save record of the layers");
		}
	}

	void OnSave(SKSE::SerializationInterface* a_intfc)
	{
		if (g_assignments.empty()) {
			return;
		}
		if (!a_intfc->OpenRecord(kAssignmentsRecord, kAssignmentsVersion)) {
			logger::error("Could not open the co-save record");
			return;
		}
		bool written = Write(*a_intfc, static_cast<std::uint32_t>(g_assignments.size()));
		for (const auto& assignment : g_assignments) {
			written = written && WriteAssignment(*a_intfc, assignment);
		}
		if (!written) {
			logger::error("Could not write the co-save record");
		}
		SaveExclusions(a_intfc);
		SaveChances(a_intfc);
		SaveLayers(a_intfc);
	}

	bool ReadPiece(const SKSE::SerializationInterface& a_intfc, Dressing::Piece& a_piece, bool& a_resolved)
	{
		std::uint8_t added = 0;
		std::uint8_t locked = 0;
		if (!ReadKey(a_intfc, a_piece.key, a_resolved) || !Read(a_intfc, added) || !Read(a_intfc, locked)) {
			return false;
		}
		a_piece.added = added != 0;
		a_piece.locked = locked != 0;
		return true;
	}

	// Reads the pieces, the copies taken off and the outfit copies locked. Returns false when
	// the record ends early. A piece or copy whose plugin is gone is dropped.
	bool ReadWorn(const SKSE::SerializationInterface& a_intfc, Assignment& a_assignment)
	{
		std::uint32_t pieces = 0;
		if (!Read(a_intfc, pieces)) {
			return false;
		}
		for (; pieces > 0; --pieces) {
			Dressing::Piece piece;
			bool            resolved = false;
			if (!ReadPiece(a_intfc, piece, resolved)) {
				return false;
			}
			if (resolved) {
				a_assignment.pieces.push_back(piece);
			}
		}
		return ReadKeys(a_intfc, a_assignment.takenOff) && ReadKeys(a_intfc, a_assignment.lockedCopies);
	}

	// Reads an armor count and the FormIDs. A form whose plugin is gone is dropped. Returns
	// false when the record ends early.
	bool ReadArmorForms(const SKSE::SerializationInterface& a_intfc, std::vector<RE::FormID>& a_forms)
	{
		std::uint32_t armors = 0;
		if (!Read(a_intfc, armors)) {
			return false;
		}
		for (; armors > 0; --armors) {
			RE::FormID armor = 0;
			if (!Read(a_intfc, armor)) {
				return false;
			}
			if (a_intfc.ResolveFormID(armor, armor)) {
				a_forms.push_back(armor);
			}
		}
		return true;
	}

	// Reads a situation's key as written (WriteSituation). Returns false when the record ends
	// early.
	bool ReadSituationKey(const SKSE::SerializationInterface& a_intfc, std::string& a_key)
	{
		std::uint8_t length = 0;
		if (!Read(a_intfc, length)) {
			return false;
		}
		a_key.resize(length);
		return length == 0 || a_intfc.ReadRecordData(a_key.data(), length) == length;
	}

	// Reads the situation of the row worn. One this version does not have (Situations::FromKey)
	// reads as the Default, with a log line. Returns false when the record ends early.
	bool ReadWornRow(const SKSE::SerializationInterface& a_intfc, Assignment& a_assignment)
	{
		std::string key;
		if (!ReadSituationKey(a_intfc, key)) {
			return false;
		}
		const auto situation = Situations::FromKey(key);
		if (!situation) {
			logger::warn("{:08X} wore the co-save row of situation {}, which this version does not have; read as the Default", a_assignment.actor, key);
		}
		a_assignment.worn = situation.value_or(Situation::kDefault);
		return true;
	}

	// One row of the record, after its situation.
	bool ReadRow(const SKSE::SerializationInterface& a_intfc, OutfitRows::Row& a_row)
	{
		std::uint8_t  random = 0;
		std::uint32_t outfits = 0;
		if (!Read(a_intfc, random) || !Read(a_intfc, a_row.chosen) || !Read(a_intfc, outfits)) {
			return false;
		}
		a_row.random = random != 0;
		for (; outfits > 0; --outfits) {
			std::uint32_t outfit = 0;
			if (!Read(a_intfc, outfit)) {
				return false;
			}
			a_row.outfits.push_back(outfit);
		}
		return true;
	}

	// Reads the Default's pieces, the row worn and the rows. A row for a situation this session
	// does not have is kept for a user's situation while situations.json cannot be read
	// (SituationTable::KeepsUnknownRow), dropped otherwise, with a log line either way. Returns
	// false when the record ends early.
	// Contract: SituationStore is loaded (kDataLoaded, before any co-save load).
	bool ReadRows(const SKSE::SerializationInterface& a_intfc, Assignment& a_assignment)
	{
		std::uint8_t rows = 0;
		if (!ReadArmorForms(a_intfc, a_assignment.defaultPieces) || !ReadWornRow(a_intfc, a_assignment) || !Read(a_intfc, rows)) {
			return false;
		}
		for (; rows > 0; --rows) {
			std::string     key;
			OutfitRows::Row row;
			if (!ReadSituationKey(a_intfc, key) || !ReadRow(a_intfc, row)) {
				return false;
			}
			const auto situation = Situations::FromKey(key);
			if (!situation) {
				// One with outfits on its list may hold a layer LAYR names (OnLoad drops it if not).
				if (!HoldsSomething(row) && row.outfits.empty()) {
					continue;
				}
				if (SituationTable::KeepsUnknownRow(key, SituationStore::WasLoaded())) {
					logger::warn("Kept the co-save row of situation {} for {:08X}: situations.json could not be read", key, a_assignment.actor);
					a_assignment.kept.push_back({ .key = std::move(key), .row = std::move(row) });
				} else {
					logger::warn("Dropped the co-save row of situation {}, which this version does not have, for {:08X}", key, a_assignment.actor);
				}
				continue;
			}
			a_assignment.rows[RowOf(*situation)] = std::move(row);
		}
		return true;
	}

	// Reads one assignment. Returns false when the record ends early.
	bool ReadAssignment(const SKSE::SerializationInterface& a_intfc, Assignment& a_assignment, bool& a_actorResolved)
	{
		if (!Read(a_intfc, a_assignment.actor) || !Read(a_intfc, a_assignment.wornOutfit)) {
			return false;
		}
		a_actorResolved = a_intfc.ResolveFormID(a_assignment.actor, a_assignment.actor);
		return ReadWorn(a_intfc, a_assignment) && ReadRows(a_intfc, a_assignment);
	}

	// The rows of situations removed in situations.json since this save was made, each with
	// a log line. Pieces worn for one stay on until the poll's next dressing
	// (ActorsWithSituations), as after ForgetSituation.
	// Contract: SituationStore is loaded (kDataLoaded, before any co-save load).
	void DropRemovedRows(Assignment& a_assignment)
	{
		for (const auto situation : Situations::Order()) {
			auto& row = a_assignment.rows[RowOf(situation)];
			if (SituationStore::IsRemoved(situation) && row != OutfitRows::Row{}) {
				logger::info("Dropped the {} row of {:08X}: the situation is removed in situations.json", Situations::Name(situation), a_assignment.actor);
				row = {};
			}
		}
	}

	// An assignment whose actor's plugin is gone is dropped; one with nothing on the actor and
	// no situation row holding an outfit is, once the other records are attached (OnLoad).
	std::vector<Assignment> ReadAssignments(const SKSE::SerializationInterface& a_intfc)
	{
		std::vector<Assignment> assignments;
		std::uint32_t           count = 0;
		if (!Read(a_intfc, count)) {
			logger::error("The co-save record is cut short");
			return assignments;
		}
		for (; count > 0; --count) {
			Assignment assignment;
			bool       actorResolved = false;
			if (!ReadAssignment(a_intfc, assignment, actorResolved)) {
				logger::error("The co-save record is cut short");
				return assignments;
			}
			DropRemovedRows(assignment);
			if (actorResolved) {
				assignments.push_back(std::move(assignment));
			}
		}
		return assignments;
	}

	// An EXCL entry as read, its FormIDs fixed for the current load order.
	struct StoredExclusion
	{
		RE::FormID               actor = 0;
		std::optional<Situation> row;  // nullopt for a situation this session does not have
		std::string              key;  // the row's situation as stored, for a kept row (Assignment::kept)
		std::uint32_t            outfit = 0;
		std::vector<RE::FormID>  armor;
	};

	// Reads the EXCL record. An entry whose actor's plugin is gone is dropped, and so is an
	// armor form whose plugin is gone (ReadArmorForms).
	std::vector<StoredExclusion> ReadExclusions(const SKSE::SerializationInterface& a_intfc)
	{
		std::vector<StoredExclusion> entries;
		std::uint32_t                count = 0;
		if (!Read(a_intfc, count)) {
			logger::error("The co-save record of the items left off is cut short");
			return entries;
		}
		for (; count > 0; --count) {
			StoredExclusion entry;
			if (!Read(a_intfc, entry.actor) || !ReadSituationKey(a_intfc, entry.key) || !Read(a_intfc, entry.outfit) || !ReadArmorForms(a_intfc, entry.armor)) {
				logger::error("The co-save record of the items left off is cut short");
				return entries;
			}
			entry.row = Situations::FromKey(entry.key);
			if (a_intfc.ResolveFormID(entry.actor, entry.actor)) {
				entries.push_back(std::move(entry));
			}
		}
		return entries;
	}

	// A CHNC entry as read, its actor's FormID fixed for the current load order.
	struct StoredChance
	{
		RE::FormID               actor = 0;
		std::optional<Situation> row;  // nullopt for a situation this session does not have
		std::string              key;  // the row's situation as stored, for a kept row (Assignment::kept)
		std::uint32_t            outfit = 0;
		std::uint8_t             value = OutfitRows::kDefaultChance;
	};

	// Reads the CHNC record. An entry whose actor's plugin is gone is dropped.
	std::vector<StoredChance> ReadChances(const SKSE::SerializationInterface& a_intfc)
	{
		std::vector<StoredChance> entries;
		std::uint32_t             count = 0;
		if (!Read(a_intfc, count)) {
			logger::error("The co-save record of the selection chances is cut short");
			return entries;
		}
		for (; count > 0; --count) {
			StoredChance entry;
			if (!Read(a_intfc, entry.actor) || !ReadSituationKey(a_intfc, entry.key) || !Read(a_intfc, entry.outfit) || !Read(a_intfc, entry.value)) {
				logger::error("The co-save record of the selection chances is cut short");
				return entries;
			}
			entry.row = Situations::FromKey(entry.key);
			if (a_intfc.ResolveFormID(entry.actor, entry.actor)) {
				entries.push_back(std::move(entry));
			}
		}
		return entries;
	}

	// A LAYR assignment entry as read, its actor's FormID fixed for the current load order.
	struct StoredLayerState
	{
		RE::FormID    actor = 0;
		bool          hasRow = false;
		bool          overOwn = false;
		std::string   key;  // the layer row's situation as stored, with `hasRow`
		std::uint32_t layer = 0;
	};

	// A LAYR row entry as read, as an EXCL entry is.
	struct StoredChosenLayer
	{
		RE::FormID               actor = 0;
		std::optional<Situation> row;  // nullopt for a situation this session does not have
		std::string              key;  // the row's situation as stored, for a kept row (Assignment::kept)
		std::uint32_t            outfit = 0;
	};

	struct StoredLayers
	{
		std::vector<StoredLayerState>  states;
		std::vector<StoredChosenLayer> chosen;
	};

	bool ReadLayerState(const SKSE::SerializationInterface& a_intfc, StoredLayerState& a_entry)
	{
		std::uint8_t hasRow = 0;
		std::uint8_t overOwn = 0;
		if (!Read(a_intfc, a_entry.actor) || !Read(a_intfc, hasRow) || !Read(a_intfc, overOwn)) {
			return false;
		}
		a_entry.hasRow = hasRow != 0;
		a_entry.overOwn = overOwn != 0;
		return !a_entry.hasRow || (ReadSituationKey(a_intfc, a_entry.key) && Read(a_intfc, a_entry.layer));
	}

	// Reads the LAYR record. An entry whose actor's plugin is gone is dropped.
	StoredLayers ReadLayers(const SKSE::SerializationInterface& a_intfc)
	{
		StoredLayers  layers;
		std::uint32_t count = 0;
		if (!Read(a_intfc, count)) {
			logger::error("The co-save record of the layers is cut short");
			return layers;
		}
		for (; count > 0; --count) {
			StoredLayerState entry;
			if (!ReadLayerState(a_intfc, entry)) {
				logger::error("The co-save record of the layers is cut short");
				return layers;
			}
			if (a_intfc.ResolveFormID(entry.actor, entry.actor)) {
				layers.states.push_back(std::move(entry));
			}
		}
		if (!Read(a_intfc, count)) {
			logger::error("The co-save record of the layers is cut short");
			return layers;
		}
		for (; count > 0; --count) {
			StoredChosenLayer entry;
			if (!Read(a_intfc, entry.actor) || !ReadSituationKey(a_intfc, entry.key) || !Read(a_intfc, entry.outfit)) {
				logger::error("The co-save record of the layers is cut short");
				return layers;
			}
			entry.row = Situations::FromKey(entry.key);
			if (a_intfc.ResolveFormID(entry.actor, entry.actor)) {
				layers.chosen.push_back(std::move(entry));
			}
		}
		return layers;
	}

	// The row an EXCL, CHNC or LAYR entry belongs to: a situation's, or a kept row stored under
	// its key; nullptr for neither.
	template <class Entry>
	OutfitRows::Row* EntryRow(Assignment& a_assignment, const Entry& a_entry)
	{
		if (a_entry.row) {
			return &a_assignment.rows[RowOf(*a_entry.row)];
		}
		const auto kept = std::ranges::find(a_assignment.kept, a_entry.key, &KeptRow::key);
		return kept != a_assignment.kept.end() ? &kept->row : nullptr;
	}

	// Gives the assignments read the items their rows' outfits leave off, kept rows included.
	// An entry for an actor without an assignment, a row this session neither has nor kept, or
	// an outfit no longer on the row (its situation removed in situations.json,
	// DropRemovedRows) is dropped.
	// Contract: after the ASGN record was read, in whichever order the records came.
	void AttachExclusions(const std::vector<StoredExclusion>& a_entries)
	{
		for (const auto& entry : a_entries) {
			auto* assignment = FindAssignment(entry.actor);
			auto* row = assignment ? EntryRow(*assignment, entry) : nullptr;
			if (!row) {
				continue;
			}
			if (std::ranges::find(row->outfits, entry.outfit) != row->outfits.end()) {
				OutfitRows::SetExcluded(*row, entry.outfit, entry.armor);
			}
		}
	}

	// Gives the assignments read their rows' outfits' selection chances, kept rows included,
	// dropping the entries AttachExclusions would.
	// Contract: after the ASGN record was read, in whichever order the records came.
	void AttachChances(const std::vector<StoredChance>& a_entries)
	{
		for (const auto& entry : a_entries) {
			auto* assignment = FindAssignment(entry.actor);
			auto* row = assignment ? EntryRow(*assignment, entry) : nullptr;
			if (row && std::ranges::find(row->outfits, entry.outfit) != row->outfits.end()) {
				OutfitRows::SetChance(*row, entry.outfit, entry.value);
			}
		}
	}

	// Gives the assignments read the rows their layer is from and the layers their rows have
	// chosen, kept rows included. An entry for an actor without an assignment is dropped, and so
	// is a layer row this session does not have (the next dressing draws anew) and a row entry
	// AttachExclusions would drop.
	// Contract: after the ASGN record was read, in whichever order the records came.
	void AttachLayers(const StoredLayers& a_layers)
	{
		for (const auto& entry : a_layers.states) {
			auto* assignment = FindAssignment(entry.actor);
			if (!assignment) {
				continue;
			}
			assignment->overOwn = entry.overOwn && !assignment->pieces.empty();
			if (const auto row = entry.hasRow ? Situations::FromKey(entry.key) : std::optional<Situation>{}) {
				assignment->layerRow = row;
				assignment->layer = entry.layer;
			}
		}
		for (const auto& entry : a_layers.chosen) {
			auto* assignment = FindAssignment(entry.actor);
			auto* row = assignment ? EntryRow(*assignment, entry) : nullptr;
			if (row && std::ranges::find(row->outfits, entry.outfit) != row->outfits.end()) {
				row->chosenLayer = entry.outfit;
			}
		}
	}

	void OnLoad(SKSE::SerializationInterface* a_intfc)
	{
		std::uint32_t                type = 0;
		std::uint32_t                version = 0;
		std::uint32_t                length = 0;
		std::vector<StoredExclusion> exclusions;
		std::vector<StoredChance>    chances;
		StoredLayers                 layers;
		while (a_intfc->GetNextRecordInfo(type, version, length)) {
			if (type == kLayersRecord) {
				if (version != kLayersVersion) {
					logger::warn("Co-save record LAYR version {} is not {}; no layer is worn until the next dressing", version, kLayersVersion);
					continue;
				}
				layers = ReadLayers(*a_intfc);
				continue;
			}
			if (type == kChancesRecord) {
				if (version != kChancesVersion) {
					logger::warn("Co-save record CHNC version {} is not {}; every outfit has the default selection chance", version, kChancesVersion);
					continue;
				}
				chances = ReadChances(*a_intfc);
				continue;
			}
			if (type == kExclusionsRecord) {
				if (version != kExclusionsVersion) {
					logger::warn("Co-save record EXCL version {} is not {}; every outfit wears all its items", version, kExclusionsVersion);
					continue;
				}
				exclusions = ReadExclusions(*a_intfc);
				continue;
			}
			if (type != kAssignmentsRecord) {
				continue;
			}
			if (version != kAssignmentsVersion) {
				logger::warn("Co-save record version {} is not {}; no assignments loaded", version, kAssignmentsVersion);
				continue;
			}
			g_assignments = ReadAssignments(*a_intfc);
			logger::info("Read co-save record version {}", version);
		}
		AttachExclusions(exclusions);
		AttachChances(chances);
		AttachLayers(layers);
		for (auto& assignment : g_assignments) {
			std::erase_if(assignment.kept, [](const KeptRow& a_kept) { return !HoldsSomething(a_kept.row); });
		}
		std::erase_if(g_assignments, IsEmpty);
		SyncManaged();
		logger::info("Loaded {} assignment(s) from the co-save", g_assignments.size());
	}

	void OnRevert(SKSE::SerializationInterface*)
	{
		g_assignments.clear();
		g_suspended = 0;
		SyncManaged();
	}

	// --- Event sinks ----------------------------------------------------------

	class LoadedSink final : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
		{
			if (a_event && a_event->loaded && IsManaged(a_event->formID)) {
				const auto id = a_event->formID;
				SKSE::GetTaskInterface()->AddTask([id] { PutPiecesBackOn(id); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		static LoadedSink& Get()
		{
			static LoadedSink sink;
			return sink;
		}
	};

	class DeathSink final : public RE::BSTEventSink<RE::TESDeathEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
		{
			const auto* dying = a_event ? a_event->actorDying.get() : nullptr;
			if (dying && IsManaged(dying->GetFormID())) {
				const auto id = dying->GetFormID();
				SKSE::GetTaskInterface()->AddTask([id] { ReleaseDead(id); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		static DeathSink& Get()
		{
			static DeathSink sink;
			return sink;
		}
	};

	// Registered only while the trade, container or pickpocket menu is open (MenuSink), so
	// normal play sends it nothing. Equips by anyone but an assigned NPC return after the check.
	class EquipSink final : public RE::BSTEventSink<RE::TESEquipEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
		{
			if (!a_event || !a_event->equipped || !a_event->actor || !IsManaged(a_event->actor->GetFormID())) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const auto* item = RE::TESForm::LookupByID(a_event->baseObject);
			if (item && item->IsArmor()) {
				const auto id = a_event->actor->GetFormID();
				SKSE::GetTaskInterface()->AddTask([id] { TakeOffGiven(id, "in a menu"); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		static EquipSink& Get()
		{
			static EquipSink sink;
			return sink;
		}
	};

	// Every other menu returns after the name compares. The note is not cleared as a menu
	// opens: whether its list fills before or after that event is not documented. Only these
	// two menus list an NPC's inventory, so the note is theirs.
	class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			// Diagnostics: what opens and closes while the window is open tells what took the
			// Cursor Menu away (CaptureWindow's "Cursor at close" line).
			if (a_event && CaptureWindow::IsOpen()) {
				logger::info("While the window is open: {} {}", a_event->menuName.c_str(), a_event->opening ? "opened" : "closed");
			}
			if (!a_event || (a_event->menuName != RE::ContainerMenu::MENU_NAME && a_event->menuName != RE::BarterMenu::MENU_NAME)) {
				return RE::BSEventNotifyControl::kContinue;
			}
			// Contract: this event's thread is not documented. BSTEventSource's Add/RemoveEventSink
			// take its lock, defer while it notifies, and never add a sink twice
			// (CommonLib's BSTEvent.h), so they are safe from here.
			if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
				if (a_event->opening) {
					events->AddEventSink<RE::TESEquipEvent>(&EquipSink::Get());
				} else {
					events->RemoveEventSink<RE::TESEquipEvent>(&EquipSink::Get());
				}
			}
			if (a_event->opening) {
				return RE::BSEventNotifyControl::kContinue;
			}
			if (const auto id = g_shownInMenu.exchange(0); id != 0) {
				SKSE::GetTaskInterface()->AddTask([id] { ReplenishTaken(id); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		static MenuSink& Get()
		{
			static MenuSink sink;
			return sink;
		}
	};
}

namespace Assignments
{
	void RegisterCoSave()
	{
		const auto* serialization = SKSE::GetSerializationInterface();
		if (!serialization) {
			logger::error("SKSE serialization interface missing: assignments are not saved");
			return;
		}
		serialization->SetUniqueID(kCoSaveID);
		serialization->SetSaveCallback(OnSave);
		serialization->SetLoadCallback(OnLoad);
		serialization->SetRevertCallback(OnRevert);
	}

	void RegisterSinks()
	{
		auto* events = RE::ScriptEventSourceHolder::GetSingleton();
		if (!events) {
			logger::error("Script event source missing: assigned outfits are not kept on");
			return;
		}
		events->AddEventSink<RE::TESObjectLoadedEvent>(&LoadedSink::Get());
		events->AddEventSink<RE::TESDeathEvent>(&DeathSink::Get());
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(&MenuSink::Get());
		} else {
			logger::error("UI missing: items taken back in the trade menu are not replaced");
		}
	}

	// An actor not in memory (a non-persistent NPC whose cell is not loaded) cannot be
	// reached: their locks and copies stay, which never breaks a save.
	void ReleaseEveryone()
	{
		const auto  total = g_assignments.size();
		std::size_t released = 0;
		for (const auto& assignment : std::exchange(g_assignments, {})) {
			if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(assignment.actor)) {
				Dressing::Release(*actor, assignment.pieces, true);
				Dressing::UnlockOutfitCopies(*actor, assignment.lockedCopies);  // before PutBack, which skips a locked copy
				// As Unassign: the player keeps what they wear.
				if (assignment.actor != kPlayerRef) {
					Dressing::PutBack(*actor, assignment.takenOff);
				}
				++released;
			}
		}
		SyncManaged();
		Notify(Strings::Format(released == total ? Strings::Text::kHudReleased : Strings::Text::kHudReleasedSome, released, total));
	}

	void Assign(RE::Actor& a_actor, const std::vector<RE::TESObjectARMO*>& a_armor, std::uint32_t a_outfit)
	{
		if (!CanBeDressed(a_actor) || a_armor.empty()) {
			return;
		}

		const auto     id = a_actor.GetFormID();
		auto&          assignment = FindOrAddAssignment(id);
		RedressOptions options;
		if (a_outfit != 0) {
			GiveRow(assignment, Situation::kDefault, a_outfit);
			options = GivenTo(a_outfit, { Situation::kDefault });
			options.leaveOut = PiecesNotIn(a_outfit, a_armor);
		} else {
			assignment.defaultPieces = FormIDsOf(a_armor);
			auto& row = assignment.rows[RowOf(Situation::kDefault)];
			OutfitRows::SortChosen(row, IsLayerOutfit);  // a layer chosen as a base before its flag changed stays
			row.chosen = 0;
			row.random = false;
		}
		// The new Default goes on even when they already wore the Default, as their base or their
		// layer; a situation outfit they wear stays on and the Default waits for its turn.
		const auto now = Situations::Evaluate(a_actor, assignment.rows);
		const auto rows = LiveRows(assignment);
		options.force = OutfitRows::Resolve(rows, now, IsLayerOutfit) == Situation::kDefault ||
		                OutfitRows::ResolveLayer(rows, now, IsLayerOutfit, WearableLayer) == Situation::kDefault;
		const auto result = Redress(assignment, a_actor, now, options);
		const auto wornFor = assignment.worn;  // read before DropIfEmpty, which may end the record
		DropIfEmpty(id);
		SyncManaged();
		using Strings::Text;
		const auto name = NameOf(a_actor);
		const bool situational = wornFor != Situation::kDefault;
		const auto situation = SituationStore::Name(wornFor);
		if (!result) {
			Notify(Strings::Format(situational ? Text::kHudWillWearDefaultFor : Text::kHudWillWearDefault, name, situation));
			return;
		}

		const auto added = std::ranges::count_if(result->result.worn, &Dressing::Piece::added);
		const auto wears = a_actor.IsPlayerRef() ? (situational ? Text::kHudYouWearTheseFor : Text::kHudYouWearThese) :
		                                           (situational ? Text::kHudWearsTheseFor : Text::kHudWearsThese);
		auto       text = Strings::Format(wears, name, situation);
		if (added > 0) {
			text = Strings::Format(Text::kHudNoteAdded, text, added);
		}
		Notify(EndOf(WithRedressNotes(std::move(text), a_actor, *result)));
	}

	void Change(RE::Actor& a_actor, const Changes& a_changes)
	{
		if (!CanBeDressed(a_actor)) {
			return;
		}
		const auto id = a_actor.GetFormID();
		auto&      assignment = FindOrAddAssignment(id);
		ApplyChanges(assignment, a_changes);
		// A removed row that still holds an outfit is a random one with others on its list.
		std::vector<Situation> emptied;
		std::vector<Situation> kept;
		for (const auto row : a_changes.remove) {
			(OutfitRows::HoldsAny(assignment.rows[RowOf(row)]) ? kept : emptied).push_back(row);
		}
		auto options = GivenTo(a_changes.outfit, a_changes.assign);
		options.force = true;
		options.leaveOut = a_changes.leaveOut;
		const auto result = Redress(assignment, a_actor, Situations::Evaluate(a_actor, assignment.rows), options);
		const auto worn = assignment.worn;  // read before DropIfEmpty, which may end the record
		const auto pieces = assignment.pieces.size();
		DropIfEmpty(id);
		SyncManaged();

		// Contract: every caller assigns the outfit to a row, so there is always a part to name.
		using Strings::Text;
		std::string parts;
		const auto  add = [&parts](const std::string& a_part) {
			parts = parts.empty() ? a_part : Strings::Format(Text::kHudNoteJoin, parts, a_part);
		};
		if (!a_changes.assign.empty()) {
			add(Strings::Format(Text::kHudPartFor, QuotedOutfitName(a_changes.outfit), JoinSituations(a_changes.assign)));
		}
		if (!emptied.empty()) {
			add(Strings::Format(Text::kHudPartNoOutfit, JoinSituations(emptied)));
		}
		if (!kept.empty()) {
			add(Strings::Format(Text::kHudPartNoLonger, QuotedOutfitName(a_changes.outfit), JoinSituations(kept)));
		}
		auto text = Strings::Format(Text::kHudChangeHead, NameOf(a_actor), parts);
		if (pieces == 0) {
			text = Strings::Format(Text::kHudOwnClothesNow, text);
		} else if (worn == Situation::kDefault) {
			text = Strings::Format(Text::kHudDressedNow, text);
		} else {
			text = Strings::Format(Text::kHudDressedNowFor, text, SituationStore::Name(worn));
		}
		Notify(EndOf(result ? WithRedressNotes(std::move(text), a_actor, *result) : text));
	}

	std::optional<Picks> SetRows(RE::Actor& a_actor, const OutfitRows::Rows& a_rows, bool a_dropDefaultPieces)
	{
		if (!CanBeDressed(a_actor)) {
			return std::nullopt;
		}
		const auto id = a_actor.GetFormID();
		auto&      assignment = FindOrAddAssignment(id);
		const auto picks = GainedPicks(assignment, a_rows);
		assignment.rows = a_rows;
		for (const auto situation : Situations::Order()) {
			if (SituationStore::IsRemoved(situation)) {
				assignment.rows[RowOf(situation)] = {};
			}
		}
		if (a_dropDefaultPieces) {
			assignment.defaultPieces.clear();
		}
		// Contract: no DropIfEmpty here. The record of someone Livery has nothing on yet would
		// end before the Refresh that dresses them; that dressing ends it if nothing goes on.
		SyncManaged();
		return picks;
	}

	bool SetLayerExcluded(RE::FormID a_actor, const std::vector<RE::FormID>& a_excluded)
	{
		auto* assignment = FindAssignment(a_actor);
		if (!assignment || assignment->layer == 0 || !assignment->layerRow || assignment->pieces.empty()) {
			return false;
		}
		auto& row = assignment->rows[RowOf(*assignment->layerRow)];
		if (std::ranges::find(row.outfits, assignment->layer) == row.outfits.end()) {
			return false;  // SetExcluded's contract: the outfit is on the row
		}
		const auto before = row.excluded;
		OutfitRows::SetExcluded(row, assignment->layer, a_excluded);
		return row.excluded != before;
	}

	bool TakeOffLayer(RE::FormID a_actor)
	{
		auto* assignment = FindAssignment(a_actor);
		if (!assignment || assignment->layer == 0 || !assignment->layerRow || assignment->pieces.empty()) {
			return false;
		}
		auto& row = assignment->rows[RowOf(*assignment->layerRow)];
		if (row.random) {
			assignment->layer = 0;  // the next dressing keeps it (LayerFor), as a draw of none
		} else {
			OutfitRows::Take(row, assignment->layer);
		}
		return true;
	}

	void Refresh(RE::Actor& a_actor, const std::vector<RE::TESObjectARMO*>& a_leaveOut, const Picks& a_picks)
	{
		const auto id = a_actor.GetFormID();
		auto*      assignment = FindAssignment(id);
		if (!assignment) {
			return;
		}
		if (a_actor.IsDead()) {
			Notify(Strings::Format(Strings::Text::kHudDead, NameOf(a_actor)));
			return;
		}
		RedressOptions options{ .force = true, .leaveOut = a_leaveOut, .givenBase = a_picks.base, .givenLayer = a_picks.layer, .givenRows = { assignment->worn } };
		if (assignment->layerRow) {
			options.givenRows.push_back(*assignment->layerRow);
		}
		const auto result = Redress(*assignment, a_actor, Situations::Evaluate(a_actor, assignment->rows), options);
		const auto worn = assignment->worn;  // read before DropIfEmpty, which may end the record
		const auto outfit = assignment->wornOutfit;
		const auto pieces = assignment->pieces.size();
		const auto layered = LayeredText(a_actor, *assignment);
		DropIfEmpty(id);
		SyncManaged();
		using Strings::Text;
		const auto  name = NameOf(a_actor);
		std::string text;
		if (pieces == 0) {
			text = Strings::Format(Text::kHudOwnClothes, name);
		} else {
			const auto wears = layered ? *layered : (outfit ? QuotedOutfitName(outfit) : std::string{ Strings::Get(Text::kHudDefaultItems) });
			text = Strings::Format(worn == Situation::kDefault ? Text::kHudWearsOutfit : Text::kHudWearsOutfitFor, name, wears, SituationStore::Name(worn));
		}
		Notify(EndOf(result ? WithRedressNotes(std::move(text), a_actor, *result) : text));
	}

	Picks PickAnother(RE::FormID a_actor, Situations::Situation a_row)
	{
		Picks       picks;
		const auto* assignment = FindAssignment(a_actor);
		if (!assignment || assignment->pieces.empty()) {
			return picks;
		}
		const auto  rows = LiveRows(*assignment);
		const auto& row = rows[RowOf(a_row)];
		if (!row.random) {
			return picks;
		}
		if (assignment->worn == a_row) {
			picks.base = OutfitRows::Pick(row, assignment->wornOutfit, RandomBelow, [](std::uint32_t a_outfit) { return !IsLayerOutfit(a_outfit); });
		}
		if (assignment->layerRow == a_row) {
			picks.layer = OutfitRows::DrawLayer(row, RandomBelow, [](std::uint32_t a_outfit) { return IsLayerOutfit(a_outfit) && WearableLayer(a_outfit); });
		}
		return picks;
	}

	bool WearFor(RE::Actor& a_actor, const Situations::Applicable& a_now)
	{
		const auto id = a_actor.GetFormID();
		auto*      assignment = FindAssignment(id);
		if (!assignment) {
			return false;
		}
		const auto redressed = Redress(*assignment, a_actor, a_now);
		if (!redressed) {
			return false;
		}
		logger::info("Dressed {:08X} for {}: {} piece(s) on", id, Situations::Name(assignment->worn), assignment->pieces.size());
		// Read before DropIfEmpty: a situation outfit gone from their rows (ForgetOutfit) may
		// leave nothing, which ends the record. After a fallback, why it gave way alone.
		const auto text = redressed->fallback ? EndOf(Strings::Format(Strings::Text::kHudFallbackAlone, FallbackText(a_actor, *redressed->fallback))) :
		                                        SituationChangeText(a_actor, *assignment);
		DropIfEmpty(id);
		SyncManaged();
		if (Settings::NotifySituationChanges()) {
			Notify(text);
		}
		return true;
	}

	// Redress's test without the dressing: on the rows they wear, OutfitFor and LayerFor keep
	// what is worn while the rows still have it, so no random pick is drawn for nothing.
	bool WouldRedress(RE::Actor& a_actor, const Situations::Applicable& a_now)
	{
		const auto* assignment = FindAssignment(a_actor.GetFormID());
		if (!assignment) {
			return false;
		}
		return !IsWorn(*assignment, DueFor(*assignment, LiveRows(*assignment), a_now, {}));
	}

	Situations::Applicable SituationsNow(RE::Actor& a_actor)
	{
		const auto* assignment = FindAssignment(a_actor.GetFormID());
		return assignment ? Situations::Evaluate(a_actor, assignment->rows) : Situations::Applicable{};
	}

	std::vector<RE::FormID> ActorsWithSituations()
	{
		std::vector<RE::FormID> actors;
		for (const auto& assignment : g_assignments) {
			const bool situationLayer = assignment.layerRow && *assignment.layerRow != Situation::kDefault;
			const bool layerGone = assignment.layer != 0 && !InLibrary(assignment.layer);  // a Default's layer ForgetOutfit took
			if (HasSituations(assignment) || assignment.worn != Situation::kDefault || situationLayer || layerGone) {
				actors.push_back(assignment.actor);
			}
		}
		return actors;
	}

	std::vector<Use> UsesOf(std::uint32_t a_outfit)
	{
		std::vector<Use> uses;
		if (a_outfit == 0) {
			return uses;
		}
		for (const auto& assignment : g_assignments) {
			Use        use{ .actor = assignment.actor };
			const auto add = [&](Situation a_situation) {
				if (OutfitRows::Holds(assignment.rows[RowOf(a_situation)], a_outfit)) {
					use.rows.push_back(a_situation);
				}
			};
			add(Situation::kDefault);
			for (const auto situation : Situations::Order()) {
				add(situation);
			}
			if (!use.rows.empty()) {
				uses.push_back(std::move(use));
			}
		}
		return uses;
	}

	std::vector<Holder> HoldersOf(Situations::Situation a_situation)
	{
		std::vector<Holder> holders;
		for (const auto& assignment : g_assignments) {
			const auto& outfits = assignment.rows[RowOf(a_situation)].outfits;
			if (const auto count = static_cast<std::size_t>(std::ranges::count_if(outfits, InLibrary)); count > 0) {
				holders.push_back({ .actor = assignment.actor, .outfits = count });
			}
		}
		return holders;
	}

	// The pieces worn for a cleared situation row, or as a layer, stay on until the next
	// dressing (WearFor), which ActorsWithSituations keeps the poll watching for. A Default's
	// base worn from the outfit is no longer worn from a library outfit (wornOutfit 0).
	std::vector<RE::FormID> ForgetOutfit(std::uint32_t a_outfit, const std::vector<RE::TESObjectARMO*>& a_pieces, bool a_layer)
	{
		std::vector<RE::FormID> wearing;
		if (a_outfit == 0) {
			return wearing;
		}
		for (auto& assignment : g_assignments) {
			const bool situationBase = assignment.worn != Situation::kDefault && assignment.wornOutfit == a_outfit;
			if ((situationBase || assignment.layer == a_outfit) && !assignment.pieces.empty()) {
				wearing.push_back(assignment.actor);
			}
			const auto& defaultRow = assignment.rows[RowOf(Situation::kDefault)];
			const bool  heldByDefault = OutfitRows::Holds(defaultRow, a_outfit);
			for (auto& row : assignment.rows) {
				OutfitRows::Take(row, a_outfit);
			}
			for (auto& kept : assignment.kept) {
				OutfitRows::Take(kept.row, a_outfit);
			}
			std::erase_if(assignment.kept, [](const KeptRow& a_kept) { return !HoldsSomething(a_kept.row); });
			if (!a_layer && heldByDefault && !OutfitRows::HoldsAny(defaultRow) && assignment.defaultPieces.empty()) {
				assignment.defaultPieces = FormIDsOf(a_pieces);
			}
			if (assignment.worn == Situation::kDefault && assignment.wornOutfit == a_outfit) {
				assignment.wornOutfit = 0;
			}
		}
		std::erase_if(g_assignments, IsEmpty);
		SyncManaged();
		return wearing;
	}

	// As ForgetOutfit: the pieces worn for the cleared row stay on until the next dressing.
	std::vector<RE::FormID> ForgetSituation(Situations::Situation a_situation)
	{
		std::vector<RE::FormID> wearing;
		if (a_situation == Situation::kDefault) {
			return wearing;
		}
		for (auto& assignment : g_assignments) {
			if ((assignment.worn == a_situation || assignment.layerRow == a_situation) && !assignment.pieces.empty()) {
				wearing.push_back(assignment.actor);
			}
			assignment.rows[RowOf(a_situation)] = {};
		}
		std::erase_if(g_assignments, IsEmpty);
		SyncManaged();
		return wearing;
	}

	void Unassign(RE::Actor& a_actor)
	{
		if (!FindAssignment(a_actor.GetFormID())) {
			return;
		}
		// The player keeps what they wear, less the copies Livery added: unlike an NPC's fixed
		// outfit, what they wore before Livery is no look they would know to expect back.
		if (a_actor.IsPlayerRef()) {
			ReleaseActor(a_actor, true, false);
			Notify(Strings::Get(Strings::Text::kHudNoLongerDressingYou));
			return;
		}
		const auto putBack = ReleaseActor(a_actor, true, true);
		logger::info("Put {} of their own piece(s) back on {:08X}", putBack, a_actor.GetFormID());
		Notify(Strings::Format(Strings::Text::kHudRestored, NameOf(a_actor)));
	}

	std::optional<Choices> ChoicesOf(RE::FormID a_actor)
	{
		const auto* assignment = FindAssignment(a_actor);
		if (!assignment) {
			return std::nullopt;
		}
		std::vector<RE::FormID> wornArmor;
		for (const auto& piece : assignment->pieces) {
			wornArmor.push_back(piece.key.armor);
		}
		return Choices{
			.dressed = !assignment->pieces.empty(),
			.wornArmor = std::move(wornArmor),
			.rows = assignment->rows,
			.defaultPieces = !assignment->defaultPieces.empty(),
			.worn = assignment->worn,
			.wornOutfit = assignment->wornOutfit,
			.layerRow = assignment->layerRow,
			.layer = assignment->layer,
			.overOwn = assignment->overOwn,
		};
	}

	// Release without removing copies takes exactly Livery's locks off (another mod's stay);
	// the pieces then read as unlocked, which PutBackOn sets right again at ResumeLocks. Their
	// outfit copies are unlocked too, so the preview can show one; ResumeLocks locks them again.
	void SuspendLocks(RE::Actor& a_actor)
	{
		const auto id = a_actor.GetFormID();
		auto*      assignment = id == kPlayerRef ? nullptr : FindAssignment(id);
		if (!assignment) {
			return;
		}
		g_suspended = id;
		Dressing::Release(a_actor, assignment->pieces, false);
		for (auto& piece : assignment->pieces) {
			piece.locked = false;
		}
		Dressing::UnlockOutfitCopies(a_actor, assignment->lockedCopies);
		assignment->lockedCopies.clear();
	}

	void ResumeLocks(RE::FormID a_actor)
	{
		if (std::exchange(g_suspended, 0) != a_actor) {
			return;
		}
		PutPiecesBackOn(a_actor);
	}

	// The player's own list is never filtered: a copy added to the player is the player's item.
	std::vector<Dressing::InstanceKey> MarkAddedCopies(RE::Actor& a_actor)
	{
		const auto  id = a_actor.GetFormID();
		const auto* assignment = id == kPlayerRef ? nullptr : FindAssignment(id);
		const auto* base = assignment ? a_actor.GetActorBase() : nullptr;
		if (!base || !base->defaultOutfit) {
			return {};
		}
		return Dressing::MarkAsOutfitItems(a_actor, assignment->pieces, base->defaultOutfit->GetFormID());
	}

	void UnmarkAddedCopies(RE::Actor& a_actor, const std::vector<Dressing::InstanceKey>& a_keys)
	{
		Dressing::UnmarkOutfitItems(a_actor, a_keys);
	}

	void NoteShownInMenu(RE::Actor& a_actor)
	{
		const auto id = a_actor.GetFormID();
		if (id != kPlayerRef && FindAssignment(id)) {
			g_shownInMenu.store(id);
		}
	}

	bool IsDressedNpc(RE::FormID a_actor)
	{
		std::lock_guard lock{ g_managedLock };
		return std::ranges::find(g_dressed, a_actor) != g_dressed.end();
	}

	bool TakeOffAfterPass(RE::FormID a_actor)
	{
		return TakeOffGiven(a_actor, "after the game's worn-gear pass");
	}
}
