#pragma once

// Title formatting for tab titles: the fields a playlist offers. The playing
// track's fields are not available here; the script runs without a track.
//
//   %title%, %playlist_name%   the playlist's name
//   %index%                    1-based position
//   %size%, %playlist_size%, %list_total%  item count
//   %is_active%                "1" or nothing, for $if()
//   %is_playing%, %isplaying%  "1" while this playlist plays (or is paused), else nothing
//   %ispaused%                 "1" while this playlist is paused
//   %playlist_is_playing%      "1" for the playing playlist, which foobar2000 keeps after stop
//   %is_locked%, %lock_name%   "1" or nothing; the lock's owner ("Autoplaylist", ...)
//   %length%, %playlist_duration%  total length, "1:02:03" (cached track info)
//   %queue_total%              tracks in the playback queue; nothing when it is empty
//   %playlist_queue_total%     queued tracks from this playlist (repeats count); nothing when none

#include <helpers/foobar2000+atl.h>

#include <cstddef>
#include <string>

namespace ept::playlists {

struct Entry;

//! What a title can show beyond the playlist itself. Built by the caller once per relabel.
struct TitleContext {
    std::size_t active{SIZE_MAX};
    std::size_t playing{SIZE_MAX}; //!< playlist_manager::get_playing_playlist(), kept after stop
    bool playback{false};          //!< playing or paused
    bool paused{false};
    std::size_t queue_total{0};
    std::size_t queue_here{0}; //!< queued tracks from this playlist
};

//! The playback part of a TitleContext, read now. Main thread. Never throws.
void read_playback(TitleContext& context) noexcept;

//! Runs `script` for playlist `index`. Never throws; an error gives an empty title.
[[nodiscard]] std::wstring format_title(const titleformat_object::ptr& script, std::size_t index,
                                        const Entry& entry, const TitleContext& context) noexcept;

//! The title the active playlist's tab gets with `pattern` (empty = its name). For the Configure
//! dialog's preview; compiles the pattern each call. Main thread. Never throws.
[[nodiscard]] std::wstring preview_title(const std::string& pattern) noexcept;

} // namespace ept::playlists
