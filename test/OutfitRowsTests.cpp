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
