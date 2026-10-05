#pragma once

// The playlists, as every Enhanced Playlist Tabs element sees them. Process-wide and main thread
// only: ONE playlist_callback however many elements exist, registered while at least one element
// has a window, with only the playlist-level flags. Item events are added (modify_callback) only
// while some element's titles show %size% or %length% (set_needs), and arrive coalesced: one
// Change::items per playlist per main-thread turn. Names are converted to
// UTF-16 once per change, so building strip items is a copy, never a conversion.
//
// Per-playlist flags (hidden) live in a playlist property (guids::playlist_flags), read when a
// playlist appears and written only by set_hidden().

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ept::playlists {

struct Entry {
    //! Stable identity for the strip (never 0, never reused while the model lives): follows the
    //! playlist through reorders, so the strip keeps its text layout.
    std::uint64_t key{0};
    std::wstring name;
    //! PlaylistFlag bits (codec.h).
    std::uint32_t flags{0};
    bool locked{false};
    //! Seconds (length_of); negative = not summed yet. Summed on a worker thread, so after a
    //! change of tracks the old value stays (length_stale) until the new one arrives.
    double length{-1.0};
    bool length_stale{false};
    bool length_queued{false};
    //! Bumped on every change of tracks: a sum started before it is thrown away.
    std::uint32_t items_version{0};
    [[nodiscard]] bool hidden() const noexcept;
};

enum class Change : std::uint8_t {
    //! Everything may differ: rebuild from entries().
    reset,
    //! entries()[index] is new.
    created,
    //! The playlist at `index` went away; later indices shifted down by one. A multiple removal
    //! arrives as one event per playlist, highest index first, so every event is consistent
    //! with entries(). active() is already the final active playlist.
    removed,
    //! Same playlists, new order (keys unchanged).
    reordered,
    //! entries()[index].name changed.
    renamed,
    //! entries()[index].flags changed (hidden or shown).
    flags,
    //! entries()[index].locked changed.
    locked,
    //! active() changed; index is the old active playlist (or SIZE_MAX).
    activated,
    //! entries()[index]'s tracks changed (added, removed, replaced; with need_lengths also
    //! modified). Only with set_needs; coalesced per main-thread turn.
    items,
};

class Listener {
public:
    virtual void on_playlists(Change change, std::size_t index) noexcept = 0;

protected:
    ~Listener() = default;
};

//! The first subscriber registers the playlist callback and reads every playlist once; the last
//! one to leave unregisters it and drops the cache.
void subscribe(Listener& listener) noexcept;
void unsubscribe(Listener& listener) noexcept;

//! Valid while anyone is subscribed.
[[nodiscard]] const std::vector<Entry>& entries() noexcept;
//! The playlist with this Entry::key now, or SIZE_MAX (gone, or nobody subscribed).
[[nodiscard]] std::size_t index_of_key(std::uint64_t key) noexcept;
//! The active playlist, or SIZE_MAX.
[[nodiscard]] std::size_t active() noexcept;

//! What a listener's titles need from item events.
enum Need : std::uint32_t {
    need_counts = 1u << 0,  // added / removed / replaced
    need_lengths = 1u << 1, // also modified (a track's length became known)
};
//! Sets `listener`'s Need bits; the callback's item flags follow the union of all listeners.
void set_needs(Listener& listener, std::uint32_t needs) noexcept;
//! The playlist's total length in seconds, from cached track info (no file access). Never sums
//! on the main thread: when unknown or stale it queues a sum on a CPU worker and returns the old
//! value, or a negative number when there is none yet; Change::items follows with the result.
[[nodiscard]] double length_of(std::size_t index) noexcept;

//! Writes the hidden flag to the playlist property and tells every listener.
void set_hidden(std::size_t index, bool hidden) noexcept;
//! Sets or clears PlaylistFlag bits in the playlist property (also with nobody subscribed) and,
//! when the model lives, tells every listener (Change::flags). False if it could not be stored.
bool set_flag(std::size_t index, std::uint32_t flag, bool on) noexcept;
//! The stored PlaylistFlag bits, read from the playlist property (no model needed).
[[nodiscard]] std::uint32_t stored_flags(std::size_t index) noexcept;

} // namespace ept::playlists
