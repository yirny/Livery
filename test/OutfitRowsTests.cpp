#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "OutfitRows.h"

namespace
{
	using OutfitRows::Row;
	using Situations::Situation;

	using Ids = std::vector<std::uint32_t>;

	// A pick that must not be drawn: the row keeps what it has.
	std::size_t NoPick(std::size_t)
	{
		FAIL("no pick expected");
		return 0;
	}

	// OutfitRows' `a_isLayer` for rows of bases only.
	bool NoLayer(std::uint32_t)
	{
		return false;
	}

	// Pick's `a_candidate` that takes every outfit.
	bool Any(std::uint32_t)
	{
		return true;
	}

	// In the layer tests, ids from 100 up are layers.
	bool IsLayer(std::uint32_t a_outfit)
	{
		return a_outfit >= 100;
	}

	// OutfitRows' `a_wearable`: every layer has an item loaded.
	bool Loaded(std::uint32_t)
	{
		return true;
	}

	// A draw that hands out `values` in turn, each checked below the count asked.
	struct Draws
	{
		std::vector<std::size_t> values;
		std::vector<std::size_t> asked;

		std::size_t operator()(std::size_t a_count)
		{
			REQUIRE(asked.size() < values.size());
			const auto value = values[asked.size()];
			asked.push_back(a_count);
			REQUIRE(value < a_count);
			return value;
		}
	};
}

TEST_CASE("Give with random off replaces the chosen outfit in its place", "[rows]")
{
	Row row;
	OutfitRows::Give(row, 1, {}, NoLayer);  // an empty list: added
	CHECK(row.outfits == Ids{ 1 });
	CHECK(row.chosen == 1);

	OutfitRows::Give(row, 2, {}, NoLayer);  // takes the chosen one's place, which leaves
	CHECK(row.outfits == Ids{ 2 });
	CHECK(row.chosen == 2);

	Row hidden{ .outfits = { 3, 1, 2 }, .chosen = 1 };  // others on the list from random
	OutfitRows::Give(hidden, 4, {}, NoLayer);
	CHECK(hidden.outfits == Ids{ 3, 4, 2 });
	CHECK(hidden.chosen == 4);

	OutfitRows::Give(hidden, 2, {}, NoLayer);  // on the list already: chosen where it is
	CHECK(hidden.outfits == Ids{ 3, 4, 2 });
	CHECK(hidden.chosen == 2);

	Row none{ .outfits = { 1, 2 } };  // nothing chosen: added at the front
	OutfitRows::Give(none, 3, {}, NoLayer);
	CHECK(none.outfits == Ids{ 3, 1, 2 });
	CHECK(none.chosen == 3);

	OutfitRows::Give(none, 0, {}, NoLayer);  // not an outfit
	CHECK(none.outfits == Ids{ 3, 1, 2 });
	CHECK(none.chosen == 3);
}

TEST_CASE("Give with random on adds to the list and leaves chosen", "[rows]")
{
	Row row{ .outfits = { 1 }, .random = true, .chosen = 1 };
	OutfitRows::Give(row, 2, {}, NoLayer);
	CHECK(row.outfits == Ids{ 2, 1 });
	CHECK(row.chosen == 1);
}

TEST_CASE("Place with random on puts the outfit at the drop place, moving it when on the list", "[rows]")
{
	Row row{ .outfits = { 1, 2, 3 }, .random = true };
	OutfitRows::Place(row, 1, 2, NoLayer);  // between 2 and 3
	CHECK(row.outfits == Ids{ 2, 1, 3 });
	OutfitRows::Place(row, 3, 0, NoLayer);
	CHECK(row.outfits == Ids{ 3, 2, 1 });
	OutfitRows::Place(row, 3, 3, NoLayer);  // after the last
	CHECK(row.outfits == Ids{ 2, 1, 3 });
	OutfitRows::Place(row, 4, 1, NoLayer);  // not on the list yet
	CHECK(row.outfits == Ids{ 2, 4, 1, 3 });
	OutfitRows::Place(row, 5, 9, NoLayer);  // past the end: last
	CHECK(row.outfits == Ids{ 2, 4, 1, 3, 5 });
	CHECK(row.chosen == 0);  // random on leaves chosen

	Row off{ .outfits = { 1, 2 } };  // random off with nothing chosen: the place counts
	OutfitRows::Place(off, 3, 1, NoLayer);
	CHECK(off.outfits == Ids{ 1, 3, 2 });
	CHECK(off.chosen == 3);
	OutfitRows::Place(off, 0, 0, NoLayer);  // not an outfit
	CHECK(off.outfits == Ids{ 1, 3, 2 });
}

