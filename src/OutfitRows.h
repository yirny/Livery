// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Situations.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// An actor's rows (docs/situation-management-ui.md §1): the Default and one per situation,
// each a list of library outfits, a random flag, the outfit worn with random off and the
// items each outfit leaves off for this actor.
// Assignments keeps them in the co-save and the capture window shows them; the rules here
// need no game, so the unit tests reach them.
namespace OutfitRows
{
	// The items an outfit on a row leaves off for this actor: those unticked in its piece list
	// on the Wardrobe page (its Apply) or in its details opened from the row on Manage
	// Situations. Its other items, those added to it later included, are worn.
	struct Exclusion
	{
		std::uint32_t              outfit = 0;
		std::vector<std::uint32_t> armor;  // armor FormIDs, ascending, each once, never empty

		friend bool operator==(const Exclusion&, const Exclusion&) = default;
	};

	struct Row
	{
		std::vector<std::uint32_t> outfits;         // library outfit ids, each once, in the user's order (only how a page lists them)
		bool                       random = false;  // wear one of `outfits`, picked as the row goes on
		// Worn with random off; with random on, the last pick worn (Assignments records it), which
		// turning random off keeps on. Always on `outfits`, or 0 for none.
		std::uint32_t chosen = 0;
		// One per outfit on `outfits` that leaves items off, in ascending outfit order
		// (SetExcluded keeps it so, so equal rows compare equal); an outfit not here wears all
		// its items.
		std::vector<Exclusion> excluded;

		friend bool operator==(const Row&, const Row&) = default;
	};

	// One row per Situations::Situation, at the index of its value: the Default first, then
	// the situations in Situations::kChoosable order.
	using Rows = std::array<Row, Situations::kChoosable.size() + 1>;

	constexpr std::size_t RowOf(Situations::Situation a_situation) noexcept
	{
		return static_cast<std::size_t>(a_situation);
	}

	static_assert(
		[] {
			for (std::size_t i = 0; i < Situations::kChoosable.size(); ++i) {
				if (RowOf(Situations::kChoosable[i]) != i + 1) {
					return false;
				}
			}
			return true;
		}(),
		"Rows: the situations follow the Default in Situations::kChoosable order");

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

