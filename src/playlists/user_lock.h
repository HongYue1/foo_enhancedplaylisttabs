#pragma once

// "Lock playlist" from the tab menu: our own playlist_lock, which blocks adding, removing,
// reordering and replacing tracks, renaming and removing the playlist. Double-click still plays.
// The choice is stored in the playlist property (playlist_flag_locked) and the lock is
// reinstalled when foobar2000 starts (the core does not persist locks). Main thread only.

#include <cstddef>

namespace ept::playlists {

//! The lock name shown by foobar2000 (and %lock_name%).
inline constexpr const char* user_lock_name = "Enhanced Playlist Tabs";

//! Our lock is on this playlist.
[[nodiscard]] bool has_user_lock(std::size_t index) noexcept;
//! Installs our lock (no property change). False if the playlist already has a lock.
bool install_user_lock(std::size_t index) noexcept;
//! Locks or unlocks, and stores the choice. False if another component's lock is in the way.
bool set_user_lock(std::size_t index, bool locked) noexcept;

} // namespace ept::playlists