TEST_CASE("SetRandom off keeps the last pick, or chooses the first with none", "[rows]")
{
	Row row{ .outfits = { 1, 2 }, .random = true, .chosen = 2 };  // the pick worn last
	OutfitRows::SetRandom(row, false, NoLayer);
	CHECK_FALSE(row.random);
	CHECK(row.chosen == 2);

	Row fresh{ .outfits = { 3, 4 }, .random = true };  // never worn
	OutfitRows::SetRandom(fresh, false, NoLayer);
	CHECK(fresh.chosen == 3);

	Row empty{ .random = true };
	OutfitRows::SetRandom(empty, false, NoLayer);
	CHECK(empty.chosen == 0);

	OutfitRows::SetRandom(row, true, NoLayer);
	CHECK(row.random);
	CHECK(row.chosen == 2);  // kept for the next off
}

TEST_CASE("Take removes the outfit and clears chosen only when it was that", "[rows]")
{
	Row row{ .outfits = { 2, 1 }, .chosen = 2 };
	OutfitRows::Take(row, 1);
	CHECK(row.outfits == Ids{ 2 });
	CHECK(row.chosen == 2);

	OutfitRows::Take(row, 2);
	CHECK(row.outfits.empty());
	CHECK(row.chosen == 0);

	Row hidden{ .outfits = { 2, 1 }, .chosen = 2 };
	OutfitRows::Take(hidden, 2);  // the outfit hidden behind it is not chosen in its place
	CHECK(hidden.outfits == Ids{ 1 });
	CHECK(hidden.chosen == 0);
}

TEST_CASE("WithoutGone leaves out the outfits the library lacks, KeepGone puts them back", "[rows]")
{
	const auto inLibrary = [](std::uint32_t a_outfit) { return a_outfit < 10; };  // 10 and up are gone

	const Row stored{ .outfits = { 10, 1, 11 }, .chosen = 10 };
	const Row shown = OutfitRows::WithoutGone(stored, inLibrary);
	CHECK(shown.outfits == Ids{ 1 });
	CHECK(shown.chosen == 0);
	CHECK(OutfitRows::WithoutGone(Row{ .outfits = { 1, 2 }, .chosen = 2 }, inLibrary) == Row{ .outfits = { 1, 2 }, .chosen = 2 });

	const Row kept = OutfitRows::KeepGone(shown, stored, inLibrary);  // saved as shown
	CHECK(kept.outfits == Ids{ 1, 10, 11 });
	CHECK(kept.chosen == 10);

	Row edited = shown;
	OutfitRows::Give(edited, 2, {}, NoLayer);  // an outfit dropped where the gone one was chosen
	const Row replaced = OutfitRows::KeepGone(edited, stored, inLibrary);
	CHECK(replaced.outfits == Ids{ 2, 1, 10, 11 });
	CHECK(replaced.chosen == 2);

	const Row random{ .outfits = { 10, 1 }, .random = true, .chosen = 1 };
	const Row randomShown = OutfitRows::WithoutGone(random, inLibrary);
	CHECK(randomShown.outfits == Ids{ 1 });
	CHECK(randomShown.chosen == 1);
	CHECK(OutfitRows::KeepGone(randomShown, random, inLibrary) == Row{ .outfits = { 1, 10 }, .random = true, .chosen = 1 });
}

TEST_CASE("Holds is chosen with random off and the list with random on", "[rows]")
{
	const Row off{ .outfits = { 2, 1 }, .chosen = 2 };
	CHECK(OutfitRows::Holds(off, 2));
	CHECK_FALSE(OutfitRows::Holds(off, 1));  // hidden
	CHECK_FALSE(OutfitRows::Holds(off, 0));
	CHECK(OutfitRows::HoldsAny(off));

	const Row none{ .outfits = { 1 } };
	CHECK_FALSE(OutfitRows::Holds(none, 1));
	CHECK_FALSE(OutfitRows::HoldsAny(none));

	const Row random{ .outfits = { 2, 1 }, .random = true };
	CHECK(OutfitRows::Holds(random, 1));
	CHECK(OutfitRows::Holds(random, 2));
	CHECK(OutfitRows::HoldsAny(random));
	CHECK_FALSE(OutfitRows::HoldsAny(Row{ .random = true, .chosen = 1 }));
}

