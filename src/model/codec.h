#pragma once

// The stored instance blob (ui_element_config data). Pure code, no SDK: the element adapts SDK
// configs to byte spans, and test/codec_test.cpp exercises everything here offline.
//
// Tag/length/value, little-endian, built for the two directions that break naive blobs: an older
// build reading a newer blob (unknown tags are skipped, and kept verbatim so the next save writes
// them back) and a newer build reading an older blob (missing tags keep their defaults). Nothing
// here fails: a blob reads as far as it is valid, then defaults.
//
//     'E' 'P' 'L' 'T'  uint16 format_version  uint16 min_reader_version
//     then sections until the end:  uint16 tag  uint32 length  bytes[length]
//       1 settings  fields: uint16 id  uint16 length  bytes    (ids in codec.cpp, frozen)
//       2 child     GUID (16 bytes, Data1..3 little-endian)  uint32 n  config[n]
//
// Per-playlist state (hidden) is not here: it is a playlist property (guids::playlist_flags).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <guiddef.h>

#include "settings.h"

namespace ept {

using Bytes = std::vector<std::uint8_t>;

//! A field or section this build does not understand, kept so it survives a save.
struct RawField {
    std::uint16_t id{0};
    Bytes data;
    [[nodiscard]] bool operator==(const RawField&) const = default;
};

//! The hosted element: its GUID and its own configuration.
struct ChildRecord {
    GUID guid{};
    Bytes config;
};

struct InstanceData {
    Settings settings;
    std::vector<RawField> unknown_settings;
    //! False: no child section (a new element; the host picks Playlist View).
    bool has_child{false};
    ChildRecord child;
    std::vector<RawField> unknown_sections;
};

inline constexpr std::uint16_t instance_format_version = 1;

[[nodiscard]] Bytes encode_instance(const InstanceData& data);
//! Throws only std::bad_alloc.
[[nodiscard]] InstanceData decode_instance(std::span<const std::uint8_t> bytes);

//! Playlist property guids::playlist_flags: one uint32 of PlaylistFlag bits, little-endian.
enum PlaylistFlag : std::uint32_t {
    playlist_flag_hidden = 1u << 0,
    //! Locked from the tab menu (playlists/user_lock.h); the lock is reinstalled at startup.
    playlist_flag_locked = 1u << 1,
    //! Pinned to the start / end of the strip (never both): always shown, outside the overflow.
    playlist_flag_pin_start = 1u << 2,
    playlist_flag_pin_end = 1u << 3,
};
inline constexpr std::uint32_t playlist_flag_pins = playlist_flag_pin_start | playlist_flag_pin_end;
[[nodiscard]] Bytes encode_playlist_flags(std::uint32_t flags);
[[nodiscard]] std::uint32_t decode_playlist_flags(std::span<const std::uint8_t> bytes) noexcept;

} // namespace ept
