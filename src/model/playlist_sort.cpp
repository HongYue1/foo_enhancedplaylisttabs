#include "playlist_sort.h"

#include <algorithm>
#include <cwctype>
#include <numeric>

namespace ept {

namespace {

[[nodiscard]] bool is_space(wchar_t c) noexcept { return c == L' ' || c == L'\t' || c == 0x00A0; }
[[nodiscard]] bool is_digit(wchar_t c) noexcept { return c >= L'0' && c <= L'9'; }
[[nodiscard]] wchar_t fold(wchar_t c) noexcept { return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c))); }

//! `name` starts with `word` (lower case ASCII) followed by a space.
[[nodiscard]] bool starts_with_word(std::wstring_view name, std::wstring_view word) noexcept {
    if (name.size() <= word.size() || !is_space(name[word.size()])) return false;
    for (std::size_t i = 0; i < word.size(); ++i) {
        if (fold(name[i]) != word[i]) return false;
    }
    return true;
}

} // namespace

std::wstring_view sort_name(std::wstring_view name, bool ignore_articles) noexcept {
    while (!name.empty() && is_space(name.front())) name.remove_prefix(1);
    if (!ignore_articles) return name;
    for (const std::wstring_view article : {std::wstring_view(L"the"), std::wstring_view(L"an"), std::wstring_view(L"a")}) {
        if (!starts_with_word(name, article)) continue;
        std::wstring_view rest = name.substr(article.size());
        while (!rest.empty() && is_space(rest.front())) rest.remove_prefix(1);
        if (!rest.empty()) return rest;
    }
    return name;
}

int natural_compare(std::wstring_view a, std::wstring_view b) noexcept {
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (is_digit(a[i]) && is_digit(b[j])) {
            // Compare the numbers by value: skip leading zeros, then the longer run is larger.
            std::size_t za = i;
            std::size_t zb = j;
            while (za < a.size() && a[za] == L'0') ++za;
            while (zb < b.size() && b[zb] == L'0') ++zb;
            std::size_t ea = za;
            std::size_t eb = zb;
            while (ea < a.size() && is_digit(a[ea])) ++ea;
            while (eb < b.size() && is_digit(b[eb])) ++eb;
            if (ea - za != eb - zb) return ea - za < eb - zb ? -1 : 1;
            for (std::size_t k = 0; k < ea - za; ++k) {
                if (a[za + k] != b[zb + k]) return a[za + k] < b[zb + k] ? -1 : 1;
            }
            i = ea;
            j = eb;
            continue;
        }
        const wchar_t x = fold(a[i]);
        const wchar_t y = fold(b[j]);
        if (x != y) return x < y ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

std::vector<std::size_t> sort_order(std::span<const SortEntry> entries, SortKey key, bool descending,
                                    bool ignore_articles) {
    std::vector<std::size_t> order(entries.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::vector<std::wstring_view> names(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) names[i] = sort_name(entries[i].name, ignore_articles);
    // -1, 0, 1 in ascending order; descending flips it, so ties stay in place either way.
    const auto compare = [&](std::size_t a, std::size_t b) noexcept {
        int c = 0;
        if (key == SortKey::size && entries[a].count != entries[b].count) {
            c = entries[a].count < entries[b].count ? -1 : 1;
        } else if (key == SortKey::length && entries[a].length != entries[b].length) {
            c = entries[a].length < entries[b].length ? -1 : 1;
        } else {
            c = natural_compare(names[a], names[b]);
        }
        return descending ? -c : c;
    };
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return compare(a, b) < 0; });
    return order;
}

} // namespace ept
