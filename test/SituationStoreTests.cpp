#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

#include "SituationRules.h"
#include "SituationTable.h"
#include "Strings.h"

namespace
{
	using Situations::Situation;
	using SituationTable::IndexOf;

	using Lines = std::vector<std::string>;

	SituationTable::Entry& At(SituationTable::Table& a_table, Situation a_situation)
	{
		return a_table[IndexOf(a_situation)];
	}

	// A language that words the presets and the name clash its own way; the rest English.
	const char* Dutch(Strings::Text a_text)
	{
		switch (a_text) {
		case Strings::Text::kPresetTown:
			return "Stad";
		case Strings::Text::kPresetHome:
			return "Thuis";
		case Strings::Text::kTwoSituationsNamed:
			return "Twee: {0}";
		default:
			return Strings::English(a_text);
		}
	}
}

TEST_CASE("Presets are named by their keys", "[situations]")
{
	auto table = SituationTable::Presets();
	CHECK(At(table, Situation::kDefault).name == "Default");
	CHECK(At(table, Situation::kTown).name == "Town");
	CHECK(At(table, Situation::kCold).name == "Cold");
	for (const auto& entry : table) {
		CHECK_FALSE(entry.removed);
	}

	CHECK_FALSE(SituationTable::CheckNames(table));
	CHECK(SituationTable::RestoreChanges(table).empty());
}

TEST_CASE("The presets' texts are their keys in English", "[situations]")
{
	for (std::size_t i = 0; i < Situations::kPresetCount; ++i) {
		CHECK(std::string(Strings::English(SituationTable::kPresetNames[i])) == Situations::Name(static_cast<Situation>(i)));
	}
}

TEST_CASE("A preset named as installed is shown in the language and stored as its key", "[situations]")
{
	using SituationTable::DisplayName;
	using SituationTable::StoredName;

	auto       table = SituationTable::Presets();
	const auto town = IndexOf(Situation::kTown);
	const auto home = IndexOf(Situation::kHome);
	CHECK(DisplayName(table[town], town, Dutch) == "Stad");
	CHECK(DisplayName(table[town], town) == "Town");
	CHECK(StoredName(town, "Stad", Dutch) == "Town");
	CHECK(StoredName(home, "Stad", Dutch) == "Stad");  // another preset's name is a name like any

	table[town].name = "City";  // renamed: shown and stored as entered
	CHECK(DisplayName(table[town], town, Dutch) == "City");
	CHECK(StoredName(town, "City", Dutch) == "City");

	table[home].name = "Town";  // the key of another preset is no key here
	CHECK(DisplayName(table[home], home, Dutch) == "Town");

	const auto user = table.size();
	SituationTable::AddUserEntry(table, "Home", {});
	CHECK(DisplayName(table[user], user, Dutch) == "Home");
	CHECK(StoredName(user, "Thuis", Dutch) == "Thuis");
}

TEST_CASE("Names clash as shown too, and the reason is in the language", "[situations]")
{
	auto table = SituationTable::Presets();
	SituationTable::AddUserEntry(table, "stad", {});  // shown beside the Town preset as "Stad"
	CHECK_FALSE(SituationTable::CheckNames(table, Strings::English));
	CHECK(SituationTable::CheckNames(table, Dutch) == std::optional<std::string>{ "Twee: stad" });
	At(table, Situation::kTown).removed = true;
	CHECK_FALSE(SituationTable::CheckNames(table, Dutch));

	table = SituationTable::Presets();
	At(table, Situation::kHome).name = "town";
	CHECK(SituationTable::CheckNames(table) == std::optional<std::string>{ "Two situations are named \"town\"." });
	CHECK(SituationTable::CheckNames(table, Dutch) == std::optional<std::string>{ "Twee: town" });  // as the file keeps them
}

TEST_CASE("Names need text, are unique among the situations not removed, and the Default is never removed", "[situations]")
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
	At(table, Situation::kCold).name = "DEFAULT";
	CHECK(SituationTable::CheckNames(table));

	table = SituationTable::Presets();
	At(table, Situation::kDefault).name = "Everyday";  // the Default may be renamed
	CHECK_FALSE(SituationTable::CheckNames(table));
	At(table, Situation::kCold).name = "Default";  // its old name is free to take
	CHECK_FALSE(SituationTable::CheckNames(table));
	At(table, Situation::kDefault).name = "town";
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
	CHECK(At(table, Situation::kTown) == SituationTable::Entry{ .name = "City", .rules = SituationTable::PresetRules(Situation::kTown) });
	CHECK(At(table, Situation::kHome) == SituationTable::Entry{ .name = "Home", .removed = true, .rules = SituationTable::PresetRules(Situation::kHome) });
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
	CHECK(SituationTable::Parse(R"({ "version": 3, "situations": [] })", table));  // a newer version
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

	At(table, Situation::kCold).name = "Chill";
	At(table, Situation::kCastle).removed = true;
	const auto text = SituationTable::Serialize(table);
	CHECK(text.find("\"Cold\"") != std::string::npos);
	CHECK(text.find("\"Castle\"") != std::string::npos);
	CHECK(text.find("\"Town\"") == std::string::npos);
	CHECK(text.find("description") == std::string::npos);
	REQUIRE_FALSE(SituationTable::Parse(text, read));
	CHECK(read == table);
}

TEST_CASE("Keys name each situation once", "[situations]")
{
	for (std::size_t i = 0; i < Situations::Count(); ++i) {
		const auto situation = static_cast<Situation>(i);
		CHECK(Situations::FromKey(Situations::Name(situation)) == situation);
	}
	CHECK_FALSE(Situations::FromKey("Tavern"));
}

TEST_CASE("The registry holds the presets in the installed order, the lowest winning", "[situations]")
{
	CHECK(Situations::Count() == 6);
	CHECK(Situations::Count() <= Situations::kMaxSituations);
	CHECK(std::ranges::equal(Situations::Order(), std::array{ Situation::kTown, Situation::kCastle, Situation::kHome, Situation::kCold, Situation::kSleep }));
	CHECK(std::string(Situations::Name(Situation::kDefault)) == "Default");
	CHECK(SituationTable::Presets().size() == Situations::Count());
	CHECK(std::ranges::equal(SituationTable::Presets().order, Situations::Order()));
}