TEST_CASE("Wears gives chosen, or a random row's kept pick, or a new pick", "[rows]")
{
	CHECK(OutfitRows::Wears(Row{ .outfits = { 2, 1 }, .chosen = 1 }, 2, NoPick, NoLayer) == 1);
	CHECK(OutfitRows::Wears(Row{ .outfits = { 1 } }, 1, NoPick, NoLayer) == 0);

	const Row random{ .outfits = { 3, 2, 1 }, .random = true };
	CHECK(OutfitRows::Wears(random, 2, NoPick, NoLayer) == 2);  // kept while on the list
	std::size_t asked = 0;
	const auto  pickLast = [&asked](std::size_t a_count) {
		asked = a_count;
		return a_count - 1;
	};
	CHECK(OutfitRows::Wears(random, 9, pickLast, NoLayer) == 1);  // gone from the list: picks again
	CHECK(asked == 3 * OutfitRows::kDefaultChance);
	CHECK(OutfitRows::Wears(random, 0, pickLast, NoLayer) == 1);
	CHECK(OutfitRows::Wears(Row{ .random = true }, 0, NoPick, NoLayer) == 0);
}

TEST_CASE("Each situation's row is at its id; a row not stored is empty", "[rows]")
{
	for (const auto situation : Situations::Order()) {
		CHECK(OutfitRows::RowOf(situation) == static_cast<std::size_t>(situation));
	}

	OutfitRows::Rows rows;
	const auto&      view = rows;
	CHECK(view[OutfitRows::RowOf(Situation::kCold)] == Row{});
	CHECK(rows.size() == 0);
	rows[OutfitRows::RowOf(Situation::kHome)].chosen = 1;
	CHECK(rows.size() == OutfitRows::RowOf(Situation::kHome) + 1);
	CHECK(view[OutfitRows::RowOf(Situation::kHome)].chosen == 1);
	CHECK(view[OutfitRows::RowOf(Situation::kCold)] == Row{});
}

TEST_CASE("Resolve takes the first situation whose row holds an outfit", "[rows]")
{
	OutfitRows::Rows rows{};
	rows[OutfitRows::RowOf(Situation::kDefault)] = { .outfits = { 1 }, .chosen = 1 };
	rows[OutfitRows::RowOf(Situation::kTown)] = { .outfits = { 2 }, .chosen = 2 };
	rows[OutfitRows::RowOf(Situation::kCold)] = { .outfits = { 3 } };  // nothing chosen

	Situations::Applicable now;
	CHECK(OutfitRows::Resolve(rows, now, NoLayer) == Situation::kDefault);
	now.Add(Situation::kCold);
	now.Add(Situation::kTown);
	CHECK(OutfitRows::Resolve(rows, now, NoLayer) == Situation::kTown);

	rows[OutfitRows::RowOf(Situation::kCold)].random = true;
	CHECK(OutfitRows::Resolve(rows, now, NoLayer) == Situation::kCold);

	Situations::Applicable home;
	home.Add(Situation::kHome);
	CHECK(OutfitRows::Resolve(rows, home, NoLayer) == Situation::kDefault);
}

TEST_CASE("SetExcluded keeps each outfit's items sorted and drops an empty one", "[rows][excluded]")
{
	Row row{ .outfits = { 5, 2 }, .chosen = 5 };
	OutfitRows::SetExcluded(row, 5, { 30, 10, 30 });
	OutfitRows::SetExcluded(row, 2, { 20 });
	CHECK(row.excluded == std::vector<OutfitRows::Exclusion>{ { .outfit = 2, .armor = { 20 } }, { .outfit = 5, .armor = { 10, 30 } } });
	CHECK(OutfitRows::ExcludedOf(row, 5) == Ids{ 10, 30 });
	CHECK(OutfitRows::ExcludedOf(row, 7).empty());

	OutfitRows::SetExcluded(row, 5, {});  // all its items worn again
	CHECK(OutfitRows::ExcludedOf(row, 5).empty());
	CHECK(row.excluded.size() == 1);

	Row same{ .outfits = { 5, 2 }, .chosen = 5 };
	CHECK(same != row);  // the items left off count in a row's equality
	OutfitRows::SetExcluded(same, 2, { 20 });
	CHECK(same == row);
}

