#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

#include "PieceFile.h"

namespace
{
	using nlohmann::json;
}

TEST_CASE("ParsePiece reads a piece and keeps its name", "[PieceFile]")
{
	const json          in = json::parse(R"({ "plugin": "Skyrim.esm", "formId": "0x013921", "name": "Imperial Light Armor" })");
	PieceFile::PieceRef piece;
	REQUIRE_FALSE(PieceFile::ParsePiece(in, piece).has_value());
	CHECK(piece.plugin == "Skyrim.esm");
	CHECK(piece.localID == 0x013921u);
	CHECK(piece.name == "Imperial Light Armor");
}

TEST_CASE("ParsePiece refuses a piece without a plugin or with a bad formId", "[PieceFile]")
{
	PieceFile::PieceRef piece;
	CHECK(PieceFile::ParsePiece(json::parse(R"({ "formId": "0x000001" })"), piece).has_value());
	CHECK(PieceFile::ParsePiece(json::parse(R"({ "plugin": "A.esp", "formId": "000001" })"), piece).has_value());
	CHECK(PieceFile::ParsePiece(json::parse(R"({ "plugin": "A.esp", "formId": "0x1000000" })"), piece).has_value());
	CHECK(PieceFile::ParsePiece(json::parse(R"({ "plugin": "A.esp", "formId": "0x12g" })"), piece).has_value());
	CHECK(PieceFile::ParsePiece(json::parse(R"("not an object")"), piece).has_value());
}

TEST_CASE("ToJson writes the formId as 0x and six hex digits, which ParsePiece reads back", "[PieceFile]")
{
	const PieceFile::PieceRef piece{ "Mod.esl", 0x800, "Hood" };
	const auto                out = PieceFile::ToJson(piece);
	CHECK(out["formId"] == "0x000800");
	PieceFile::PieceRef back;
	REQUIRE_FALSE(PieceFile::ParsePiece(json::parse(out.dump()), back).has_value());
	CHECK(back.plugin == piece.plugin);
	CHECK(back.localID == piece.localID);
	CHECK(back.name == piece.name);
}

TEST_CASE("ReadId takes ids from 1 up that fit 32 bits", "[PieceFile]")
{
	// Parsed, as the files are: a non-negative number parses as unsigned.
	std::uint32_t id = 0;
	const auto    reads = [&id](const char* a_text) {
		const json value = json::parse(a_text);
		return PieceFile::ReadId(&value, id);
	};
	CHECK(reads("1"));
	CHECK(id == 1u);
	CHECK_FALSE(reads("0"));
	CHECK_FALSE(reads("-1"));
	CHECK_FALSE(reads("4294967296"));
	CHECK_FALSE(reads("\"1\""));
	CHECK_FALSE(PieceFile::ReadId(nullptr, id));
}

TEST_CASE("ReadString refuses an empty string", "[PieceFile]")
{
	std::string text;
	const auto  reads = [&text](const json& a_value) { return PieceFile::ReadString(&a_value, text); };
	CHECK(reads("Guard"));
	CHECK(text == "Guard");
	CHECK_FALSE(reads(""));
	CHECK_FALSE(reads(3));
}
