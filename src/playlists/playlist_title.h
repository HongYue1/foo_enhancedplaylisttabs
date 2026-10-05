#pragma once

// Title formatting for tab titles: the fields a playlist offers. The playing
// track's fields are not available here; the script runs without a track.
//
//   %title%, %playlist_name%   the playlist's name
//   %index%                    1-based position
//   %size%, %playlist_size%    item count
//   %is_active%, %is_playing%  "1" or nothing, for $if()
//   %is_locked%, %lock_name%   "1" or nothing; the lock's owner ("Autoplaylist", ...)
//   %length%, %playlist_duration%  total length, "1:02:03" (cached track info)

#include <helpers/foobar2000+atl.h>

#include <cstddef>
#include <string>

namespace ept::playlists {

struct Entry;

//! Runs `script` for playlist `index`. Never throws; an error gives an empty title.
[[nodiscard]] std::wstring format_title(const titleformat_object::ptr& script, std::size_t index,
                                        const Entry& entry, std::size_t active, std::size_t playing) noexcept;

//! The title the active playlist's tab gets with `pattern` (empty = its name). For the Configure
//! dialog's preview; compiles the pattern each call. Main thread. Never throws.
[[nodiscard]] std::wstring preview_title(const std::string& pattern) noexcept;

} // namespace ept::playlists
