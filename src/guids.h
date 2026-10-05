#pragma once

// Every GUID this component owns. Fresh (generated 2026-10-05), never copied from a sample or from
// foo_bettertabs, so both components install side by side. Changing one breaks every saved layout
// or setting that contains it, so these are frozen once shipped.

#include <guiddef.h>

namespace ept::guids {

//! The Default UI element's identity. Stored in every Default UI layout that uses it.
inline constexpr GUID dui_element = {0x65b941dd, 0x3451, 0x472d, {0xae, 0x0b, 0x7a, 0xec, 0xfc, 0xcf, 0x6c, 0x4b}};

//! Playlist property holding our per-playlist flags (hidden from the strip). Shared by every
//! instance, since a playlist property belongs to the playlist.
inline constexpr GUID playlist_flags = {0xe7d38304, 0x9367, 0x4e7c, {0x94, 0xa6, 0xc1, 0xd8, 0x24, 0x6f, 0xef, 0x03}};

//! Advanced preferences: Display > Enhanced Playlist Tabs: log performance.
inline constexpr GUID advconfig_perf = {0xba2bd2fa, 0xb11d, 0x473b, {0xbf, 0x3b, 0xae, 0xcc, 0xaf, 0xf1, 0xec, 0x9c}};
//! Advanced preferences: wrap tab switches in WM_SETREDRAW (an experiment, off by default).
inline constexpr GUID advconfig_setredraw = {0xcd6d3102, 0x8da4, 0x43ee, {0xa9, 0x83, 0x65, 0x5f, 0x82, 0xf8, 0xc9, 0xbf}};

// Columns UI.
//! The Columns UI container (uie::window).
inline constexpr GUID cui_container = {0x45cbc271, 0x7b79, 0x4c57, {0xae, 0xca, 0x99, 0x22, 0x63, 0xf2, 0x84, 0x17}};
//! The host handed to its panel.
inline constexpr GUID cui_host = {0x3a8c0b8d, 0xe1b1, 0x4839, {0xb2, 0x24, 0xdc, 0x09, 0xc8, 0xcc, 0x2c, 0x9a}};
//! Its entry on Columns UI's Colours page.
inline constexpr GUID cui_colour_client = {0x4fc9cd61, 0x886a, 0x4d0d, {0x8f, 0x59, 0xb9, 0xf5, 0x00, 0x3a, 0x9f, 0xf3}};
//! Its entry on Columns UI's Fonts page.
inline constexpr GUID cui_font_client = {0xbe8e020d, 0xf4a0, 0x4604, {0xbf, 0x96, 0x97, 0xc9, 0xb2, 0xb0, 0xb1, 0x8e}};

} // namespace ept::guids