TEST_CASE("Reorder takes every situation but the Default once, or nothing", "[situations]")
{
	const std::array before{ Situation::kTown, Situation::kCastle, Situation::kHome, Situation::kCold, Situation::kSleep };
	CHECK_FALSE(Situations::Reorder(std::array{ Situation::kTown, Situation::kCastle }));
	CHECK_FALSE(Situations::Reorder(std::array{ Situation::kTown, Situation::kTown, Situation::kHome, Situation::kCold, Situation::kSleep }));
	CHECK_FALSE(Situations::Reorder(std::array{ Situation::kDefault, Situation::kCastle, Situation::kHome, Situation::kCold, Situation::kSleep }));
	CHECK(std::ranges::equal(Situations::Order(), before));
	const std::array after{ Situation::kSleep, Situation::kTown, Situation::kCastle, Situation::kHome, Situation::kCold };
	CHECK(Situations::Reorder(after));
	CHECK(std::ranges::equal(Situations::Order(), after));
	CHECK(Situations::Reorder(before));
}

TEST_CASE("The presets are installed with their rules, Allow during combat off", "[situations]")
{
	using SituationRules::Kind;
	const auto cold = SituationTable::PresetRules(Situation::kCold);
	REQUIRE(cold.rows.size() == 1);
	CHECK(cold.rows[0].kind == Kind::kWeather);
	CHECK(cold.rows[0].values == (SituationRules::kSnow | SituationRules::kFreezingRegion));
	CHECK(SituationRules::Text(cold) == "In snow or a freezing region.");
	CHECK(SituationRules::Text(SituationTable::PresetRules(Situation::kTown)) == "In a town.");
	CHECK(SituationRules::Text(SituationTable::PresetRules(Situation::kHome)) == "In a player home.");
	CHECK(SituationRules::Text(SituationTable::PresetRules(Situation::kSleep)) == "Sleeping.");
	CHECK(SituationTable::PresetRules(Situation::kDefault).rows.empty());
	for (const auto situation : Situations::kInstalledOrder) {
		CHECK_FALSE(SituationRules::AppliesInCombat(SituationTable::PresetRules(situation)));
		CHECK_FALSE(SituationRules::NeverApplies(SituationTable::PresetRules(situation)));
	}
}

TEST_CASE("RestoreChanges names each situation that differs, under its name now", "[situations]")
{
	auto table = SituationTable::Presets();
	At(table, Situation::kTown).removed = true;
	At(table, Situation::kHome).name = "Cottage";
	At(table, Situation::kCold).name = "Chill";
	At(table, Situation::kDefault).name = "Everyday";
	CHECK(SituationTable::RestoreChanges(table) == Lines{ "Everyday: name reset", "Town: restored", "Cottage: name reset", "Chill: name reset" });

	At(table, Situation::kCold).rules.applyInCombat = true;
	At(table, Situation::kCastle).rules.rows.clear();
	CHECK(SituationTable::RestoreChanges(table) ==
		  Lines{ "Everyday: name reset", "Town: restored", "Castle: conditions reset", "Cottage: name reset", "Chill: name and conditions reset" });
}

namespace
{
	using SituationRules::Kind;
	using SituationRules::Row;
	using SituationRules::Rules;

	// A user's situation's entry, as SituationStore::Create and Parse add it.
	SituationTable::Entry User(std::uint32_t a_number, std::string a_name, Rules a_rules = {})
	{
		return { .name = std::move(a_name), .key = SituationTable::UserKey(a_number), .rules = std::move(a_rules) };
	}

	// Adds a user's situation last, in the table and its order, as SituationStore::Create does.
	void AddUserTo(SituationTable::Table& a_table, SituationTable::Entry a_entry)
	{
		a_table.order.push_back(static_cast<Situation>(a_table.size()));
		a_table.entries.push_back(std::move(a_entry));
	}

	const Rules kRainyTown{ .rows = { { .kind = Kind::kLocationType, .values = SituationRules::kTown | SituationRules::kCastle },
								{ .kind = Kind::kWeather, .values = SituationRules::kRain | SituationRules::kSnow },
								{ .kind = Kind::kIndoors, .negated = true },
								{ .kind = Kind::kTimeOfDay, .from = 20 * 60, .to = 6 * 60 } } };
}

TEST_CASE("The Applies line has a phrase per row, not first", "[situations]")
{
	CHECK(SituationRules::Text(kRainyTown) == "In a town or a castle, in rain or snow, outdoors, from 8 PM to 6 AM.");
	CHECK(SituationRules::Text({ .rows = { { .kind = Kind::kWeather, .negated = true, .values = SituationRules::kRain } } }) == "Not in rain.");
	CHECK(SituationRules::Text({}) == "Never applies.");
	CHECK(SituationRules::Text({ .rows = { { .kind = Kind::kWeather } } }) == "Weather (nothing selected).");
	CHECK(SituationRules::Text({ .rows = { { .kind = Kind::kInCombat, .negated = true }, { .kind = Kind::kWater, .values = SituationRules::kSwimming } } }) ==
		  "Not in combat, swimming.");
	CHECK(SituationRules::Text({ .rows = { { .kind = Kind::kWeapon }, { .kind = Kind::kWeapon, .negated = true } } }) == "With a weapon drawn, with weapons sheathed.");
	CHECK(SituationRules::Text({ .rows = { { .kind = Kind::kLocationType,
									 .values = SituationRules::kTown | SituationRules::kPlayerHome | SituationRules::kDungeon } } }) ==
		  "In a town, a player home or a dungeon.");
	CHECK(SituationRules::Text({ .rows = { { .kind = Kind::kTimeOfDay, .from = 9 * 60 + 30, .to = 12 * 60 } } }) == "From 9:30 AM to 12 PM.");
	CHECK(SituationRules::Text({ .rows = { { .kind = Kind::kTimeOfDay, .from = 0, .to = 0 } } }) == "At any hour.");
}

TEST_CASE("Time of day wraps midnight when To is before From", "[situations]")
{
	using SituationRules::InTimeRange;
	CHECK(InTimeRange(20 * 60, 6 * 60, 23 * 60));
	CHECK(InTimeRange(20 * 60, 6 * 60, 0));
	CHECK(InTimeRange(20 * 60, 6 * 60, 20 * 60));
	CHECK_FALSE(InTimeRange(20 * 60, 6 * 60, 6 * 60));
	CHECK_FALSE(InTimeRange(20 * 60, 6 * 60, 12 * 60));
	CHECK(InTimeRange(9 * 60, 17 * 60, 9 * 60));
	CHECK_FALSE(InTimeRange(9 * 60, 17 * 60, 17 * 60));
	CHECK(InTimeRange(7 * 60, 7 * 60, 3 * 60));  // the whole day
	CHECK(SituationRules::TimeText(0) == "12 AM");
	CHECK(SituationRules::TimeText(12 * 60 + 30) == "12:30 PM");
}

