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

TEST_CASE("Give puts the outfit at the front and makes it chosen with random off", "[rows]")
{
	Row row;
	OutfitRows::Give(row, 1);
	OutfitRows::Give(row, 2);
	CHECK(row.outfits == Ids{ 2, 1 });
	CHECK(row.chosen == 2);

	OutfitRows::Give(row, 1);  // already on the list: moves to the front
	CHECK(row.outfits == Ids{ 1, 2 });
	CHECK(row.chosen == 1);

	OutfitRows::Give(row, 0);  // not an outfit
	CHECK(row.outfits == Ids{ 1, 2 });
	CHECK(row.chosen == 1);
}

TEST_CASE("Give with random on adds to the list and leaves chosen", "[rows]")
{
	Row row{ .outfits = { 1 }, .random = true, .chosen = 1 };
	OutfitRows::Give(row, 2);
	CHECK(row.outfits == Ids{ 2, 1 });
	CHECK(row.chosen == 1);
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