	// Gives the row library outfit `a_outfit` at a place in its list. Random on: it goes before
	// the outfit at `a_index` of `outfits` (at the end past it), or moves there when on it
	// already. Random off, the list keeps its length: an outfit on it becomes `chosen` where it
	// is; one not on it takes `chosen`'s place, which leaves the list (the user replaces the
	// outfit worn, the items it left off with it); with no `chosen`, it goes in at `a_index`.
	// Either way it is then `chosen`. An outfit on the list keeps the items it leaves off.
	// the Situations page's drop at a place in a row's list.
	inline void Place(Row& a_row, std::uint32_t a_outfit, std::size_t a_index)
	{
		if (a_outfit == 0) {
			return;
		}
		auto&      outfits = a_row.outfits;
		const auto found = std::ranges::find(outfits, a_outfit);
		if (!a_row.random) {
			if (found != outfits.end()) {
				a_row.chosen = a_outfit;
				return;
			}
			if (const auto chosen = std::ranges::find(outfits, a_row.chosen); a_row.chosen != 0 && chosen != outfits.end()) {
				SetExcluded(a_row, a_row.chosen, {});
				*chosen = a_outfit;
				a_row.chosen = a_outfit;
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
			a_row.chosen = a_outfit;
		}
	}

	// Gives the row library outfit `a_outfit` at the front of its list (Place): random off, in
	// `chosen`'s place, and it becomes `chosen`. It leaves `a_excluded` off (SetExcluded), all
	// its items worn with none.
	inline void Give(Row& a_row, std::uint32_t a_outfit, std::vector<std::uint32_t> a_excluded = {})
	{
		Place(a_row, a_outfit, 0);
		SetExcluded(a_row, a_outfit, std::move(a_excluded));
	}

	// Turns random on or off. Off keeps `chosen` on: the last pick worn, or the outfit chosen
	// before random went on; with none recorded yet, the first of the list.
	inline void SetRandom(Row& a_row, bool a_random)
	{
		a_row.random = a_random;
		if (!a_random && a_row.chosen == 0 && !a_row.outfits.empty()) {
			a_row.chosen = a_row.outfits.front();
		}
	}

	// Takes `a_outfit` off `outfits`, with the items it left off; `chosen` becomes 0 if it was
	// that.
	inline void Take(Row& a_row, std::uint32_t a_outfit)
	{
		std::erase(a_row.outfits, a_outfit);
		SetExcluded(a_row, a_outfit, {});
		if (a_row.chosen == a_outfit) {
			a_row.chosen = 0;
		}
	}

	// The row without the outfits `a_inLibrary` rejects, which a row from another save still
	// names after Remove took them out of the library, and the items they left off; `chosen`
	// becomes 0 if it was one. What the window shows and dressing sees: the co-save keeps them,
	// so restoring outfits.json brings them back.
	template <class Has>
	Row WithoutGone(Row a_row, Has&& a_inLibrary)
	{
		std::erase_if(a_row.outfits, [&a_inLibrary](std::uint32_t a_outfit) { return !a_inLibrary(a_outfit); });
		std::erase_if(a_row.excluded, [&a_inLibrary](const Exclusion& a_entry) { return !a_inLibrary(a_entry.outfit); });
		if (a_row.chosen != 0 && !a_inLibrary(a_row.chosen)) {
			a_row.chosen = 0;
		}
		return a_row;
	}

	// `a_edited`, made from WithoutGone(a_stored), with `a_stored`'s gone outfits back at the
	// end of its list, with the items they left off, and its gone `chosen` back while
	// `a_edited` chose none, so an edit keeps what it could not show. An outfit given in a gone
	// `chosen`'s place leaves it on the list.
	template <class Has>
	Row KeepGone(Row a_edited, const Row& a_stored, Has&& a_inLibrary)
	{
		for (const auto outfit : a_stored.outfits) {
			if (!a_inLibrary(outfit) && std::ranges::find(a_edited.outfits, outfit) == a_edited.outfits.end()) {
				a_edited.outfits.push_back(outfit);
				SetExcluded(a_edited, outfit, ExcludedOf(a_stored, outfit));
			}
		}
		if (a_edited.chosen == 0 && a_stored.chosen != 0 && !a_inLibrary(a_stored.chosen)) {
			a_edited.chosen = a_stored.chosen;
		}
		return a_edited;
	}

	// Whether the row holds `a_outfit`: it is `chosen` with random off, on `outfits` with
	// random on. What the window's ticks, headings and marks show. Never id 0.
	inline bool Holds(const Row& a_row, std::uint32_t a_outfit)
	{
		if (a_outfit == 0) {
			return false;
		}
		return a_row.random ? std::ranges::find(a_row.outfits, a_outfit) != a_row.outfits.end() : a_row.chosen == a_outfit;
	}

	// Whether the row holds any outfit. A situation row is worn only then.
	inline bool HoldsAny(const Row& a_row)
	{
		return a_row.random ? !a_row.outfits.empty() : a_row.chosen != 0;
	}

	// The outfit the row wears, 0 for none (the Default then wears its pieces): `chosen` with
	// random off; with random on `a_kept` while it is on `outfits` (the pick worn now, or an
	// outfit just given to the row), else `outfits[a_pick(outfits.size())]`. The caller records
	// a random row's pick in `chosen` once worn (SetRandom keeps it on).
	// Contract: `a_pick(n)` returns an index below n.
	template <class Pick>
	std::uint32_t Wears(const Row& a_row, std::uint32_t a_kept, Pick&& a_pick)
	{
		if (!a_row.random) {
			return a_row.chosen;
		}
		if (a_row.outfits.empty()) {
			return 0;
		}
		if (a_kept != 0 && std::ranges::find(a_row.outfits, a_kept) != a_row.outfits.end()) {
			return a_kept;
		}
		return a_row.outfits[a_pick(a_row.outfits.size())];
	}

	// The row worn in `a_now`: the first situation whose row holds an outfit, else the Default.
	inline Situations::Situation Resolve(const Rows& a_rows, const Situations::Applicable& a_now)
	{
		for (std::size_t i = 0; i < a_now.count; ++i) {
			if (HoldsAny(a_rows[RowOf(a_now.items[i])])) {
				return a_now.items[i];
			}
		}
		return Situations::Situation::kDefault;
	}
}