TEST_CASE("A row reading the player says so in the Applies line", "[situations]")
{
	using SituationRules::Target;
	const Row you{ .kind = Kind::kInCombat, .target = Target::kPlayer };
	const Row notYou{ .kind = Kind::kSleeping, .negated = true, .target = Target::kPlayer };
	const Row yourWeapon{ .kind = Kind::kWeapon, .target = Target::kPlayer };
	const Row yourSheathed{ .kind = Kind::kWeapon, .negated = true, .target = Target::kPlayer };
	CHECK(SituationRules::Text({ .rows = { you, notYou } }) == "While you are in combat, while you are not sleeping.");
	CHECK(SituationRules::Text({ .rows = { yourWeapon, yourSheathed } }) == "While your weapon is drawn, while your weapons are sheathed.");
	const Row spell{ .kind = Kind::kSpell, .target = Target::kPlayer, .spells = { { .ref = { "Skyrim.esm", 0x059B7A, "Stoneflesh" } } } };
	CHECK(SituationRules::Text({ .rows = { spell } }) == "While Stoneflesh is on you.");
	const Row notYourWater{ .kind = Kind::kWater, .negated = true, .target = Target::kPlayer, .values = SituationRules::kInWater };
	CHECK(SituationRules::Text({ .rows = { notYourWater } }) == "While you are not in water.");
	// A kind that reads no actor's state ignores the target.
	CHECK(SituationRules::Text({ .rows = { { .kind = Kind::kIndoors, .target = Target::kPlayer } } }) == "Indoors.");
	for (const auto& info : SituationRules::kKinds) {
		const bool reads = info.kind == Kind::kInCombat || info.kind == Kind::kSleeping || info.kind == Kind::kWater || info.kind == Kind::kSpell ||
		                   info.kind == Kind::kWeapon;
		CHECK(SituationRules::Targetable(info.kind) == reads);
	}
}

TEST_CASE("A row's target is written only for the player and refused where a kind has none", "[situations]")
{
	using SituationRules::Target;
	auto table = SituationTable::Presets();
	table.nextKey = 2;
	AddUserTo(table, User(1, "Your fight", { .rows = { { .kind = Kind::kInCombat, .target = Target::kPlayer }, { .kind = Kind::kWeapon } } }));
	const auto text = SituationTable::Serialize(table);
	CHECK(text.find("\"target\": \"player\"") != std::string::npos);
	CHECK(text.find("\"wearer\"") == std::string::npos);
	SituationTable::Table read;
	REQUIRE_FALSE(SituationTable::Parse(text, read));
	CHECK(read == table);

	REQUIRE_FALSE(SituationTable::Parse(
		R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A", "conditions": [ { "kind": "water", "target": "wearer", "values": ["swimming"] } ] } ] })", read));
	CHECK(read[Situations::kPresetCount].rules.rows[0].target == Target::kWearer);
	const auto refuses = [&read](const char* a_text) {
		const bool refused = SituationTable::Parse(a_text, read).has_value();
		return refused && read == SituationTable::Presets();
	};
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A", "conditions": [ { "kind": "indoors", "target": "player" } ] } ] })"));
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A", "conditions": [ { "kind": "inCombat", "target": "horse" } ] } ] })"));
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A", "conditions": [ { "kind": "inCombat", "target": 1 } ] } ] })"));
}

TEST_CASE("An In combat row makes a situation apply in combat", "[situations]")
{
	CHECK_FALSE(SituationRules::AppliesInCombat({}));
	CHECK(SituationRules::AppliesInCombat({ .applyInCombat = true }));
	CHECK(SituationRules::AppliesInCombat({ .rows = { { .kind = Kind::kInCombat } } }));
	CHECK_FALSE(SituationRules::AppliesInCombat({ .rows = { { .kind = Kind::kInCombat, .negated = true } } }));
}

TEST_CASE("User keys are user: and a number from 1", "[situations]")
{
	CHECK(SituationTable::UserKey(3) == "user:3");
	CHECK(SituationTable::UserKeyNumber("user:12") == 12u);
	CHECK_FALSE(SituationTable::UserKeyNumber("user:0"));
	CHECK_FALSE(SituationTable::UserKeyNumber("user:"));
	CHECK_FALSE(SituationTable::UserKeyNumber("user:3a"));
	CHECK_FALSE(SituationTable::UserKeyNumber("Town"));
}

TEST_CASE("The co-save keeps an unknown user's situation's row only while situations.json cannot be read", "[situations]")
{
	CHECK(SituationTable::KeepsUnknownRow("user:3", false));
	CHECK_FALSE(SituationTable::KeepsUnknownRow("user:3", true));
	CHECK_FALSE(SituationTable::KeepsUnknownRow("Warm", false));  // a retired preset
	CHECK_FALSE(SituationTable::KeepsUnknownRow("user:0", false));
}

TEST_CASE("A user's situation is added under the next key, last or right after its source", "[situations]")
{
	auto table = SituationTable::Presets();
	table.nextKey = 3;
	const auto first = static_cast<Situation>(Situations::kPresetCount);
	CHECK(SituationTable::AddUserEntry(table, "Night", {}) == "user:3");
	CHECK(table.order.back() == first);

	CHECK(SituationTable::AddUserEntry(table, "Town - Copy", SituationTable::PresetRules(Situation::kTown), Situation::kTown) == "user:4");
	const auto copy = static_cast<Situation>(Situations::kPresetCount + 1);
	REQUIRE(table.size() == Situations::kPresetCount + 2);
	CHECK(table[IndexOf(copy)].rules == SituationTable::PresetRules(Situation::kTown));
	CHECK(table.nextKey == 5);
	const auto town = std::ranges::find(table.order, Situation::kTown);
	REQUIRE(town + 1 != table.order.end());
	CHECK(*(town + 1) == copy);
	CHECK(table.order.back() == first);
	CHECK(table.order.size() + 1 == table.size());
}

