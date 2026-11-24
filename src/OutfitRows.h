// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Situations.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// An actor's rows (docs/situation-management-ui.md §1): the Default and one per situation,
// each a list of library outfits, a random flag, the base and the layer worn with random off,
// the items each outfit leaves off for this actor and each outfit's selection chance.
// Assignments keeps them in the co-save and the capture window shows them; the rules here
// need no game, so the unit tests reach them.
//
// A layer (Library::Outfit::layer) is worn over the base, the outfit beneath (docs/design.md
// §7). The functions that tell the two apart take `a_isLayer(id)`, whether library outfit `id`
// is a layer now; it is read anew each time, so an outfit whose flag changed since it was
// given is worn as what it is now.
namespace OutfitRows
{
	// An outfit's selection chance on a random row: how often it is picked against the others
	// on the list, 0 never (Pick). Every outfit has the default until the user moves it.
	inline constexpr std::uint8_t kDefaultChance = 100;
	inline constexpr std::uint8_t kMaxChance = 200;

	// The items an outfit on a row leaves off for this actor: those unticked in its piece list
	// on the Wardrobe page (its Apply) or in its details opened from the row on Manage
	// Situations. Its other items, those added to it later included, are worn.
	struct Exclusion
	{
		std::uint32_t              outfit = 0;
		std::vector<std::uint32_t> armor;  // armor FormIDs, ascending, each once, never empty

		friend bool operator==(const Exclusion&, const Exclusion&) = default;
	};

	// An outfit on a row whose selection chance is not kDefaultChance.
	struct Chance
	{
		std::uint32_t outfit = 0;
		std::uint8_t  value = kDefaultChance;  // 0 to kMaxChance, never kDefaultChance

		friend bool operator==(const Chance&, const Chance&) = default;
	};

	struct Row
	{
		std::vector<std::uint32_t> outfits;         // library outfit ids, each once, in the user's order (only how a page lists them)
		bool                       random = false;  // wear one of `outfits`, picked as the row goes on
		// The base worn with random off; with random on, the last base picked (Assignments records
		// it), which turning random off keeps on. Always on `outfits`, or 0 for none.
		std::uint32_t chosen = 0;
		// The layer worn with random off; with random on, the last layer picked (a draw of none
		// leaves it), which turning random off keeps on. Always on `outfits`, or 0 for none.
		// ChosenOf reads the two by what each outfit is now.
		std::uint32_t chosenLayer = 0;
		// One per outfit on `outfits` that leaves items off, in ascending outfit order
		// (SetExcluded keeps it so, so equal rows compare equal); an outfit not here wears all
		// its items.
		std::vector<Exclusion> excluded;
		// One per outfit on `outfits` whose selection chance is not kDefaultChance, in ascending
		// outfit order (SetChance keeps it so). Kept with random off, for random on again.
		std::vector<Chance> chances;

		friend bool operator==(const Row&, const Row&) = default;
	};

	// One row per Situations::Situation, at the index of its id (RowOf): the Default first, then
	// the presets, then the user's situations in the order made. A row not stored yet is empty:
	// reading one past the stored rows gives an empty row, and writing one stores the rows up to it.
	class Rows
	{
	public:
		Row& operator[](std::size_t a_index)
		{
			if (a_index >= rows.size()) {
				rows.resize(a_index + 1);
			}
			return rows[a_index];
		}

		const Row& operator[](std::size_t a_index) const
		{
			static const Row empty;
			return a_index < rows.size() ? rows[a_index] : empty;
		}

		// The rows stored, never more than Situations::Count(); those past them are empty.
		std::size_t size() const noexcept { return rows.size(); }

		auto begin() noexcept { return rows.begin(); }
		auto end() noexcept { return rows.end(); }
		auto begin() const noexcept { return rows.begin(); }
		auto end() const noexcept { return rows.end(); }

	private:
		std::vector<Row> rows;
	};

	constexpr std::size_t RowOf(Situations::Situation a_situation) noexcept
	{
		return static_cast<std::size_t>(a_situation);
	}

	// The items `a_outfit` leaves off on the row (Exclusion::armor); empty for none.
	inline std::vector<std::uint32_t> ExcludedOf(const Row& a_row, std::uint32_t a_outfit)
	{
		const auto found = std::ranges::find(a_row.excluded, a_outfit, &Exclusion::outfit);
		return found != a_row.excluded.end() ? found->armor : std::vector<std::uint32_t>{};
	}

