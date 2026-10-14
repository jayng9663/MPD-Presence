#include "doctest.h"
#include "util.hpp"

TEST_CASE("split_trim: basic comma list") {
	auto out = util::split_trim("a, b ,c", ',');
	REQUIRE(out.size() == 3);
	CHECK(out[0] == "a");
	CHECK(out[1] == "b");
	CHECK(out[2] == "c");
}

TEST_CASE("split_trim: empty input yields empty vector") {
	CHECK(util::split_trim("", ',').empty());
}

TEST_CASE("split_trim: only whitespace yields empty vector") {
	CHECK(util::split_trim("   ", ',').empty());
}

TEST_CASE("split_trim: drops empty items from consecutive delimiters") {
	auto out = util::split_trim("a,,b,", ',');
	REQUIRE(out.size() == 2);
	CHECK(out[0] == "a");
	CHECK(out[1] == "b");
}

TEST_CASE("split_trim: trims tabs as well as spaces") {
	auto out = util::split_trim("\tfoo\t,  bar  ", ',');
	REQUIRE(out.size() == 2);
	CHECK(out[0] == "foo");
	CHECK(out[1] == "bar");
}

TEST_CASE("split_trim: single item, no delimiter present") {
	auto out = util::split_trim("fingerprint", ',');
	REQUIRE(out.size() == 1);
	CHECK(out[0] == "fingerprint");
}

TEST_CASE("split_trim: real-world album art method order config value") {
	auto out = util::split_trim("fingerprint,search", ',');
	REQUIRE(out.size() == 2);
	CHECK(out[0] == "fingerprint");
	CHECK(out[1] == "search");
}

TEST_CASE("split_trim: real-world ignore-list with leading slash paths") {
	auto out = util::split_trim(" /ASMR , /Podcasts/News ", ',');
	REQUIRE(out.size() == 2);
	CHECK(out[0] == "/ASMR");
	CHECK(out[1] == "/Podcasts/News");
}