TEST_CASE("Serialize writes version 1 until a user's situation is made, then version 2, and Parse reads it back", "[situations]")
{
	auto table = SituationTable::Presets();
	At(table, Situation::kCold).name = "Chill";
	CHECK(SituationTable::Serialize(table).find("\"version\": 1") != std::string::npos);
	CHECK(SituationTable::Serialize(table).find("nextKey") == std::string::npos);

	table.nextKey = 4;
	AddUserTo(table, User(1, "Rainy town", kRainyTown));
	AddUserTo(table, User(3, "Night", { .rows = { { .kind = Kind::kTimeOfDay, .from = 22 * 60, .to = 5 * 60 + 30 } }, .applyInCombat = true }));
	const auto text = SituationTable::Serialize(table);
	CHECK(text.find("\"version\": 2") != std::string::npos);
	CHECK(text.find("\"nextKey\": 4") != std::string::npos);
	CHECK(text.find("\"05:30\"") != std::string::npos);
	SituationTable::Table read;
	REQUIRE_FALSE(SituationTable::Parse(text, read));
	CHECK(read == table);

	// A removed user's situation leaves the file; its key is not used again.
	table[Situations::kPresetCount].removed = true;
	REQUIRE_FALSE(SituationTable::Parse(SituationTable::Serialize(table), read));
	REQUIRE(read.size() == Situations::kPresetCount + 1);
	CHECK(read[Situations::kPresetCount].key == "user:3");
	CHECK(read.nextKey == 4);

	// Removing every one keeps version 2, so nextKey stays.
	table[Situations::kPresetCount + 1].removed = true;
	CHECK(SituationTable::Serialize(table).find("\"nextKey\": 4") != std::string::npos);
}

TEST_CASE("Parse reads version 2's user's situations after the presets, in the file's order", "[situations]")
{
	SituationTable::Table table;
	REQUIRE_FALSE(SituationTable::Parse(R"({ "version": 2, "nextKey": 6, "situations": [
		{ "key": "user:5", "name": "Night", "conditions": [ { "kind": "timeOfDay", "from": "8:00", "to": "06:00" } ] },
		{ "key": "Town", "name": "City" },
		{ "key": "user:2", "name": "Wet", "applyInCombat": true, "comment": "ignored",
		  "conditions": [ { "kind": "weather", "values": ["rain", "coolRegion"] }, { "kind": "indoors", "not": true } ] } ] })",
		table));
	REQUIRE(table.size() == Situations::kPresetCount + 2);
	CHECK(table.nextKey == 6);
	CHECK(At(table, Situation::kTown).name == "City");
	CHECK(At(table, Situation::kTown).rules == SituationTable::PresetRules(Situation::kTown));  // conditions left out: as installed
	// The presets it lacks first, as installed, then the file's order.
	const auto night = static_cast<Situation>(Situations::kPresetCount);
	const auto wet = static_cast<Situation>(Situations::kPresetCount + 1);
	CHECK(table.order ==
		  std::vector{ Situation::kCastle, Situation::kHome, Situation::kCold, Situation::kSleep, night, Situation::kTown, wet });
	CHECK(table[Situations::kPresetCount] == User(5, "Night", { .rows = { { .kind = Kind::kTimeOfDay, .from = 8 * 60, .to = 6 * 60 } } }));
	CHECK(table[Situations::kPresetCount + 1] ==
		  User(2, "Wet",
			  { .rows = { { .kind = Kind::kWeather, .values = SituationRules::kRain | SituationRules::kCoolRegion }, { .kind = Kind::kIndoors, .negated = true } },
				  .applyInCombat = true }));
}

TEST_CASE("Parse refuses the whole file for a user's situation it cannot read", "[situations]")
{
	SituationTable::Table table;
	const auto            refuses = [&table](const char* a_text) {
		const bool refused = SituationTable::Parse(a_text, table).has_value();
		return refused && table == SituationTable::Presets();
	};
	CHECK(refuses(R"({ "version": 2, "situations": [] })"));  // no nextKey
	CHECK(refuses(R"({ "version": 3, "nextKey": 1, "situations": [] })"));
	CHECK(refuses(R"({ "version": 1, "situations": [ { "key": "user:1", "name": "A", "conditions": [] } ] })"));
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:2", "name": "A", "conditions": [] } ] })"));
	CHECK(refuses(R"({ "version": 2, "nextKey": 3, "situations": [ { "key": "user:1", "name": "A", "conditions": [] },
		{ "key": "user:1", "name": "B", "conditions": [] } ] })"));
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "conditions": [] } ] })"));
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A" } ] })"));
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "Town", "conditions": [] } ] })"));
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A", "conditions": [ { "kind": "faction" } ] } ] })"));
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A", "conditions": [ { "kind": "weather", "values": ["fog"] } ] } ] })"));
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A", "conditions": [ { "kind": "weather" } ] } ] })"));
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A", "conditions": [ { "kind": "timeOfDay", "from": "24:00", "to": "6:00" } ] } ] })"));
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A", "conditions": [ { "kind": "indoors", "not": "yes" } ] } ] })"));
}

TEST_CASE("Restore Presets leaves the user's situations as they are", "[situations]")
{
	auto table = SituationTable::Presets();
	table.nextKey = 3;
	At(table, Situation::kTown).removed = true;
	AddUserTo(table, User(1, "Rainy town", kRainyTown));
	AddUserTo(table, User(2, "Gone"));
	table[Situations::kPresetCount + 1].removed = true;
	CHECK(SituationTable::RestoreChanges(table) == Lines{ "Town: restored" });

	const auto restored = SituationTable::RestorePresets(table);
	CHECK(restored[IndexOf(Situation::kTown)] == SituationTable::Preset(Situation::kTown));
	CHECK(restored[Situations::kPresetCount] == table[Situations::kPresetCount]);
	CHECK(restored[Situations::kPresetCount + 1].removed);
	CHECK(restored.nextKey == 3);
	CHECK(SituationTable::RestoreChanges(restored).empty());
}

TEST_CASE("A user's situation's name follows the same rules as a preset's", "[situations]")
{
	auto table = SituationTable::Presets();
	table.nextKey = 3;
	AddUserTo(table, User(1, "Rainy town"));
	CHECK_FALSE(SituationTable::CheckNames(table));
	AddUserTo(table, User(2, "TOWN"));
	CHECK(SituationTable::CheckNames(table));
	At(table, Situation::kTown).removed = true;
	CHECK_FALSE(SituationTable::CheckNames(table));
	table[Situations::kPresetCount + 1].name = "rainy town";
	CHECK(SituationTable::CheckNames(table));
	table[Situations::kPresetCount].removed = true;  // a removed one's name is free to take
	CHECK_FALSE(SituationTable::CheckNames(table));
}