	// From now on `a_outfit` leaves `a_armor` (armor FormIDs in any order) off on the row;
	// empty wears all its items.
	// Contract: `a_outfit` is on `outfits`, or `a_armor` is empty.
	inline void SetExcluded(Row& a_row, std::uint32_t a_outfit, std::vector<std::uint32_t> a_armor)
	{
		std::erase_if(a_row.excluded, [a_outfit](const Exclusion& a_entry) { return a_entry.outfit == a_outfit; });
		if (a_outfit == 0 || a_armor.empty()) {
			return;
		}
		std::ranges::sort(a_armor);
		const auto repeats = std::ranges::unique(a_armor);
		a_armor.erase(repeats.begin(), repeats.end());
		const auto at = std::ranges::lower_bound(a_row.excluded, a_outfit, {}, &Exclusion::outfit);
		a_row.excluded.insert(at, { .outfit = a_outfit, .armor = std::move(a_armor) });
	}

	// `a_outfit`'s selection chance on the row; kDefaultChance for one not set.
	inline std::uint8_t ChanceOf(const Row& a_row, std::uint32_t a_outfit)
	{
		const auto found = std::ranges::find(a_row.chances, a_outfit, &Chance::outfit);
		return found != a_row.chances.end() ? found->value : kDefaultChance;
	}

	// From now on `a_outfit` has selection chance `a_value` on the row, kMaxChance at most.
	// Contract: `a_outfit` is on `outfits`, or `a_value` is kDefaultChance.
	inline void SetChance(Row& a_row, std::uint32_t a_outfit, std::uint8_t a_value)
	{
		std::erase_if(a_row.chances, [a_outfit](const Chance& a_entry) { return a_entry.outfit == a_outfit; });
		a_value = (std::min)(a_value, kMaxChance);
		if (a_outfit == 0 || a_value == kDefaultChance) {
			return;
		}
		const auto at = std::ranges::lower_bound(a_row.chances, a_outfit, {}, &Chance::outfit);
		a_row.chances.insert(at, { .outfit = a_outfit, .value = a_value });
	}

	// The pieces worn of an outfit's `a_pieces`: those whose armor (`a_armorOf`) `a_excluded`
	// does not name, or all of them when that leaves none, as a library update that took every
	// other item out of the outfit would.
	template <class Piece, class ArmorOf>
	std::vector<Piece> WornPieces(const std::vector<Piece>& a_pieces, const std::vector<std::uint32_t>& a_excluded, ArmorOf&& a_armorOf)
	{
		auto worn = a_pieces;
		std::erase_if(worn, [&](const Piece& a_piece) { return std::ranges::find(a_excluded, a_armorOf(a_piece)) != a_excluded.end(); });
		return worn.empty() ? a_pieces : worn;
	}

	// The base's pieces worn under a layer: those of `a_base` whose body slots (`a_slotsOf`, a
	// mask) no piece of `a_layer` takes, as equipping the layer would push the others off. The
	// dressing puts these on with the layer's pieces, so no base piece it would push off is
	// ever on to be put back.
	template <class Piece, class SlotsOf>
	std::vector<Piece> Uncovered(const std::vector<Piece>& a_base, const std::vector<Piece>& a_layer, SlotsOf&& a_slotsOf)
	{
		std::uint32_t taken = 0;
		for (const auto& piece : a_layer) {
			taken |= a_slotsOf(piece);
		}
		std::vector<Piece> uncovered;
		for (const auto& piece : a_base) {
			if ((a_slotsOf(piece) & taken) == 0) {
				uncovered.push_back(piece);
			}
		}
		return uncovered;
	}

	// The outfit of one kind the row wears with random off, a layer with `a_layer`, else the
	// base: `chosenLayer` for a layer and `chosen` for a base while each is of that kind, else
	// the other one when it is (an outfit whose flag changed since it was given); 0 for none.
	template <class IsLayer>
	std::uint32_t ChosenOf(const Row& a_row, bool a_layer, IsLayer&& a_isLayer)
	{
		const auto own = a_layer ? a_row.chosenLayer : a_row.chosen;
		const auto other = a_layer ? a_row.chosen : a_row.chosenLayer;
		if (own != 0 && a_isLayer(own) == a_layer) {
			return own;
		}
		if (other != 0 && a_isLayer(other) == a_layer) {
			return other;
		}
		return 0;
	}

