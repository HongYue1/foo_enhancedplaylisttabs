#pragma once

// What a tab-title script needs (no SDK): which of our fields it mentions, so the playlists model
// subscribes to item events only when %size% or %length% is shown, and a reorder relabels only
// when %index% is. Also the %length% text.

#include <cstdint>
#include <string>
#include <string_view>

namespace ept {

enum TitleField : std::uint32_t {
    title_field_name = 1u << 0,    // %title%, %playlist_name%
    title_field_index = 1u << 1,   // %index%
    title_field_size = 1u << 2,    // %size%, %playlist_size%, %list_total%
    title_field_length = 1u << 3,  // %length%, %playlist_duration%
    title_field_active = 1u << 4,  // %is_active%
    title_field_playing = 1u << 5, // %is_playing%, %isplaying%, %ispaused%: playback state
    title_field_lock = 1u << 6,    // %is_locked%, %lock_name%
    title_field_playing_playlist = 1u << 7, // %playlist_is_playing%: the playing playlist, kept after stop
    title_field_queue_total = 1u << 8,      // %queue_total%
    title_field_queue_playlist = 1u << 9,   // %playlist_queue_total%
};

//! The TitleField bits for the %fields% in `pattern`, case-insensitive. Text in 'quotes' is
//! literal and does not count. Conservative: a field in a branch that never runs still counts.
[[nodiscard]] std::uint32_t title_fields(std::string_view pattern) noexcept;

//! A playlist's duration the way foobar2000 shows it: "3:05", "1:02:03", "2d 3:04:05".
//! Negative or not-a-number gives "0:00"; fractions are dropped.
[[nodiscard]] std::string format_duration(double seconds);

} // namespace ept