namespace
{
	SituationRules::Place PlaceOf(const char* a_plugin, std::uint32_t a_localID, const char* a_name, std::uint32_t a_formID = 0)
	{
		return { .ref = { a_plugin, a_localID, a_name }, .formID = a_formID };
	}
}

TEST_CASE("A Place row reads at its places, and never holds with none", "[situations]")
{
	using SituationRules::Kind;
	const SituationRules::Row home{ .kind = Kind::kPlace, .places = { PlaceOf("Skyrim.esm", 0x0165EC, "Breezehome"), PlaceOf("Skyrim.esm", 0x01A26F, "") } };
	CHECK(SituationRules::Text({ .rows = { home } }) == "At Breezehome or Skyrim.esm 01A26F.");
	auto negated = home;
	negated.negated = true;
	negated.places.pop_back();
	CHECK(SituationRules::Text({ .rows = { negated } }) == "Not at Breezehome.");
	CHECK(SituationRules::Text({ .rows = { { .kind = Kind::kPlace } } }) == "Specific Location (nothing selected).");
	CHECK(SituationRules::NothingSelected({ .kind = Kind::kPlace }));
	CHECK_FALSE(SituationRules::NothingSelected(home));
	CHECK_FALSE(SituationRules::NothingSelected({ .kind = Kind::kIndoors }));
}

TEST_CASE("A place is the same by plugin and FormID, its load order FormID neither written nor compared", "[situations]")
{
	CHECK(SituationRules::SameForm(PlaceOf("Skyrim.esm", 0x0165EC, "Breezehome").ref, PlaceOf("SKYRIM.ESM", 0x0165EC, "Other").ref));
	CHECK_FALSE(SituationRules::SameForm(PlaceOf("Skyrim.esm", 0x0165EC, "").ref, PlaceOf("Update.esm", 0x0165EC, "").ref));
	CHECK(PlaceOf("Skyrim.esm", 0x0165EC, "Breezehome", 0x000165EC) == PlaceOf("Skyrim.esm", 0x0165EC, "Breezehome"));

	auto table = SituationTable::Presets();
	table.nextKey = 2;
	const SituationRules::Rules rules{ .rows = { { .kind = SituationRules::Kind::kPlace,
										   .places = { PlaceOf("Skyrim.esm", 0x0165EC, "Breezehome", 0x000165EC), PlaceOf("MyHome.esp", 0x000801, "Lakeside", 0) } } } };
	AddUserTo(table, User(1, "Home sweet home", rules));
	const auto text = SituationTable::Serialize(table);
	CHECK(text.find("\"kind\": \"place\"") != std::string::npos);
	CHECK(text.find("\"formId\": \"0x0165EC\"") != std::string::npos);
	CHECK(text.find("formID") == std::string::npos);
	SituationTable::Table read;
	REQUIRE_FALSE(SituationTable::Parse(text, read));
	CHECK(read == table);
	CHECK(read[Situations::kPresetCount].rules.rows[0].places[0].formID == 0);  // SituationStore resolves it
}

TEST_CASE("Parse refuses the whole file for a place it cannot read", "[situations]")
{
	SituationTable::Table table;
	const auto            refuses = [&table](const char* a_text) {
		const bool refused = SituationTable::Parse(a_text, table).has_value();
		return refused && table == SituationTable::Presets();
	};
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A", "conditions": [ { "kind": "place" } ] } ] })"));
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A", "conditions": [ { "kind": "place", "values": ["Whiterun"] } ] } ] })"));
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A", "conditions": [ { "kind": "place", "values": [ { "plugin": "Skyrim.esm", "formId": "1A26F" } ] } ] } ] })"));
	CHECK_FALSE(SituationTable::Parse(
		R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A", "conditions": [ { "kind": "place", "not": true, "values": [] } ] } ] })", table));
}

TEST_CASE("The Add place popup's choices run location, its parents, then the cell", "[situations]")
{
	SituationRules::PlaceChoices choices{ .location = PlaceOf("Skyrim.esm", 0x01, "Proudspire Manor"),
		.parents = { PlaceOf("Skyrim.esm", 0x02, "Solitude Avenues"), PlaceOf("Skyrim.esm", 0x03, "Solitude") },
		.cell = PlaceOf("Skyrim.esm", 0x04, "Proudspire Manor") };
	REQUIRE(choices.At(0));
	CHECK(choices.At(0)->ref.localID == 0x01);
	CHECK(choices.At(2)->ref.localID == 0x03);
	CHECK(choices.At(3)->ref.localID == 0x04);
	CHECK_FALSE(choices.At(4));
	CHECK_FALSE(choices.At(-1));

	const SituationRules::PlaceChoices outdoors{ .cell = PlaceOf("Skyrim.esm", 0x05, "") };  // no location in the open country
	REQUIRE(outdoors.At(0));
	CHECK(outdoors.At(0)->ref.localID == 0x05);
	CHECK_FALSE(outdoors.At(1));
}

TEST_CASE("Of two locations in a row with the same name, the popup keeps the wider", "[situations]")
{
	// Proudspire Manor -> Solitude Avenues ("Solitude") -> Solitude.
	SituationRules::PlaceChoices manor{ .location = PlaceOf("Skyrim.esm", 0x01, "Proudspire Manor"),
		.parents = { PlaceOf("Skyrim.esm", 0x02, "Solitude"), PlaceOf("Skyrim.esm", 0x03, "Solitude") } };
	SituationRules::MergeSameNames(manor);
	REQUIRE(manor.parents.size() == 1);
	CHECK(manor.location->ref.localID == 0x01);
	CHECK(manor.parents[0].ref.localID == 0x03);

	// Standing in the narrower one: This Location becomes the wider.
	SituationRules::PlaceChoices avenues{ .location = PlaceOf("Skyrim.esm", 0x02, "Solitude"), .parents = { PlaceOf("Skyrim.esm", 0x03, "Solitude") } };
	SituationRules::MergeSameNames(avenues);
	CHECK(avenues.location->ref.localID == 0x03);
	CHECK(avenues.parents.empty());

	// Different names stay.
	SituationRules::PlaceChoices hall{ .location = PlaceOf("Skyrim.esm", 0x04, "Dragonsreach"), .parents = { PlaceOf("Skyrim.esm", 0x05, "Whiterun") } };
	SituationRules::MergeSameNames(hall);
	CHECK(hall.location->ref.localID == 0x04);
	CHECK(hall.parents.size() == 1);
}

