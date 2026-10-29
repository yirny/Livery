#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
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
}

TEST_CASE("Give with random off replaces the chosen outfit in its place", "[rows]")
{
	Row row;
	OutfitRows::Give(row, 1);  // an empty list: added
	CHECK(row.outfits == Ids{ 1 });
	CHECK(row.chosen == 1);

	OutfitRows::Give(row, 2);  // takes the chosen one's place, which leaves
	CHECK(row.outfits == Ids{ 2 });
	CHECK(row.chosen == 2);

	Row hidden{ .outfits = { 3, 1, 2 }, .chosen = 1 };  // others on the list from random
	OutfitRows::Give(hidden, 4);
	CHECK(hidden.outfits == Ids{ 3, 4, 2 });
	CHECK(hidden.chosen == 4);

	OutfitRows::Give(hidden, 2);  // on the list already: chosen where it is
	CHECK(hidden.outfits == Ids{ 3, 4, 2 });
	CHECK(hidden.chosen == 2);

	Row none{ .outfits = { 1, 2 } };  // nothing chosen: added at the front
	OutfitRows::Give(none, 3);
	CHECK(none.outfits == Ids{ 3, 1, 2 });
	CHECK(none.chosen == 3);

	OutfitRows::Give(none, 0);  // not an outfit
	CHECK(none.outfits == Ids{ 3, 1, 2 });
	CHECK(none.chosen == 3);
}

TEST_CASE("Give with random on adds to the list and leaves chosen", "[rows]")
{
	Row row{ .outfits = { 1 }, .random = true, .chosen = 1 };
	OutfitRows::Give(row, 2);
	CHECK(row.outfits == Ids{ 2, 1 });
	CHECK(row.chosen == 1);
}

TEST_CASE("Place with random on puts the outfit at the drop place, moving it when on the list", "[rows]")
{
	Row row{ .outfits = { 1, 2, 3 }, .random = true };
	OutfitRows::Place(row, 1, 2);  // between 2 and 3
	CHECK(row.outfits == Ids{ 2, 1, 3 });
	OutfitRows::Place(row, 3, 0);
	CHECK(row.outfits == Ids{ 3, 2, 1 });
	OutfitRows::Place(row, 3, 3);  // after the last
	CHECK(row.outfits == Ids{ 2, 1, 3 });
	OutfitRows::Place(row, 4, 1);  // not on the list yet
	CHECK(row.outfits == Ids{ 2, 4, 1, 3 });
	OutfitRows::Place(row, 5, 9);  // past the end: last
	CHECK(row.outfits == Ids{ 2, 4, 1, 3, 5 });
	CHECK(row.chosen == 0);  // random on leaves chosen

	Row off{ .outfits = { 1, 2 } };  // random off with nothing chosen: the place counts
	OutfitRows::Place(off, 3, 1);
	CHECK(off.outfits == Ids{ 1, 3, 2 });
	CHECK(off.chosen == 3);
	OutfitRows::Place(off, 0, 0);  // not an outfit
	CHECK(off.outfits == Ids{ 1, 3, 2 });
}

TEST_CASE("SetRandom off keeps the last pick, or chooses the first with none", "[rows]")
{
	Row row{ .outfits = { 1, 2 }, .random = true, .chosen = 2 };  // the pick worn last
	OutfitRows::SetRandom(row, false);
	CHECK_FALSE(row.random);
	CHECK(row.chosen == 2);

	Row fresh{ .outfits = { 3, 4 }, .random = true };  // never worn
	OutfitRows::SetRandom(fresh, false);
	CHECK(fresh.chosen == 3);

	Row empty{ .random = true };
	OutfitRows::SetRandom(empty, false);
	CHECK(empty.chosen == 0);

	OutfitRows::SetRandom(row, true);
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
	OutfitRows::Give(edited, 2);  // an outfit dropped where the gone one was chosen
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
	CHECK(OutfitRows::Wears(Row{ .outfits = { 2, 1 }, .chosen = 1 }, 2, NoPick) == 1);
	CHECK(OutfitRows::Wears(Row{ .outfits = { 1 } }, 1, NoPick) == 0);

	const Row random{ .outfits = { 3, 2, 1 }, .random = true };
	CHECK(OutfitRows::Wears(random, 2, NoPick) == 2);  // kept while on the list
	std::size_t asked = 0;
	const auto  pickLast = [&asked](std::size_t a_count) {
		asked = a_count;
		return a_count - 1;
	};
	CHECK(OutfitRows::Wears(random, 9, pickLast) == 1);  // gone from the list: picks again
	CHECK(asked == 3);
	CHECK(OutfitRows::Wears(random, 0, pickLast) == 1);
	CHECK(OutfitRows::Wears(Row{ .random = true }, 0, NoPick) == 0);
}

TEST_CASE("Resolve takes the first situation whose row holds an outfit", "[rows]")
{
	OutfitRows::Rows rows{};
	rows[OutfitRows::RowOf(Situation::kDefault)] = { .outfits = { 1 }, .chosen = 1 };
	rows[OutfitRows::RowOf(Situation::kTown)] = { .outfits = { 2 }, .chosen = 2 };
	rows[OutfitRows::RowOf(Situation::kWarm)] = { .outfits = { 3 } };  // nothing chosen

	Situations::Applicable now;
	CHECK(OutfitRows::Resolve(rows, now) == Situation::kDefault);
	now.Add(Situation::kWarm);
	now.Add(Situation::kTown);
	CHECK(OutfitRows::Resolve(rows, now) == Situation::kTown);

	rows[OutfitRows::RowOf(Situation::kWarm)].random = true;
	CHECK(OutfitRows::Resolve(rows, now) == Situation::kWarm);

	Situations::Applicable home;
	home.Add(Situation::kHome);
	CHECK(OutfitRows::Resolve(rows, home) == Situation::kDefault);
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
	OutfitRows::Give(row, 1, { 11 });
	CHECK(OutfitRows::ExcludedOf(row, 1) == Ids{ 11 });

	OutfitRows::Give(row, 1);  // given again with all its items
	CHECK(row.excluded.empty());

	OutfitRows::Give(row, 1, { 11 });
	OutfitRows::Give(row, 2, { 22 });  // random off: takes 1's place, which leaves with its items
	CHECK(row.outfits == Ids{ 2 });
	CHECK(OutfitRows::ExcludedOf(row, 1).empty());
	CHECK(OutfitRows::ExcludedOf(row, 2) == Ids{ 22 });

	Row random{ .outfits = { 1, 2 }, .random = true };
	OutfitRows::SetExcluded(random, 1, { 11 });
	OutfitRows::Place(random, 1, 2);  // reordered: keeps its items
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