	// Moves each chosen outfit to the field of its kind where that is free or holds the other
	// kind: a layer in `chosen` to `chosenLayer`, a base in `chosenLayer` to `chosen`. Two of one
	// kind stay as they are (ChosenOf wears one). Before an edit that sets the field of a kind.
	template <class IsLayer>
	void SortChosen(Row& a_row, IsLayer&& a_isLayer)
	{
		const bool baseIsLayer = a_row.chosen != 0 && a_isLayer(a_row.chosen);
		const bool layerIsBase = a_row.chosenLayer != 0 && !a_isLayer(a_row.chosenLayer);
		if ((baseIsLayer && (a_row.chosenLayer == 0 || layerIsBase)) || (layerIsBase && a_row.chosen == 0)) {
			std::swap(a_row.chosen, a_row.chosenLayer);
		}
	}

	// Gives the row library outfit `a_outfit` at a place in its list. Random on: it goes before
	// the outfit at `a_index` of `outfits` (at the end past it), or moves there when on it
	// already. Random off, the list keeps its length and each kind has its own chosen outfit
	// (`chosen` for a base, `chosenLayer` for a layer): an outfit on the list becomes the one of
	// its kind where it is; one not on it takes the place of the one of its kind, which leaves
	// the list (the user replaces the outfit worn, the items it left off and its chance with
	// it); with none of its kind, it goes in at `a_index`. Either way it is then the one of its
	// kind. An outfit on the list keeps the items it leaves off and its chance.
	// the Situations page's drop at a place in a row's list.
	template <class IsLayer>
	void Place(Row& a_row, std::uint32_t a_outfit, std::size_t a_index, IsLayer&& a_isLayer)
	{
		if (a_outfit == 0) {
			return;
		}
		auto&      outfits = a_row.outfits;
		const auto found = std::ranges::find(outfits, a_outfit);
		const bool layer = a_isLayer(a_outfit);
		if (!a_row.random) {
			SortChosen(a_row, a_isLayer);
			auto& chosen = layer ? a_row.chosenLayer : a_row.chosen;
			auto& other = layer ? a_row.chosen : a_row.chosenLayer;
			if (found != outfits.end()) {
				chosen = a_outfit;
				if (other == a_outfit) {
					other = 0;  // it was chosen as the other kind before its flag changed
				}
				return;
			}
			if (const auto worn = std::ranges::find(outfits, chosen); chosen != 0 && worn != outfits.end()) {
				SetExcluded(a_row, chosen, {});
				SetChance(a_row, chosen, kDefaultChance);
				*worn = a_outfit;
				chosen = a_outfit;
				return;
			}
		} else if (found != outfits.end()) {
			if (static_cast<std::size_t>(found - outfits.begin()) < a_index) {
				--a_index;  // the outfits after it move up one
			}
			outfits.erase(found);
		}
		a_index = (std::min)(a_index, outfits.size());
		outfits.insert(outfits.begin() + static_cast<std::ptrdiff_t>(a_index), a_outfit);
		if (!a_row.random) {
			(layer ? a_row.chosenLayer : a_row.chosen) = a_outfit;
		}
	}

	// Gives the row library outfit `a_outfit` at the front of its list (Place): random off, in
	// the place of the one of its kind, and it becomes that. It leaves `a_excluded` off
	// (SetExcluded), all its items worn with none.
	template <class IsLayer>
	void Give(Row& a_row, std::uint32_t a_outfit, std::vector<std::uint32_t> a_excluded, IsLayer&& a_isLayer)
	{
		Place(a_row, a_outfit, 0, a_isLayer);
		SetExcluded(a_row, a_outfit, std::move(a_excluded));
	}

	// Turns random on or off. Off keeps each kind's chosen outfit on: the last pick worn, or the
	// one chosen before random went on; with none recorded yet, the first of that kind on the
	// list.
	template <class IsLayer>
	void SetRandom(Row& a_row, bool a_random, IsLayer&& a_isLayer)
	{
		a_row.random = a_random;
		if (a_random) {
			return;
		}
		SortChosen(a_row, a_isLayer);
		const auto firstOf = [&a_row, &a_isLayer](bool a_layer) -> std::uint32_t {
			const auto found = std::ranges::find_if(a_row.outfits, [&](std::uint32_t a_outfit) { return a_isLayer(a_outfit) == a_layer; });
			return found != a_row.outfits.end() ? *found : 0;
		};
		if (a_row.chosen == 0) {
			a_row.chosen = firstOf(false);
		}
		if (a_row.chosenLayer == 0) {
			a_row.chosenLayer = firstOf(true);
		}
	}