TEST_CASE("A Spell row reads while its spells are on, and never holds with none", "[situations]")
{
	using SituationRules::Kind;
	const SituationRules::Row flesh{ .kind = Kind::kSpell,
		.spells = { PlaceOf("Skyrim.esm", 0x051B15, "Oakflesh"), PlaceOf("Skyrim.esm", 0x059B7A, "Stoneflesh") } };
	CHECK(SituationRules::Text({ .rows = { flesh } }) == "While Oakflesh or Stoneflesh is on.");
	auto negated = flesh;
	negated.negated = true;
	negated.spells.erase(negated.spells.begin());
	CHECK(SituationRules::Text({ .rows = { negated } }) == "Not while Stoneflesh is on.");
	CHECK(SituationRules::Text({ .rows = { { .kind = Kind::kSpell } } }) == "Spell Effect (nothing selected).");
	CHECK(SituationRules::NothingSelected({ .kind = Kind::kSpell }));
	CHECK_FALSE(SituationRules::NothingSelected(flesh));
	// A Spell row's forms are its spells, a Place row's its places.
	CHECK(SituationRules::FormsOf(flesh).size() == 2);
	CHECK(SituationRules::FormsOf({ .kind = Kind::kPlace, .spells = flesh.spells }).empty());
}

TEST_CASE("A Spell row is written and read as pieces, and the whole file refused for one it cannot read", "[situations]")
{
	auto table = SituationTable::Presets();
	table.nextKey = 2;
	const SituationRules::Rules rules{ .rows = { { .kind = SituationRules::Kind::kSpell,
										   .spells = { PlaceOf("Skyrim.esm", 0x059B7A, "Stoneflesh", 0x00059B7A), PlaceOf("Magic.esp", 0x000812, "Frost Skin", 0) } } },
		.applyInCombat = true };
	AddUserTo(table, User(1, "Stone skin", rules));
	const auto text = SituationTable::Serialize(table);
	CHECK(text.find("\"kind\": \"spell\"") != std::string::npos);
	CHECK(text.find("\"formId\": \"0x059B7A\"") != std::string::npos);
	SituationTable::Table read;
	REQUIRE_FALSE(SituationTable::Parse(text, read));
	CHECK(read == table);
	CHECK(read[Situations::kPresetCount].rules.rows[0].spells[1].ref.name == "Frost Skin");

	const auto refuses = [&read](const char* a_text) {
		const bool refused = SituationTable::Parse(a_text, read).has_value();
		return refused && read == SituationTable::Presets();
	};
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A", "conditions": [ { "kind": "spell" } ] } ] })"));
	CHECK(refuses(R"({ "version": 2, "nextKey": 2, "situations": [ { "key": "user:1", "name": "A", "conditions": [ { "kind": "spell", "values": ["Stoneflesh"] } ] } ] })"));
}

TEST_CASE("Every kind's tooltip formats with the actor's name", "[situations]")
{
	const std::string name = "Lydia";
	for (const auto& info : SituationRules::kKinds) {
		const auto tip = Strings::Format(info.tip, name);
		CHECK((tip.find("Lydia") != std::string::npos) == (info.kind == SituationRules::Kind::kSpell));
	}
}

TEST_CASE("A spell's effects match exactly: none more, none fewer, repeats allowed", "[situations]")
{
	const std::vector<std::uint32_t> stone{ 0x5AD5D };
	const std::vector<std::uint32_t> blessing{ 0x100, 0x200 };
	CHECK(SituationRules::SameEffects(stone, std::vector<std::uint32_t>{ 0x5AD5D }));
	CHECK(SituationRules::SameEffects(stone, std::vector<std::uint32_t>{ 0x5AD5D, 0x5AD5D }));
	CHECK_FALSE(SituationRules::SameEffects(stone, std::vector<std::uint32_t>{ 0x5AD5C }));
	CHECK(SituationRules::SameEffects(blessing, std::vector<std::uint32_t>{ 0x200, 0x100 }));
	CHECK_FALSE(SituationRules::SameEffects(blessing, std::vector<std::uint32_t>{ 0x100 }));                // fewer
	CHECK_FALSE(SituationRules::SameEffects(blessing, std::vector<std::uint32_t>{ 0x100, 0x200, 0x300 }));  // more
}

TEST_CASE("The popup keeps one of each spell with the same name and effects, by name", "[situations]")
{
	const auto choice = [](std::uint32_t a_formID, const char* a_name, std::vector<std::uint32_t> a_effects) {
		return SituationRules::SpellChoice{ .spell = { .ref = { "Skyrim.esm", a_formID, a_name }, .formID = a_formID, .effects = std::move(a_effects) } };
	};
	std::vector<SituationRules::SpellChoice> choices{ choice(0x72316, "Stoneflesh", { 0x1 }), choice(0x965A3, "Scroll of Stoneflesh", { 0x1 }),
		choice(0x5AD5D, "Stoneflesh", { 0x1 }), choice(0x5AD5C, "oakflesh", { 0x2 }), choice(0x10, "Flames", { 0x3 }), choice(0x11, "Flames", { 0x4 }),
		choice(0x5AD5D, "Stoneflesh", { 0x1 }) };
	SituationRules::MergeSameSpells(choices);
	std::vector<std::uint32_t> kept;
	for (const auto& c : choices) {
		kept.push_back(c.spell.formID);
	}
	// Flames twice (other effects), oakflesh before Scroll (case ignored), the lower Stoneflesh.
	CHECK(kept == std::vector<std::uint32_t>{ 0x10, 0x11, 0x5AD5C, 0x965A3, 0x5AD5D });
}

TEST_CASE("Version 2 keeps the order and the presets' rules; back to version 1 once nothing else differs", "[situations]")
{
	using SituationRules::Kind;
	auto table = SituationTable::Presets();
	table.order = { Situation::kSleep, Situation::kTown, Situation::kCastle, Situation::kHome, Situation::kCold };
	auto text = SituationTable::Serialize(table);
	CHECK(text.find("\"version\": 2") != std::string::npos);
	CHECK(text.find("\"nextKey\": 1") != std::string::npos);
	CHECK(text.find("\"Default\"") == std::string::npos);  // as installed
	SituationTable::Table read;
	REQUIRE_FALSE(SituationTable::Parse(text, read));
	CHECK(read == table);

	table.order = SituationTable::Presets().order;
	At(table, Situation::kHome).rules.rows.push_back({ .kind = Kind::kIndoors });
	At(table, Situation::kCastle).removed = true;
	At(table, Situation::kDefault).name = "Everyday";
	text = SituationTable::Serialize(table);
	CHECK(text.find("\"version\": 2") != std::string::npos);
	CHECK(text.find("\"Everyday\"") != std::string::npos);
	REQUIRE_FALSE(SituationTable::Parse(text, read));
	CHECK(read == table);

	At(table, Situation::kHome).rules = SituationTable::PresetRules(Situation::kHome);
	text = SituationTable::Serialize(table);
	CHECK(text.find("\"version\": 1") != std::string::npos);
	REQUIRE_FALSE(SituationTable::Parse(text, read));
	CHECK(read == table);
}

