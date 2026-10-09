<h1 align="center">Enhanced Playlist Tabs (foo_enhancedplaylisttabs)</h1>

<p align="center">
  Fast, customisable playlist tabs for <a href="https://www.foobar2000.org/">foobar2000</a> v2, for
  Default UI and Columns UI. A replacement for the built-in Playlist Tabs.
</p>

<p align="center">
  <img src="docs/images/screenshot_1.png" alt="Enhanced Playlist Tabs above Playlist View, titles showing each playlist's length and a play mark on the playing one" width="800" />
</p>

## Features

- **Works like Playlist Tabs**: one tab per playlist and one element under it (Playlist View,
  NG Playlist or any other element or panel).
- **One row, no stacking.** Tabs that don't fit go to an overflow list, and the active tab always
  stays in view. Titles can shrink to fit first.
- **Strip on any side**, with rotated titles on the left and right if you like.
- **Your look**: underline, pill, tab, outlined tab or text-only indicator, chips, corner radius,
  tab width and alignment, spacing, hover styles (the active tab has its own), a tab font with
  fallbacks, and transparency.
- **Accent colour** from the UI, a custom colour or the playing track's cover, and a strip
  background that can be tinted with it. Colours, fonts and dark mode follow your Default UI or
  Columns UI settings.
- **Titles** from the playlist name or title formatting (track count, length, playing mark...),
  updated live.
- **Playlist commands** in the tab menu: new, rename, duplicate, remove, save, load, lock, hide,
  pin, move and sort, plus foobar2000's track menu for the playlist if you turn it on.
- **Pinned tabs** stay at either end and never go to the overflow.
- **Multi-select** tabs to remove, lock, hide, pin, sort or drag them together.
- **Drag and drop**: reorder tabs, drop files on a tab to add them, or on empty space for a new
  playlist.
- **Your clicks**: pick what clicking the active tab, double-clicking a tab and middle-clicking a
  tab do.
- **Auto-hide**: the strip shows when the pointer reaches the edge.
- **Light on resources**: nothing runs while idle, and a switch or a playlist change repaints only
  the tabs involved, even with hundreds of playlists.

## Install

Requires foobar2000 v2 on Windows 7 or later, 32-bit or 64-bit (the package contains both).
Columns UI is optional.

1. Download `foo_enhancedplaylisttabs.fb2k-component` from the
   [latest release](https://github.com/HongYue1/foo_enhancedplaylisttabs/releases/latest).
2. Double-click it, or in foobar2000 open **Preferences > Components > Install...**, and restart.
3. Add it: in Default UI, enable **View > Layout > Enable layout editing mode**, then right-click >
   **Replace UI Element... > Containers > Enhanced Playlist Tabs** (it comes with Playlist View
   inside). In Columns UI, **Preferences > Display > Columns UI > Layout**, then add
   **Splitters > Enhanced Playlist Tabs** (it comes with NG Playlist inside).

Settings are under **Configure...** in the tab menu (in Columns UI also on the Layout page). They
belong to each element, preview live, and **Cancel** undoes them.

## Keyboard and mouse

| Key / mouse | Action |
| --- | --- |
| Click a tab | Activate its playlist |
| Click the active tab, double-click or middle-click a tab | The action you picked on the Mouse page (none by default) |
| Double-click empty space | New playlist |
| Ctrl+click / Shift+click | Add a tab to or take it out of the selection / select a range |
| Plain click, Esc | Clear the selection |
| Mouse wheel | Previous / next tab |
| Ctrl+Tab / Ctrl+Shift+Tab | Next / previous tab, while the focus is inside the element |
| Drag a tab | Reorder; a selected tab brings the whole selection along; Esc cancels |
| Drop files on a tab / on empty space | Add them to that playlist / new playlist named after the folder |
| Hold a drag over a tab | Switch to it |
| Right-click a tab | Tab menu; on a selected tab, commands for all selected playlists |
| Right-click empty space | New playlist, Load playlist, Sort, Show hidden tab, Appearance, Configure |

Click actions to pick from: show now playing, jump to the first or last track, rename, duplicate,
pin left or right (again to unpin), hide the tab, remove the playlist, lock or unlock.

## Good to know

- **Another element inside**: in Default UI, right-click it in layout editing mode >
  **Replace hosted element...** (Copy / Paste as hosted element work too). In Columns UI, change
  the panel under it in the layout tree.
- **Pinned tabs** only reorder among themselves. **Appearance > Pin icon on pinned tabs** hides
  the pin.
- **Lock playlist** stops tracks from being added, removed or reordered and the playlist from being
  renamed or removed. Locks and hidden tabs are kept with the playlist across restarts.
- **Remove** asks first when the playlist has tracks (you can turn that off). **File > Restore**
  brings a removed playlist back.
- **Hidden tabs** come back from **Show hidden tab** in the menu.
- **Title formatting** has the playlist's fields: `%index%`, `%size%` (or `%list_total%`),
  `%length%`, `%is_active%`, `%isplaying%`, `%ispaused%`, `%playlist_is_playing%` (also after
  stop), `%is_locked%`, `%lock_name%`, `%queue_total%` and `%playlist_queue_total%`. The Titles
  page lists them with examples. Track fields such as `%artist%` are empty, and literal brackets
  need quotes: `%title% '('%size%')'`.
- **Tracks menu**: on the Behaviour page, add foobar2000's track menu to the tab menu as an
  **Items** submenu or an **Items...** entry. With several tabs selected it covers all of them.
- **Transparency** only shows something when your layout draws a background behind the strip,
  such as a Columns UI theme. The auto-hide strip shown over the playlist stays solid.
- **Lighten the text colour** for the hovered active tab does nothing to white text.
- **Columns UI**: colours and fonts are also on the **Colours and fonts** page, under
  *Enhanced Playlist Tabs*. FCL export and import keep the settings and the hosted panel.
- **Performance log**: **Preferences > Advanced > Display > Enhanced Playlist Tabs: log
  performance to the console**. While it is on, the empty-space menu can create and remove 500
  test playlists.

## Building

Visual Studio 2022 or later with the C++ desktop workload and ATL, next to these folders:
`SDK-2026-09-17` (foobar2000 SDK, with the Columns UI SDK inside as `columns_ui-sdk`), `wtl`
(WTL 10) and [`fb2k-common`](https://github.com/HongYue1/fb2k-common).

- `build.bat [Release|Debug] [x64|Win32]` builds the DLL.
- `test\build_tests.bat` builds and runs the tests (no foobar2000 needed).
- `package.bat` builds both platforms into `dist\foo_enhancedplaylisttabs.fb2k-component` (PDBs in
  `dist\symbols`, needs 7-Zip).

The Visual Studio and 7-Zip paths are at the top of the batch files.

## See also

My other components:

- [foo_bettertabs](https://github.com/HongYue1/foo_bettertabs): a tab container for panels and
  elements, where this strip comes from.
- [foo_filetree](https://github.com/HongYue1/foo_filetree): a folder tree panel.
- [foo_onscreendisplay](https://github.com/HongYue1/foo_onscreendisplay): an on-screen display.

## License

[MIT](LICENSE)