	// Takes `a_outfit` off `outfits`, with the items it left off and its chance; `chosen` or
	// `chosenLayer` becomes 0 if it was that.
	inline void Take(Row& a_row, std::uint32_t a_outfit)
	{
		std::erase(a_row.outfits, a_outfit);
		SetExcluded(a_row, a_outfit, {});
		SetChance(a_row, a_outfit, kDefaultChance);
		if (a_row.chosen == a_outfit) {
			a_row.chosen = 0;
		}
		if (a_row.chosenLayer == a_outfit) {
			a_row.chosenLayer = 0;
		}
	}

	// The row without the outfits `a_inLibrary` rejects, which a row from another save still
	// names after Remove took them out of the library, the items they left off and their
	// chances; `chosen` or `chosenLayer` becomes 0 if it was one. What the window shows and
	// dressing sees: the co-save keeps them, so restoring outfits.json brings them back.
	template <class Has>
	Row WithoutGone(Row a_row, Has&& a_inLibrary)
	{
		std::erase_if(a_row.outfits, [&a_inLibrary](std::uint32_t a_outfit) { return !a_inLibrary(a_outfit); });
		std::erase_if(a_row.excluded, [&a_inLibrary](const Exclusion& a_entry) { return !a_inLibrary(a_entry.outfit); });
		std::erase_if(a_row.chances, [&a_inLibrary](const Chance& a_entry) { return !a_inLibrary(a_entry.outfit); });
		if (a_row.chosen != 0 && !a_inLibrary(a_row.chosen)) {
			a_row.chosen = 0;
		}
		if (a_row.chosenLayer != 0 && !a_inLibrary(a_row.chosenLayer)) {
			a_row.chosenLayer = 0;
		}
		return a_row;
	}

	// `a_edited`, made from WithoutGone(a_stored), with `a_stored`'s gone outfits back at the
	// end of its list, with the items they left off and their chances, and its gone `chosen` and
	// `chosenLayer` back while `a_edited` chose none in that field, so an edit keeps what it
	// could not show. An outfit given in a gone chosen outfit's place leaves it on the list.
	template <class Has>
	Row KeepGone(Row a_edited, const Row& a_stored, Has&& a_inLibrary)
	{
		for (const auto outfit : a_stored.outfits) {
			if (!a_inLibrary(outfit) && std::ranges::find(a_edited.outfits, outfit) == a_edited.outfits.end()) {
				a_edited.outfits.push_back(outfit);
				SetExcluded(a_edited, outfit, ExcludedOf(a_stored, outfit));
				SetChance(a_edited, outfit, ChanceOf(a_stored, outfit));
			}
		}
		if (a_edited.chosen == 0 && a_stored.chosen != 0 && !a_inLibrary(a_stored.chosen)) {
			a_edited.chosen = a_stored.chosen;
		}
		if (a_edited.chosenLayer == 0 && a_stored.chosenLayer != 0 && !a_inLibrary(a_stored.chosenLayer)) {
			a_edited.chosenLayer = a_stored.chosenLayer;
		}
		return a_edited;
	}

	// Whether the row holds `a_outfit`: it is `chosen` or `chosenLayer` with random off, on
	// `outfits` with random on. What the window's ticks, headings and marks show. Never id 0.
	inline bool Holds(const Row& a_row, std::uint32_t a_outfit)
	{
		if (a_outfit == 0) {
			return false;
		}
		if (a_row.random) {
			return std::ranges::find(a_row.outfits, a_outfit) != a_row.outfits.end();
		}
		return a_row.chosen == a_outfit || a_row.chosenLayer == a_outfit;
	}

	// Whether the row holds any outfit: what the window shows and the record keeps.
	// Situations::Evaluate reads the conditions of these rows only, a random row's at any chance:
	// its layers all at 0 still end the walk for a layer (PicksLayer). PicksBase and PicksLayer
	// tell which kind the row gives.
	inline bool HoldsAny(const Row& a_row)
	{
		return a_row.random ? !a_row.outfits.empty() : a_row.chosen != 0 || a_row.chosenLayer != 0;
	}