TEST_CASE("Reordered puts the shown situations in their new order, a removed one keeping its place", "[situations]")
{
	const std::vector order{ Situation::kTown, Situation::kCastle, Situation::kHome, Situation::kCold, Situation::kSleep };
	const auto        removed = [](Situation a_situation) { return a_situation == Situation::kCastle; };
	const std::vector shown{ Situation::kSleep, Situation::kTown, Situation::kHome, Situation::kCold };
	CHECK(SituationTable::Reordered(order, shown, removed) ==
		  std::vector{ Situation::kSleep, Situation::kCastle, Situation::kTown, Situation::kHome, Situation::kCold });
	CHECK_FALSE(SituationTable::Reordered(order, std::vector{ Situation::kSleep, Situation::kTown, Situation::kHome }, removed));
	CHECK_FALSE(SituationTable::Reordered(order, std::vector{ Situation::kSleep, Situation::kTown, Situation::kHome, Situation::kCastle }, removed));
	CHECK_FALSE(SituationTable::Reordered(order, std::vector{ Situation::kSleep, Situation::kSleep, Situation::kHome, Situation::kCold }, removed));
}

TEST_CASE("Restore Presets brings back the presets' rules and leaves the order", "[situations]")
{
	auto table = SituationTable::Presets();
	table.order = { Situation::kSleep, Situation::kTown, Situation::kCastle, Situation::kHome, Situation::kCold };
	At(table, Situation::kCold).rules.rows.pop_back();
	At(table, Situation::kSleep).rules.applyInCombat = true;
	const auto restored = SituationTable::RestorePresets(table);
	CHECK(restored[IndexOf(Situation::kCold)] == SituationTable::Preset(Situation::kCold));
	CHECK(restored[IndexOf(Situation::kSleep)] == SituationTable::Preset(Situation::kSleep));
	CHECK(restored.order == table.order);
	CHECK(SituationTable::RestoreChanges(restored).empty());
}

namespace
{
	using SituationRules::CoveredBy;
	using SituationRules::PlaceFact;

	Rules RulesOf(std::initializer_list<Row> a_rows, bool a_applyInCombat = false)
	{
		return { .rows = a_rows, .applyInCombat = a_applyInCombat };
	}
}

TEST_CASE("A situation is never worn while one lower applies whenever it does", "[situations]")
{
	const Row                        town{ .kind = Kind::kLocationType, .values = SituationRules::kTown };
	const Row                        townOrCastle{ .kind = Kind::kLocationType, .values = SituationRules::kTown | SituationRules::kCastle };
	const Row                        rain{ .kind = Kind::kWeather, .values = SituationRules::kRain };
	const Row                        outdoors{ .kind = Kind::kIndoors, .negated = true };
	const std::span<const PlaceFact> none;

	// The lower one asks for less.
	CHECK(CoveredBy(RulesOf({ town, rain }), RulesOf({ townOrCastle }), none));
	CHECK(CoveredBy(RulesOf({ rain, outdoors }), RulesOf({ outdoors }), none));
	CHECK_FALSE(CoveredBy(RulesOf({ townOrCastle }), RulesOf({ town }), none));
	CHECK_FALSE(CoveredBy(RulesOf({ town }), RulesOf({ town, rain }), none));
	// Negated, the narrower list is the one implied.
	const Row notRain{ .kind = Kind::kWeather, .negated = true, .values = SituationRules::kRain };
	const Row notRainOrSnow{ .kind = Kind::kWeather, .negated = true, .values = SituationRules::kRain | SituationRules::kSnow };
	CHECK(CoveredBy(RulesOf({ notRainOrSnow }), RulesOf({ notRain }), none));
	CHECK_FALSE(CoveredBy(RulesOf({ notRain }), RulesOf({ notRainOrSnow }), none));
	CHECK_FALSE(CoveredBy(RulesOf({ notRain }), RulesOf({ rain }), none));
	// Swimming is in water.
	const Row inWater{ .kind = Kind::kWater, .values = SituationRules::kInWater };
	const Row swimming{ .kind = Kind::kWater, .values = SituationRules::kSwimming };
	const Row notInWater{ .kind = Kind::kWater, .negated = true, .values = SituationRules::kInWater };
	const Row notSwimming{ .kind = Kind::kWater, .negated = true, .values = SituationRules::kSwimming };
	CHECK(CoveredBy(RulesOf({ swimming }), RulesOf({ inWater }), none));
	CHECK_FALSE(CoveredBy(RulesOf({ inWater }), RulesOf({ swimming }), none));
	CHECK(CoveredBy(RulesOf({ notInWater }), RulesOf({ notSwimming }), none));
	CHECK_FALSE(CoveredBy(RulesOf({ notSwimming }), RulesOf({ notInWater }), none));
	// Combat: the lower one must be allowed during combat when the upper one is.
	const Row combat{ .kind = Kind::kInCombat };
	CHECK(CoveredBy(RulesOf({ town }), RulesOf({ town }, true), none));
	CHECK_FALSE(CoveredBy(RulesOf({ town }, true), RulesOf({ town }), none));
	CHECK_FALSE(CoveredBy(RulesOf({ town, combat }), RulesOf({ town }), none));
	// A situation that never applies is never marked and covers nothing.
	CHECK_FALSE(CoveredBy(RulesOf({}), RulesOf({ town }), none));
	CHECK_FALSE(CoveredBy(RulesOf({ town }), RulesOf({}), none));
	CHECK_FALSE(CoveredBy(RulesOf({ town, { .kind = Kind::kWeather } }), RulesOf({ town }), none));
	// The presets as installed: Sleep and Cold are not covered by each other.
	CHECK_FALSE(CoveredBy(SituationTable::PresetRules(Situation::kCold), SituationTable::PresetRules(Situation::kSleep), none));
	CHECK_FALSE(CoveredBy(SituationTable::PresetRules(Situation::kHome), SituationTable::PresetRules(Situation::kTown), none));
}