TEST_CASE("Give sets the items the outfit leaves off; a replaced or taken outfit loses its own", "[rows][excluded]")
{
	Row row;
	OutfitRows::Give(row, 1, { 11 }, NoLayer);
	CHECK(OutfitRows::ExcludedOf(row, 1) == Ids{ 11 });

	OutfitRows::Give(row, 1, {}, NoLayer);  // given again with all its items
	CHECK(row.excluded.empty());

	OutfitRows::Give(row, 1, { 11 }, NoLayer);
	OutfitRows::Give(row, 2, { 22 }, NoLayer);  // random off: takes 1's place, which leaves with its items
	CHECK(row.outfits == Ids{ 2 });
	CHECK(OutfitRows::ExcludedOf(row, 1).empty());
	CHECK(OutfitRows::ExcludedOf(row, 2) == Ids{ 22 });

	Row random{ .outfits = { 1, 2 }, .random = true };
	OutfitRows::SetExcluded(random, 1, { 11 });
	OutfitRows::Place(random, 1, 2, NoLayer);  // reordered: keeps its items
	CHECK(OutfitRows::ExcludedOf(random, 1) == Ids{ 11 });
	OutfitRows::Take(random, 1);
	CHECK(random.excluded.empty());
}

TEST_CASE("WithoutGone hides a gone outfit's items and KeepGone brings them back", "[rows][excluded]")
{
	const auto inLibrary = [](std::uint32_t a_outfit) { return a_outfit != 9; };
	Row        stored{ .outfits = { 1, 9 }, .random = true };
	OutfitRows::SetExcluded(stored, 1, { 11 });
	OutfitRows::SetExcluded(stored, 9, { 99 });

	auto shown = OutfitRows::WithoutGone(stored, inLibrary);
	CHECK(shown.outfits == Ids{ 1 });
	CHECK(shown.excluded.size() == 1);

	OutfitRows::SetExcluded(shown, 1, { 12 });  // the edit
	const auto kept = OutfitRows::KeepGone(shown, stored, inLibrary);
	CHECK(kept.outfits == Ids{ 1, 9 });
	CHECK(OutfitRows::ExcludedOf(kept, 1) == Ids{ 12 });
	CHECK(OutfitRows::ExcludedOf(kept, 9) == Ids{ 99 });
}

TEST_CASE("WornPieces leaves out the items left off, or none when that leaves nothing", "[rows][excluded]")
{
	const Ids  pieces{ 10, 20, 30 };
	const auto self = [](std::uint32_t a_armor) { return a_armor; };
	CHECK(OutfitRows::WornPieces(pieces, Ids{ 20 }, self) == Ids{ 10, 30 });
	CHECK(OutfitRows::WornPieces(pieces, Ids{ 40 }, self) == pieces);  // an item no longer in the outfit
	CHECK(OutfitRows::WornPieces(pieces, Ids{ 10, 20, 30 }, self) == pieces);
	CHECK(OutfitRows::WornPieces(pieces, Ids{}, self) == pieces);
}

TEST_CASE("SetChance keeps each outfit's chance sorted, drops the default and caps the value", "[rows][chance]")
{
	Row row{ .outfits = { 5, 2, 7 }, .random = true };
	CHECK(OutfitRows::ChanceOf(row, 5) == OutfitRows::kDefaultChance);
	OutfitRows::SetChance(row, 5, 50);
	OutfitRows::SetChance(row, 2, 0);
	OutfitRows::SetChance(row, 7, 255);  // past the top
	CHECK(row.chances == std::vector<OutfitRows::Chance>{ { .outfit = 2, .value = 0 }, { .outfit = 5, .value = 50 }, { .outfit = 7, .value = OutfitRows::kMaxChance } });
	CHECK(OutfitRows::ChanceOf(row, 2) == 0);

	OutfitRows::SetChance(row, 5, OutfitRows::kDefaultChance);  // back to the default: not stored
	CHECK(row.chances.size() == 2);
	CHECK(OutfitRows::ChanceOf(row, 5) == OutfitRows::kDefaultChance);

	Row same{ .outfits = { 5, 2, 7 }, .random = true };
	CHECK(same != row);  // the chances count in a row's equality
	OutfitRows::SetChance(same, 7, OutfitRows::kMaxChance);
	OutfitRows::SetChance(same, 2, 0);
	CHECK(same == row);
}