	// Whether the row has a base to wear: ChosenOf a base with random off, a base on the list
	// whose chance is above 0 with random on. A situation row is worn as the base only then
	// (Resolve).
	template <class IsLayer>
	bool PicksBase(const Row& a_row, IsLayer&& a_isLayer)
	{
		if (!a_row.random) {
			return ChosenOf(a_row, false, a_isLayer) != 0;
		}
		return std::ranges::any_of(a_row.outfits, [&](std::uint32_t a_outfit) { return !a_isLayer(a_outfit) && ChanceOf(a_row, a_outfit) > 0; });
	}

	// Whether the row has a layer to wear: ChosenOf a layer that `a_wearable` accepts (one with
	// a loaded item) with random off, such a layer on the list at any chance with random on. The
	// row then gives the layer, even when its draw gives none, as it always does with every one
	// at 0 (ResolveLayer, DrawLayer).
	template <class IsLayer, class Wearable>
	bool PicksLayer(const Row& a_row, IsLayer&& a_isLayer, Wearable&& a_wearable)
	{
		if (!a_row.random) {
			const auto layer = ChosenOf(a_row, true, a_isLayer);
			return layer != 0 && a_wearable(layer);
		}
		return std::ranges::any_of(a_row.outfits, [&](std::uint32_t a_outfit) { return a_isLayer(a_outfit) && a_wearable(a_outfit); });
	}

	// The sum of the chances (ChanceOf) of the row's outfits that `a_candidate` accepts, other
	// than `a_except` (0 for none): what Pick draws from, so an outfit's chance over the sum is
	// the chance a pick is that outfit.
	template <class Candidate>
	std::size_t ChanceTotal(const Row& a_row, std::uint32_t a_except, Candidate&& a_candidate)
	{
		std::size_t total = 0;
		for (const auto outfit : a_row.outfits) {
			if (outfit != a_except && a_candidate(outfit)) {
				total += ChanceOf(a_row, outfit);
			}
		}
		return total;
	}

	// An outfit of the row's list that `a_candidate` accepts, other than `a_except` (0 for none),
	// picked in proportion to the outfits' chances (ChanceOf): one with chance 0 never. 0 when
	// none is left.
	// Contract: `a_draw(n)` returns a number below n, n above 0.
	template <class Draw, class Candidate>
	std::uint32_t Pick(const Row& a_row, std::uint32_t a_except, Draw&& a_draw, Candidate&& a_candidate)
	{
		const auto chanceOf = [&](std::uint32_t a_outfit) -> std::size_t {
			return a_outfit != a_except && a_candidate(a_outfit) ? ChanceOf(a_row, a_outfit) : 0;
		};
		const auto total = ChanceTotal(a_row, a_except, a_candidate);
		if (total == 0) {
			return 0;
		}
		auto drawn = static_cast<std::size_t>(a_draw(total));
		for (const auto outfit : a_row.outfits) {
			if (drawn < chanceOf(outfit)) {
				return outfit;
			}
			drawn -= chanceOf(outfit);
		}
		return 0;  // only for a draw past the contract
	}

	// A layer drawn from the row's list among those `a_candidate` accepts, or 0 for none: no
	// layer with probability Π(1 − min(c, 100) / 100) over their chances c (each a trial at
	// min(c, 100) in 100, the first success ending them), so one at 70 alone is worn 70% of the
	// time and one at 100 or above means a layer always; else one picked in proportion to the
	// chances (Pick).
	// Contract: as Pick's for `a_draw`.
	template <class Draw, class Candidate>
	std::uint32_t DrawLayer(const Row& a_row, Draw&& a_draw, Candidate&& a_candidate)
	{
		for (const auto outfit : a_row.outfits) {
			if (!a_candidate(outfit)) {
				continue;
			}
			const auto chance = (std::min)(ChanceOf(a_row, outfit), kDefaultChance);
			if (chance >= kDefaultChance || (chance > 0 && static_cast<std::size_t>(a_draw(std::size_t{ kDefaultChance })) < std::size_t{ chance })) {
				return Pick(a_row, 0, a_draw, a_candidate);
			}
		}
		return 0;
	}