TEST_CASE("Never worn: a row reading the player implies only one reading the player", "[situations]")
{
	using SituationRules::Target;
	const std::span<const PlaceFact> none;
	const Row                        wearerFights{ .kind = Kind::kInCombat };
	const Row                        youFight{ .kind = Kind::kInCombat, .target = Target::kPlayer };
	CHECK(CoveredBy(RulesOf({ youFight }), RulesOf({ youFight }, true), none));
	CHECK_FALSE(CoveredBy(RulesOf({ youFight }), RulesOf({ wearerFights }, true), none));
	CHECK_FALSE(CoveredBy(RulesOf({ wearerFights }), RulesOf({ youFight }, true), none));
	const Row yourWeapon{ .kind = Kind::kWeapon, .target = Target::kPlayer };
	CHECK(CoveredBy(RulesOf({ yourWeapon, youFight }), RulesOf({ yourWeapon }, true), none));
}

TEST_CASE("Never worn: a time range within another, spells with the same effects", "[situations]")
{
	const std::span<const PlaceFact> none;
	const Row                        night{ .kind = Kind::kTimeOfDay, .from = 20 * 60, .to = 6 * 60 };
	const Row                        lateNight{ .kind = Kind::kTimeOfDay, .from = 23 * 60, .to = 2 * 60 };
	const Row                        evening{ .kind = Kind::kTimeOfDay, .from = 18 * 60, .to = 22 * 60 };
	const Row                        wholeDay{ .kind = Kind::kTimeOfDay, .from = 0, .to = 0 };
	CHECK(CoveredBy(RulesOf({ lateNight }), RulesOf({ night }), none));
	CHECK_FALSE(CoveredBy(RulesOf({ night }), RulesOf({ lateNight }), none));
	CHECK_FALSE(CoveredBy(RulesOf({ evening }), RulesOf({ night }), none));
	CHECK(CoveredBy(RulesOf({ night }), RulesOf({ wholeDay }), none));
	CHECK_FALSE(CoveredBy(RulesOf({ wholeDay }), RulesOf({ night }), none));

	const auto spell = [](std::uint32_t a_localID, std::vector<std::uint32_t> a_effects) {
		return SituationRules::Spell{ .ref = { "Skyrim.esm", a_localID, "" }, .formID = a_localID, .effects = std::move(a_effects) };
	};
	const Row stoneflesh{ .kind = Kind::kSpell, .spells = { spell(0x59B7A, { 0x1 }) } };
	const Row scroll{ .kind = Kind::kSpell, .spells = { spell(0x965A3, { 0x1 }) } };  // the same effects
	const Row flesh{ .kind = Kind::kSpell, .spells = { spell(0x51B15, { 0x2 }), spell(0x59B7A, { 0x1 }) } };
	CHECK(CoveredBy(RulesOf({ stoneflesh }), RulesOf({ flesh }), none));
	CHECK(CoveredBy(RulesOf({ scroll }), RulesOf({ stoneflesh }), none));
	CHECK_FALSE(CoveredBy(RulesOf({ flesh }), RulesOf({ stoneflesh }), none));
}

TEST_CASE("Never worn: a place within another, and a place whose every location is of a type", "[situations]")
{
	const auto                   place = [](std::uint32_t a_formID) { return PlaceOf("Skyrim.esm", a_formID, "", a_formID); };
	constexpr std::uint32_t      kWhiterun = 0x10;
	constexpr std::uint32_t      kBreezehome = 0x11;
	constexpr std::uint32_t      kCell = 0x12;
	constexpr std::uint32_t      kRuin = 0x13;
	const auto                   mask = [](std::uint32_t a_types) { return static_cast<std::uint16_t>(1u << a_types); };
	const std::vector<PlaceFact> facts{
		{ .formID = kWhiterun, .typeMasks = static_cast<std::uint16_t>(mask(SituationRules::kTown) | mask(SituationRules::kTown | SituationRules::kCastle)) },
		{ .formID = kBreezehome, .within = { kWhiterun }, .typeMasks = mask(SituationRules::kTown | SituationRules::kPlayerHome) },
		{ .formID = kCell, .within = { kBreezehome, kWhiterun }, .typeMasks = mask(SituationRules::kTown | SituationRules::kPlayerHome) },
		{ .formID = kRuin, .typeMasks = static_cast<std::uint16_t>(mask(SituationRules::kDungeon) | mask(0)) },
	};
	const Row whiterun{ .kind = Kind::kPlace, .places = { place(kWhiterun) } };
	const Row breezehome{ .kind = Kind::kPlace, .places = { place(kBreezehome) } };
	const Row cell{ .kind = Kind::kPlace, .places = { place(kCell) } };
	const Row ruin{ .kind = Kind::kPlace, .places = { place(kRuin) } };
	CHECK(CoveredBy(RulesOf({ breezehome }), RulesOf({ whiterun }), facts));
	CHECK(CoveredBy(RulesOf({ cell }), RulesOf({ whiterun }), facts));
	CHECK_FALSE(CoveredBy(RulesOf({ whiterun }), RulesOf({ breezehome }), facts));
	CHECK_FALSE(CoveredBy(RulesOf({ breezehome }), RulesOf({ whiterun }), {}));  // no facts: nothing told

	const Row town{ .kind = Kind::kLocationType, .values = SituationRules::kTown };
	const Row home{ .kind = Kind::kLocationType, .values = SituationRules::kPlayerHome };
	const Row dungeon{ .kind = Kind::kLocationType, .values = SituationRules::kDungeon };
	CHECK(CoveredBy(RulesOf({ whiterun }), RulesOf({ town }), facts));
	CHECK(CoveredBy(RulesOf({ breezehome }), RulesOf({ home }), facts));
	CHECK_FALSE(CoveredBy(RulesOf({ whiterun }), RulesOf({ home }), facts));
	CHECK_FALSE(CoveredBy(RulesOf({ ruin }), RulesOf({ dungeon }), facts));  // a location within it has no type
	// Negated, the narrower list is the one implied.
	const Row notWhiterun{ .kind = Kind::kPlace, .negated = true, .places = { place(kWhiterun) } };
	const Row notBreezehome{ .kind = Kind::kPlace, .negated = true, .places = { place(kBreezehome) } };
	CHECK(CoveredBy(RulesOf({ notWhiterun }), RulesOf({ notBreezehome }), facts));
	CHECK_FALSE(CoveredBy(RulesOf({ notBreezehome }), RulesOf({ notWhiterun }), facts));
}