TEST_CASE("Pick draws in proportion to the chances, never one at 0 or the one left out", "[rows][chance]")
{
	Row row{ .outfits = { 1, 2, 3 }, .random = true };
	OutfitRows::SetChance(row, 1, 200);
	OutfitRows::SetChance(row, 3, 0);
	std::size_t total = 0;
	std::size_t drawn = 0;
	const auto  draw = [&](std::size_t a_total) {
		total = a_total;
		return drawn;
	};
	drawn = 0;
	CHECK(OutfitRows::Pick(row, 0, draw, Any) == 1);
	CHECK(total == 300);
	drawn = 199;
	CHECK(OutfitRows::Pick(row, 0, draw, Any) == 1);
	drawn = 200;
	CHECK(OutfitRows::Pick(row, 0, draw, Any) == 2);
	drawn = 299;
	CHECK(OutfitRows::Pick(row, 0, draw, Any) == 2);

	drawn = 0;
	CHECK(OutfitRows::Pick(row, 1, draw, Any) == 2);  // the one worn left out
	CHECK(total == 100);
	CHECK(OutfitRows::Pick(row, 2, draw, Any) == 1);  // 3 is at 0: 1 is all that is left
	CHECK(total == 200);
	OutfitRows::SetChance(row, 1, 0);
	CHECK(OutfitRows::Pick(row, 2, NoPick, Any) == 0);  // nothing left to draw from
	CHECK(OutfitRows::Pick(Row{ .random = true }, 0, NoPick, Any) == 0);
}

TEST_CASE("A random row whose outfits are all at 0 is passed over but still held", "[rows][chance]")
{
	Row zero{ .outfits = { 3, 4 }, .random = true };
	OutfitRows::SetChance(zero, 3, 0);
	CHECK(OutfitRows::PicksAny(zero));
	OutfitRows::SetChance(zero, 4, 0);
	CHECK_FALSE(OutfitRows::PicksAny(zero));
	CHECK(OutfitRows::HoldsAny(zero));                        // the window still shows its list
	CHECK(OutfitRows::Wears(zero, 3, NoPick, NoLayer) == 3);  // a pick worn stays on
	CHECK(OutfitRows::Wears(zero, 0, NoPick, NoLayer) == 0);

	CHECK(OutfitRows::PicksAny(Row{ .outfits = { 3 }, .chosen = 3 }));  // random off: chances do not count
	OutfitRows::SetRandom(zero, false, NoLayer);
	CHECK(OutfitRows::PicksAny(zero));

	OutfitRows::Rows rows{};
	rows[OutfitRows::RowOf(Situation::kDefault)] = { .outfits = { 1 }, .chosen = 1 };
	rows[OutfitRows::RowOf(Situation::kTown)] = { .outfits = { 2 }, .chosen = 2 };
	rows[OutfitRows::RowOf(Situation::kCold)] = { .outfits = { 3 }, .random = true };
	OutfitRows::SetChance(rows[OutfitRows::RowOf(Situation::kCold)], 3, 0);
	Situations::Applicable now;
	now.Add(Situation::kCold);
	now.Add(Situation::kTown);
	CHECK(OutfitRows::Resolve(rows, now, NoLayer) == Situation::kTown);
}

TEST_CASE("An outfit keeps its chance on the list and loses it as it leaves", "[rows][chance]")
{
	Row random{ .outfits = { 1, 2 }, .random = true };
	OutfitRows::SetChance(random, 1, 150);
	OutfitRows::Place(random, 1, 2, NoLayer);  // reordered
	CHECK(OutfitRows::ChanceOf(random, 1) == 150);
	OutfitRows::SetRandom(random, false, NoLayer);  // hidden, kept for random on again
	OutfitRows::SetRandom(random, true, NoLayer);
	CHECK(OutfitRows::ChanceOf(random, 1) == 150);
	OutfitRows::Take(random, 1);
	CHECK(random.chances.empty());

	Row off{ .outfits = { 1 }, .chosen = 1 };
	OutfitRows::SetChance(off, 1, 40);
	OutfitRows::Give(off, 2, {}, NoLayer);  // random off: takes 1's place, which leaves with its chance
	CHECK(off.chances.empty());
	CHECK(OutfitRows::ChanceOf(off, 2) == OutfitRows::kDefaultChance);
}

