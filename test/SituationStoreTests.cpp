#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "SituationTable.h"

namespace
{
	using Situations::Situation;
	using SituationTable::IndexOf;

	using Lines = std::vector<std::string>;

	SituationTable::Entry& At(SituationTable::Table& a_table, Situation a_situation)
	{
		return a_table[IndexOf(a_situation)];
	}
}

TEST_CASE("Presets are named by their keys", "[situations]")
{
	auto table = SituationTable::Presets();
	CHECK(At(table, Situation::kDefault).name == "Default");
	CHECK(At(table, Situation::kTown).name == "Town");
	CHECK(At(table, Situation::kWarm).name == "Warm");
	for (const auto& entry : table) {
		CHECK_FALSE(entry.removed);
	}

	CHECK_FALSE(SituationTable::CheckNames(table));
	CHECK(SituationTable::RestoreChanges(table).empty());
}

TEST_CASE("Names need text, are unique among the situations not removed, and the Default keeps its own", "[situations]")
{
	auto table = SituationTable::Presets();
	At(table, Situation::kHome).name = "  ";
	CHECK(SituationTable::CheckNames(table));

	table = SituationTable::Presets();
	At(table, Situation::kHome).name = "town";  // ASCII case ignored
	CHECK(SituationTable::CheckNames(table));
	At(table, Situation::kTown).removed = true;  // a removed one's name is free to take
	CHECK_FALSE(SituationTable::CheckNames(table));

	table = SituationTable::Presets();
	At(table, Situation::kWarm).name = "DEFAULT";
	CHECK(SituationTable::CheckNames(table));

	table = SituationTable::Presets();
	At(table, Situation::kDefault).name = "Everyday";
	CHECK(SituationTable::CheckNames(table));

	table = SituationTable::Presets();
	At(table, Situation::kDefault).removed = true;
	CHECK(SituationTable::CheckNames(table));
}

TEST_CASE("Parse replaces the presets' fields an entry has and ignores the others", "[situations]")
{
	SituationTable::Table table;
	const auto            problem = SituationTable::Parse(R"({ "version": 1, "situations": [
		{ "key": "Town", "name": "City", "description": "in the city", "removed": false },
		{ "key": "Home", "removed": true },
		{ "key": "Default", "description": 3 } ] })",
		table);
	REQUIRE_FALSE(problem);
	CHECK(At(table, Situation::kTown) == SituationTable::Entry{ .name = "City" });
	CHECK(At(table, Situation::kHome) == SituationTable::Entry{ .name = "Home", .removed = true });
	CHECK(At(table, Situation::kSleep) == SituationTable::Preset(Situation::kSleep));
	CHECK(At(table, Situation::kDefault) == SituationTable::Preset(Situation::kDefault));

	REQUIRE_FALSE(SituationTable::Parse(R"({ "version": 1, "situations": [] })", table));
	CHECK(table == SituationTable::Presets());
}

TEST_CASE("Parse reads an empty file as no file: the presets", "[situations]")
{
	SituationTable::Table table;
	REQUIRE_FALSE(SituationTable::Parse("", table));
	CHECK(table == SituationTable::Presets());
	REQUIRE_FALSE(SituationTable::Parse(" \r\n\t\n", table));
	CHECK(table == SituationTable::Presets());
}

TEST_CASE("Parse refuses the whole file for an unknown key and leaves the presets", "[situations]")
{
	SituationTable::Table table;
	CHECK(SituationTable::Parse(R"({ "version": 1, "situations": [ { "key": "Town", "name": "City" }, { "key": "Tavern", "name": "Inn" } ] })", table));
	CHECK(table == SituationTable::Presets());

	CHECK(SituationTable::Parse(R"({ "version": 1, "situations": [ { "name": "City" } ] })", table));
	CHECK(SituationTable::Parse(R"({ "version": 1, "situations": [ { "key": "Town" }, { "key": "Town" } ] })", table));
	CHECK(SituationTable::Parse(R"({ "version": 1, "situations": [ { "key": "Town", "name": 3 } ] })", table));
	CHECK(SituationTable::Parse(R"({ "version": 1, "situations": [ { "key": "Town", "removed": "yes" } ] })", table));
	CHECK(SituationTable::Parse(R"({ "version": 1, "situations": [ { "key": "Home", "name": "Town" } ] })", table));
	CHECK(SituationTable::Parse(R"({ "version": 2, "situations": [] })", table));  // a newer version
	CHECK(SituationTable::Parse(R"({ "version": 1 })", table));
	CHECK(SituationTable::Parse("not json", table));
	CHECK(table == SituationTable::Presets());
}

TEST_CASE("Serialize writes only what differs from the presets, and Parse reads it back", "[situations]")
{
	auto                  table = SituationTable::Presets();
	SituationTable::Table read;
	REQUIRE_FALSE(SituationTable::Parse(SituationTable::Serialize(table), read));
	CHECK(read == table);
	CHECK(SituationTable::Serialize(table).find("Town") == std::string::npos);

	At(table, Situation::kWarm).name = "Cold";
	At(table, Situation::kSwimming).removed = true;
	const auto text = SituationTable::Serialize(table);
	CHECK(text.find("\"Warm\"") != std::string::npos);
	CHECK(text.find("\"Swimming\"") != std::string::npos);
	CHECK(text.find("\"Town\"") == std::string::npos);
	CHECK(text.find("description") == std::string::npos);
	REQUIRE_FALSE(SituationTable::Parse(text, read));
	CHECK(read == table);
}

TEST_CASE("RestoreChanges names each situation that differs, under its name now", "[situations]")
{
	auto table = SituationTable::Presets();
	At(table, Situation::kTown).removed = true;
	At(table, Situation::kHome).name = "Cottage";
	At(table, Situation::kWarm).name = "Cold";
	CHECK(SituationTable::RestoreChanges(table) == Lines{ "Town: restored", "Cottage: name reset", "Cold: name reset" });
}
