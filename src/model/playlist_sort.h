#pragma once

// "Sort playlists": the order only. Pure code, no SDK and no Win32, so test/sort_test.cpp runs it
// offline; switcher_core.cpp gathers the names, counts and lengths and applies the result with
// playlist_manager::reorder.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ept {

enum class SortKey : std::uint8_t { name, size, length };

struct SortEntry {
    std::wstring name;
    std::size_t count{0};
    //! Seconds.
    double length{0.0};
};

//! `name` without a leading "a ", "an " or "the " (any case), and without leading spaces. Never
//! empty unless `name` is: "The" alone stays "The".
[[nodiscard]] std::wstring_view sort_name(std::wstring_view name, bool ignore_articles) noexcept;

//! Case-insensitive natural order: runs of digits compare by value, so "Mix 2" < "Mix 10".
//! Negative, 0 or positive.
[[nodiscard]] int natural_compare(std::wstring_view a, std::wstring_view b) noexcept;

//! The sorted order as indices into `entries`: result[i] is the entry that goes to place i.
//! Stable: equal entries keep their order, also when descending. Size and length ties go by name.
[[nodiscard]] std::vector<std::size_t> sort_order(std::span<const SortEntry> entries, SortKey key, bool descending,
                                                  bool ignore_articles);

} // namespace ept