TEST_CASE("WithoutGone hides a gone outfit's chance and KeepGone brings it back", "[rows][chance]")
{
	const auto inLibrary = [](std::uint32_t a_outfit) { return a_outfit != 9; };
	Row        stored{ .outfits = { 1, 9 }, .random = true };
	OutfitRows::SetChance(stored, 1, 20);
	OutfitRows::SetChance(stored, 9, 180);

	auto shown = OutfitRows::WithoutGone(stored, inLibrary);
	CHECK(shown.chances == std::vector<OutfitRows::Chance>{ { .outfit = 1, .value = 20 } });

	OutfitRows::SetChance(shown, 1, 60);  // the edit
	const auto kept = OutfitRows::KeepGone(shown, stored, inLibrary);
	CHECK(OutfitRows::ChanceOf(kept, 1) == 60);
	CHECK(OutfitRows::ChanceOf(kept, 9) == 180);
}

TEST_CASE("ChosenOf reads the base and the layer by what each outfit is now", "[rows][layer]")
{
	CHECK(OutfitRows::ChosenOf(Row{ .outfits = { 1, 101 }, .chosen = 1, .chosenLayer = 101 }, false, IsLayer) == 1);
	CHECK(OutfitRows::ChosenOf(Row{ .outfits = { 1, 101 }, .chosen = 1, .chosenLayer = 101 }, true, IsLayer) == 101);

	const Row turned{ .outfits = { 101 }, .chosen = 101 };  // given as a base, a layer since
	CHECK(OutfitRows::ChosenOf(turned, false, IsLayer) == 0);
	CHECK(OutfitRows::ChosenOf(turned, true, IsLayer) == 101);

	const Row twoLayers{ .outfits = { 101, 102 }, .chosen = 101, .chosenLayer = 102 };  // one is worn
	CHECK(OutfitRows::ChosenOf(twoLayers, true, IsLayer) == 102);
	CHECK(OutfitRows::ChosenOf(twoLayers, false, IsLayer) == 0);
}

TEST_CASE("Give with random off replaces only the outfit of its own kind", "[rows][layer]")
{
	Row row;
	OutfitRows::Give(row, 1, {}, IsLayer);
	OutfitRows::Give(row, 101, {}, IsLayer);  // a layer beside the base
	CHECK(row.outfits == Ids{ 101, 1 });
	CHECK(row.chosen == 1);
	CHECK(row.chosenLayer == 101);

	OutfitRows::Give(row, 102, { 7 }, IsLayer);  // takes the layer's place, which leaves
	CHECK(row.outfits == Ids{ 102, 1 });
	CHECK(row.chosenLayer == 102);
	OutfitRows::Give(row, 2, {}, IsLayer);  // takes the base's place
	CHECK(row.outfits == Ids{ 102, 2 });
	CHECK(row.chosen == 2);
	CHECK(row.chosenLayer == 102);
	CHECK(OutfitRows::ExcludedOf(row, 102) == Ids{ 7 });

	Row turned{ .outfits = { 101, 1 }, .chosen = 101 };  // 101 turned into a layer since
	OutfitRows::Give(turned, 2, {}, IsLayer);            // no base chosen: added, the layer stays
	CHECK(turned.outfits == Ids{ 2, 101, 1 });
	CHECK(turned.chosen == 2);
	CHECK(turned.chosenLayer == 101);

	Row twice{ .outfits = { 101, 102 }, .chosen = 101, .chosenLayer = 102 };
	OutfitRows::Place(twice, 101, 0, IsLayer);  // on the list: the layer worn, never chosen twice
	CHECK(twice.chosenLayer == 101);
	CHECK(twice.chosen == 0);
}

TEST_CASE("SetRandom off keeps or chooses one outfit of each kind", "[rows][layer]")
{
	Row fresh{ .outfits = { 101, 1, 2, 102 }, .random = true };
	OutfitRows::SetRandom(fresh, false, IsLayer);
	CHECK(fresh.chosen == 1);
	CHECK(fresh.chosenLayer == 101);

	Row worn{ .outfits = { 101, 1, 102 }, .random = true, .chosen = 1, .chosenLayer = 102 };
	OutfitRows::SetRandom(worn, false, IsLayer);
	CHECK(worn.chosen == 1);
	CHECK(worn.chosenLayer == 102);

	Row bases{ .outfits = { 1, 2 }, .random = true };
	OutfitRows::SetRandom(bases, false, IsLayer);
	CHECK(bases.chosenLayer == 0);
}

