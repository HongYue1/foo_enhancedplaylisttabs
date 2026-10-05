#include "title_fields.h"

#include <cmath>
#include <cstdio>

namespace ept {

namespace {

[[nodiscard]] bool same(std::string_view a, const char* b) noexcept {
    std::size_t i = 0;
    for (; i < a.size() && b[i] != 0; ++i) {
        const char x = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
        if (x != b[i]) return false;
    }
    return i == a.size() && b[i] == 0;
}

[[nodiscard]] std::uint32_t field_bit(std::string_view name) noexcept {
    if (same(name, "title") || same(name, "playlist_name")) return title_field_name;
    if (same(name, "index")) return title_field_index;
    if (same(name, "size") || same(name, "playlist_size")) return title_field_size;
    if (same(name, "length") || same(name, "playlist_duration")) return title_field_length;
    if (same(name, "is_active")) return title_field_active;
    if (same(name, "is_playing")) return title_field_playing;
    if (same(name, "is_locked") || same(name, "lock_name")) return title_field_lock;
    return 0;
}

} // namespace

std::uint32_t title_fields(std::string_view pattern) noexcept {
    std::uint32_t out = 0;
    std::size_t i = 0;
    while (i < pattern.size()) {
        const char c = pattern[i];
        if (c == '\'') {
            // Literal up to the closing quote ('' is a literal quote: an empty literal here).
            const std::size_t end = pattern.find('\'', i + 1);
            if (end == std::string_view::npos) break;
            i = end + 1;
        } else if (c == '%') {
            const std::size_t end = pattern.find('%', i + 1);
            if (end == std::string_view::npos) break;
            out |= field_bit(pattern.substr(i + 1, end - i - 1));
            i = end + 1;
        } else {
            ++i;
        }
    }
    return out;
}

std::string format_duration(double seconds) {
    if (!(seconds > 0) || !std::isfinite(seconds)) return "0:00";
    const auto total = static_cast<unsigned long long>(seconds);
    const unsigned long long days = total / 86400, h = total / 3600 % 24, m = total / 60 % 60, s = total % 60;
    char buf[64];
    if (days > 0) {
        std::snprintf(buf, sizeof buf, "%llud %llu:%02llu:%02llu", days, h, m, s);
    } else if (h > 0) {
        std::snprintf(buf, sizeof buf, "%llu:%02llu:%02llu", h, m, s);
    } else {
        std::snprintf(buf, sizeof buf, "%llu:%02llu", m, s);
    }
    return buf;
}

} // namespace ept
