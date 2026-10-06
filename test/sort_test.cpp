// Offline tests for src/model/playlist_sort.cpp. Run build_tests.bat.

#include <cstdio>
#include <vector>

#include "../src/model/playlist_sort.h"

using namespace ept;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

[[nodiscard]] bool same(const std::vector<std::size_t>& a, const std::vector<std::size_t>& b) { return a == b; }

} // namespace

int main() {
    check(sort_name(L"The Beatles", true) == L"Beatles", "article: the");
    check(sort_name(L"an Album", true) == L"Album", "article: an, any case");
    check(sort_name(L"A Tribe", true) == L"Tribe", "article: a");
    check(sort_name(L"Theory", true) == L"Theory", "article: only as a word");
    check(sort_name(L"The", true) == L"The", "article: alone stays");
    check(sort_name(L"  The  End", true) == L"End", "article: spaces around");
    check(sort_name(L"The Beatles", false) == L"The Beatles", "article: kept when off");
    check(sort_name(L"Apple", true) == L"Apple", "article: no article");

    check(natural_compare(L"Mix 2", L"Mix 10") < 0, "natural: numbers by value");
    check(natural_compare(L"mix 02", L"Mix 2") == 0, "natural: case and leading zeros");
    check(natural_compare(L"abc", L"ABD") < 0, "natural: case-insensitive");
    check(natural_compare(L"ab", L"abc") < 0 && natural_compare(L"abc", L"ab") > 0, "natural: prefix first");
    check(natural_compare(L"", L"") == 0, "natural: empty");

    const std::vector<SortEntry> e = {
        {L"The Zombies", 10, 300.0}, {L"Abba", 5, 900.0}, {L"Mix 10", 5, 100.0}, {L"Mix 2", 20, 100.0}, {L"a Band", 1, 50.0},
    };
    check(same(sort_order(e, SortKey::name, false, true), {1, 4, 3, 2, 0}), "name, ignoring articles");
    check(same(sort_order(e, SortKey::name, false, false), {4, 1, 3, 2, 0}), "name, with articles");
    check(same(sort_order(e, SortKey::name, true, true), {0, 2, 3, 4, 1}), "name, descending");
    check(same(sort_order(e, SortKey::size, false, true), {4, 1, 2, 0, 3}), "size, ties by name");
    check(same(sort_order(e, SortKey::size, true, true), {3, 0, 2, 1, 4}), "size, descending");
    check(same(sort_order(e, SortKey::length, false, true), {4, 3, 2, 0, 1}), "length, ties by name");

    const std::vector<SortEntry> twins = {{L"Same", 1, 1.0}, {L"same", 1, 1.0}, {L"SAME", 1, 1.0}};
    check(same(sort_order(twins, SortKey::name, false, true), {0, 1, 2}), "stable ascending");
    check(same(sort_order(twins, SortKey::name, true, true), {0, 1, 2}), "stable descending");
    check(sort_order({}, SortKey::size, false, true).empty(), "no playlists");

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