TEST_CASE("Take, WithoutGone and KeepGone treat the chosen layer as chosen", "[rows][layer]")
{
	Row row{ .outfits = { 1, 101 }, .chosen = 1, .chosenLayer = 101 };
	CHECK(OutfitRows::Holds(row, 101));
	CHECK(OutfitRows::HoldsAny(Row{ .outfits = { 101 }, .chosenLayer = 101 }));
	CHECK(OutfitRows::PicksAny(Row{ .outfits = { 101 }, .chosenLayer = 101 }));
	OutfitRows::Take(row, 101);
	CHECK(row.chosenLayer == 0);
	CHECK(row.chosen == 1);

	const auto inLibrary = [](std::uint32_t a_outfit) { return a_outfit != 109; };
	const Row  stored{ .outfits = { 1, 109 }, .chosen = 1, .chosenLayer = 109 };
	const Row  shown = OutfitRows::WithoutGone(stored, inLibrary);
	CHECK(shown.chosenLayer == 0);
	CHECK(OutfitRows::KeepGone(shown, stored, inLibrary) == stored);
}

TEST_CASE("Resolve and ResolveLayer take the base and the layer from rows apart", "[rows][layer]")
{
	OutfitRows::Rows rows{};
	rows[OutfitRows::RowOf(Situation::kDefault)] = { .outfits = { 1 }, .chosen = 1 };
	rows[OutfitRows::RowOf(Situation::kTown)] = { .outfits = { 101 }, .chosenLayer = 101 };  // a layer alone
	rows[OutfitRows::RowOf(Situation::kCold)] = { .outfits = { 2 }, .chosen = 2 };

	Situations::Applicable now;  // Cold outranks Town
	now.Add(Situation::kCold);
	now.Add(Situation::kTown);
	CHECK(OutfitRows::Resolve(rows, now, IsLayer) == Situation::kCold);
	CHECK(OutfitRows::ResolveLayer(rows, now, IsLayer, Loaded) == Situation::kTown);

	Situations::Applicable town;
	town.Add(Situation::kTown);
	CHECK(OutfitRows::Resolve(rows, town, IsLayer) == Situation::kDefault);  // Town has no base
	CHECK(OutfitRows::ResolveLayer(rows, town, IsLayer, Loaded) == Situation::kTown);

	const Situations::Applicable none;
	CHECK_FALSE(OutfitRows::ResolveLayer(rows, none, IsLayer, Loaded).has_value());
	rows[OutfitRows::RowOf(Situation::kDefault)] = { .outfits = { 1, 103 }, .chosen = 1, .chosenLayer = 103 };
	CHECK(OutfitRows::ResolveLayer(rows, none, IsLayer, Loaded) == Situation::kDefault);

	const auto notTown = [](std::uint32_t a_outfit) { return a_outfit != 101; };  // nothing of 101 loaded
	CHECK(OutfitRows::ResolveLayer(rows, town, IsLayer, notTown) == Situation::kDefault);

	rows[OutfitRows::RowOf(Situation::kCold)] = { .outfits = { 2, 104 }, .random = true };
	OutfitRows::SetChance(rows[OutfitRows::RowOf(Situation::kCold)], 104, 0);
	CHECK(OutfitRows::Resolve(rows, now, IsLayer) == Situation::kCold);
	CHECK(OutfitRows::ResolveLayer(rows, now, IsLayer, Loaded) == Situation::kTown);  // Cold's layer at 0 is passed over
}

TEST_CASE("PicksBase and PicksLayer tell a random row's kinds apart", "[rows][layer]")
{
	Row layers{ .outfits = { 101, 102 }, .random = true };
	CHECK(OutfitRows::PicksAny(layers));
	CHECK_FALSE(OutfitRows::PicksBase(layers, IsLayer));
	CHECK(OutfitRows::PicksLayer(layers, IsLayer, Loaded));

	Row mixed{ .outfits = { 1, 101 }, .random = true };
	OutfitRows::SetChance(mixed, 1, 0);
	CHECK_FALSE(OutfitRows::PicksBase(mixed, IsLayer));
	CHECK(OutfitRows::PicksLayer(mixed, IsLayer, Loaded));
	CHECK_FALSE(OutfitRows::PicksLayer(mixed, IsLayer, [](std::uint32_t) { return false; }));
}