	// The chance DrawLayer draws a layer at all, 0 to 1: 1 − Π(1 − min(c, 100) / 100) over the
	// chances c of the layers `a_candidate` accepts. A layer is worn with this times its chance
	// over their ChanceTotal.
	template <class Candidate>
	double LayerDrawChance(const Row& a_row, Candidate&& a_candidate)
	{
		double none = 1.0;
		for (const auto outfit : a_row.outfits) {
			if (a_candidate(outfit)) {
				none *= 1.0 - static_cast<double>((std::min)(ChanceOf(a_row, outfit), kDefaultChance)) / kDefaultChance;
			}
		}
		return 1.0 - none;
	}

	// The base the row wears, 0 for none (the Default then wears its pieces): ChosenOf a base
	// with random off; with random on `a_kept` while it is a base on `outfits` (the pick worn
	// now, or an outfit just given to the row), whatever its chance, else a new pick among the
	// bases (Pick). The caller records a random row's pick in `chosen` once worn (SetRandom
	// keeps it on).
	// Contract: as Pick's for `a_draw`.
	template <class Draw, class IsLayer>
	std::uint32_t Wears(const Row& a_row, std::uint32_t a_kept, Draw&& a_draw, IsLayer&& a_isLayer)
	{
		if (!a_row.random) {
			return ChosenOf(a_row, false, a_isLayer);
		}
		if (a_kept != 0 && !a_isLayer(a_kept) && std::ranges::find(a_row.outfits, a_kept) != a_row.outfits.end()) {
			return a_kept;
		}
		return Pick(a_row, 0, a_draw, [&a_isLayer](std::uint32_t a_outfit) { return !a_isLayer(a_outfit); });
	}

	// The layer the row wears, 0 for none: ChosenOf a layer with random off, while `a_wearable`
	// accepts it; with random on none while every wearable layer on `outfits` is at 0, as a row
	// whose bases are all at 0 is passed over (PicksBase); else `a_kept` when set: 0 (a draw of
	// none, kept while the row stays the layer row), or a wearable layer on `outfits`, whatever
	// its chance; else a new draw among the wearable layers (DrawLayer). The caller records a
	// random row's layer in `chosenLayer` once worn.
	// Contract: as Pick's for `a_draw`.
	template <class Draw, class IsLayer, class Wearable>
	std::uint32_t WearsLayer(const Row& a_row, std::optional<std::uint32_t> a_kept, Draw&& a_draw, IsLayer&& a_isLayer, Wearable&& a_wearable)
	{
		const auto candidate = [&](std::uint32_t a_outfit) { return a_isLayer(a_outfit) && a_wearable(a_outfit); };
		if (!a_row.random) {
			const auto layer = ChosenOf(a_row, true, a_isLayer);
			return layer != 0 && a_wearable(layer) ? layer : 0;
		}
		if (std::ranges::none_of(a_row.outfits, [&](std::uint32_t a_outfit) { return candidate(a_outfit) && ChanceOf(a_row, a_outfit) > 0; })) {
			return 0;
		}
		if (a_kept && (*a_kept == 0 || (candidate(*a_kept) && std::ranges::find(a_row.outfits, *a_kept) != a_row.outfits.end()))) {
			return *a_kept;
		}
		return DrawLayer(a_row, a_draw, candidate);
	}

	// The row the base is worn from in `a_now`: the first situation whose row has a base to wear
	// (PicksBase), else the Default.
	template <class IsLayer>
	Situations::Situation Resolve(const Rows& a_rows, const Situations::Applicable& a_now, IsLayer&& a_isLayer)
	{
		for (std::size_t i = 0; i < a_now.count; ++i) {
			if (PicksBase(a_rows[RowOf(a_now.items[i])], a_isLayer)) {
				return a_now.items[i];
			}
		}
		return Situations::Situation::kDefault;
	}

	// The row the layer is worn from in `a_now`, apart from the base's: the first situation
	// whose row has a layer to wear (PicksLayer), else the Default when it has one; nullopt
	// for none.
	template <class IsLayer, class Wearable>
	std::optional<Situations::Situation> ResolveLayer(const Rows& a_rows, const Situations::Applicable& a_now, IsLayer&& a_isLayer, Wearable&& a_wearable)
	{
		for (std::size_t i = 0; i < a_now.count; ++i) {
			if (PicksLayer(a_rows[RowOf(a_now.items[i])], a_isLayer, a_wearable)) {
				return a_now.items[i];
			}
		}
		if (PicksLayer(a_rows[RowOf(Situations::Situation::kDefault)], a_isLayer, a_wearable)) {
			return Situations::Situation::kDefault;
		}
		return std::nullopt;
	}
}