TEST_CASE("DrawLayer may draw no layer, at Π(1 − c/100), and else picks by the chances", "[rows][layer]")
{
	Row one{ .outfits = { 1, 101 }, .random = true };  // the base is no candidate
	OutfitRows::SetChance(one, 101, 70);
	Draws worn{ .values = { 69, 0 } };
	CHECK(OutfitRows::DrawLayer(one, worn, IsLayer) == 101);
	CHECK(worn.asked == std::vector<std::size_t>{ 100, 70 });
	Draws none{ .values = { 70 } };
	CHECK(OutfitRows::DrawLayer(one, none, IsLayer) == 0);

	Row two{ .outfits = { 101, 102 }, .random = true };
	OutfitRows::SetChance(two, 101, 50);
	OutfitRows::SetChance(two, 102, 50);
	Draws second{ .values = { 60, 40, 60 } };  // the first trial fails, the second wins: then the ratio
	CHECK(OutfitRows::DrawLayer(two, second, IsLayer) == 102);
	CHECK(second.asked == std::vector<std::size_t>{ 100, 100, 100 });
	Draws neither{ .values = { 50, 99 } };
	CHECK(OutfitRows::DrawLayer(two, neither, IsLayer) == 0);

	Row sure{ .outfits = { 101, 102 }, .random = true };  // at 100 a layer is always worn: no trial
	OutfitRows::SetChance(sure, 102, 30);
	Draws ratio{ .values = { 110 } };
	CHECK(OutfitRows::DrawLayer(sure, ratio, IsLayer) == 102);
	CHECK(ratio.asked == std::vector<std::size_t>{ 130 });

	Row zero{ .outfits = { 101 }, .random = true };
	OutfitRows::SetChance(zero, 101, 0);
	CHECK(OutfitRows::DrawLayer(zero, NoPick, IsLayer) == 0);
}

TEST_CASE("WearsLayer keeps a draw of none or a kept layer while the row stays the layer row", "[rows][layer]")
{
	Row row{ .outfits = { 1, 101, 102 }, .random = true };
	CHECK(OutfitRows::WearsLayer(row, 0u, NoPick, IsLayer, Loaded) == 0);      // none kept
	CHECK(OutfitRows::WearsLayer(row, 102u, NoPick, IsLayer, Loaded) == 102);  // kept
	Draws again{ .values = { 50 } };                                           // 101 and 102 at 100: no trial
	CHECK(OutfitRows::WearsLayer(row, std::nullopt, again, IsLayer, Loaded) == 101);
	CHECK(again.asked == std::vector<std::size_t>{ 200 });
	const auto only101 = [](std::uint32_t a_outfit) { return a_outfit == 101; };
	Draws      unloaded{ .values = { 0 } };  // 102 has nothing loaded: drawn anew without it
	CHECK(OutfitRows::WearsLayer(row, 102u, unloaded, IsLayer, only101) == 101);
	Draws base{ .values = { 0 } };
	CHECK(OutfitRows::WearsLayer(row, 1u, base, IsLayer, only101) == 101);  // a base is no kept layer

	const Row off{ .outfits = { 1, 101 }, .chosen = 1, .chosenLayer = 101 };
	CHECK(OutfitRows::WearsLayer(off, std::nullopt, NoPick, IsLayer, Loaded) == 101);
	CHECK(OutfitRows::WearsLayer(off, std::nullopt, NoPick, IsLayer, [](std::uint32_t) { return false; }) == 0);
}

TEST_CASE("Wears picks the base among the bases only", "[rows][layer]")
{
	const Row row{ .outfits = { 101, 1 }, .random = true };
	Draws     draws{ .values = { 99 } };
	CHECK(OutfitRows::Wears(row, 101, draws, IsLayer) == 1);  // a layer is no kept base
	CHECK(draws.asked == std::vector<std::size_t>{ 100 });
	CHECK(OutfitRows::Wears(Row{ .outfits = { 101 }, .chosen = 101 }, 0, NoPick, IsLayer) == 0);
}

TEST_CASE("Uncovered leaves out the base pieces sharing a body slot with a layer piece", "[rows][layer]")
{
	const auto slots = [](std::uint32_t a_mask) { return a_mask; };  // each piece is its slot mask
	const Ids  base{ 0b0001, 0b0110, 0b1000 };
	CHECK(OutfitRows::Uncovered(base, Ids{ 0b0100 }, slots) == Ids{ 0b0001, 0b1000 });
	CHECK(OutfitRows::Uncovered(base, Ids{}, slots) == base);
	CHECK(OutfitRows::Uncovered(base, Ids{ 0b1111 }, slots).empty());
}
