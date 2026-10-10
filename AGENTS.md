# AGENTS.md - foo_enhancedplaylisttabs

## Build, test, package

- Build: `cmd //c build.bat Release x64` and `cmd //c build.bat Release Win32`. Read
  `build.log` / `build-Win32.log`; 0 warnings (/W4 /WX). The build fails on post-Windows 7
  imports and on any Columns UI import on purpose (the DLL must load without Columns UI).
  `build.bat` also builds `columns_ui-sdk-public` (its plain Release is /MT).
- Tests: `cd test && cmd //c build_tests.bat` (codec, strip layout, strip render, z-order, keyed
  model, title fields, playlist sort, child min/max). The last line of `test/tests.out` must be
  `EXIT=0 0 0 0 0 0 0 0`.
- Dialogs: run the dialog check on `foo_enhancedplaylisttabs.rc` after every layout change; 0
  problems. Every page is 300 x 218 DU, the size of `IDC_PAGE_HOST`: a taller page is clipped.
- Package: `cmd //c package.bat` -> `dist/foo_enhancedplaylisttabs.fb2k-component` (x86 at the
  root, x64 in `x64/`) and `dist/symbols/*.pdb`. Version in `src/version.h`.
- Cover colour and contrast come from `../fb2k-common` (shared with Better Tabs, Media Bar and
  foo_onscreendisplay); change them there.
- Better Tabs shares the strip, settings, Configure dialog and hosts (`SwitcherCore` here,
  `TabsCore` there; keyed here, index-based there). Codec field ids differ; each keeps its own.

## Things to know

### Two hosts, one core

`SwitcherCore` (`src/hosts/switcher_core.*`) is UI-neutral; `dui_element.cpp` and
`cui_container.cpp` only supply the child, colours, fonts, keys and visibility through the
`host_*` hooks. Keep UI calls out of the core. The Columns UI container is a
`uie::splitter_window_v3` with `get_maximum_panel_count() == 1`; both hosts store the same
`InstanceData` blob (settings + one child record). Colour/font client changes fan out to every
live core, Default UI ones included (harmless).

The hosted child is shown with `ShowWindow`, never `SWP_SHOWWINDOW` (a Columns UI splitter child
stays empty otherwise), and `fill_background` paints `HostColours::layout` for a child's DC so
splitter dividers match Columns UI. Both ported from Better Tabs.

### The playlists model

- `src/playlists/playlist_model.cpp` owns the **only** `playlist_callback` in the process, alive
  while at least one element has a window. Elements are `playlists::Listener`s. Do not register
  another callback in an element.
- Item events are off by default. `playlists::set_needs` turns them on (`modify_callback`) only
  while a title shows `%size%` / `%length%`; `Change::items` is coalesced by key per main-thread
  turn.
- Every playlist has an `Entry::key` that follows it through reorders. The strip and the labels
  (`SwitcherCore::labels_`) are keyed by it, which is what makes one created playlist among 500
  build one text layout. Keep new per-playlist state keyed, not indexed.
- Deferred work (`fb2k::inMainThread`, CPU worker results) checks the model generation: keys
  restart when the model is recreated.
- `%length%` is summed on a CPU worker; the stale value stays until the new sum lands, and
  `items_version` discards sums for tracks that changed meanwhile. Never sum on the main thread
  (a cold sum of 2477 tracks took 80 ms at start-up).
- Hidden / locked flags live in a playlist property (`guids::playlist_flags`), so File > Restore
  and saved playlists keep them. Our lock is reinstalled at start-up and when a playlist is
  restored, deferred out of `on_playlist_created` (installing a lock re-enters the callback).
- Pins are flags too (`playlist_flag_pin_start` / `_pin_end`). `SwitcherCore::collect_visible`
  groups the strip as start pins, unpinned, end pins (so strip order is not playlist order when
  pins exist); `StripLayout` keeps pins outside the overflow window. Each group is in playlist
  order, so the incremental created / removed / flags paths stay incremental with pins
  (`group_insert_pos`, `insert_visible`, `erase_visible`); a pin change moves the tab by key.
- A drag starting on a selected tab is a block drag (`begin_block_drag`): the strip gathers the
  group's selected tabs, moves them as one, and reports `on_strip_reorder_block` with indices of
  the order before the drag; a cancel restores that order by key (`drag_keys_`).
- Sorting by length sums on `fb2k::inCpuWorkerThread`; `apply_sort` runs on the main thread and
  drops the result if the keys or positions changed meanwhile.
- `pin_glyph`: construct the `static const std::wstring` from a `const wchar_t*`, not from a
  `cond ? std::wstring(a) : std::wstring(b)` (came out empty under MSVC).

### Multiple selection (strip)

- The Shift+click anchor is the tab last clicked, reset to the active tab whenever the active
  tab changes by key (`StripWindow::sync_anchor`, called from `set_active` and
  `end_item_change`). The host's own `set_active` after a click resets it to the clicked tab,
  which is the same tab.
- Ctrl/Shift+click takes the keyboard focus (for Esc) and remembers where it came from;
  `clear_selection` gives it back (`release_selection_focus`), `WM_KILLFOCUS` ends the
  selection. `clear_marks` unmarks without moving the focus (a Shift+click replacing its range).
- The active tab in the selection is outlined regardless of `UISF_HIDEFOCUS`; without a
  selection the outline is only the keyboard focus cue.
- `test/render_test.cpp` `selection_test` covers the anchor, focus loss and a lost press.

### Menus and dialogs

- "Lock all playlists" / "Unlock all playlists" (tab menu and empty-space menu, `cmd_lock_all`,
  `cmd_unlock_all`): two entries, each greyed when there is nothing to do. One toggle (checked
  only when all were locked) could not unlock a set with one unlocked playlist (user report).
  Playlists with another component's lock are skipped. New playlists are not locked.
- The Fonts page's Default shows the host size read back from whole pixels with
  `tenths_from_pixels` (`src/model/font_size.h`): 11 px at 96 DPI is 8 pt, not 8.3.

- The Configure dialog is modeless when the element has a window (tab menu, Default UI edit
  menu): `run_configure(parent, true)` → `open_configure_dialog`, so the strip can be hovered and
  used while it is open. It owns its state copy, is always live, registers with
  `modeless_dialog_manager` and deletes itself; OK / Cancel (or its owner closing) reach
  `SwitcherCore::configure_closed`, which applies the result or `configure_original_`.
  `configure_wnd_` pins an auto-hidden strip, makes a second Configure focus the open dialog and
  greys the Appearance submenu (its changes would be lost on OK / Cancel). `on_destroy` closes the
  dialog quietly (`close_configure_dialog`, no callback). Columns UI's Layout page stays modal
  (`show_config_popup`, it reads `get_config` after).
- Long menu lists use `append_long_list` (submenus of 25; dark menus can't draw
  `MF_MENUBARBREAK` columns).
- Confirmations use `run_confirm_dialog` (`MessageBox` can't be dark).
- Title examples quote literal `(` `)` `[` `]` `,`: `%title% '('%size%')'`.

### Auto-hide windows must stay the topmost children

The hot zone and overlay strip must stay above the hosted element's window. `SetParent` back into
the container and a child's own `SetWindowPos(HWND_TOP)` bury them with no message and no
`EVENT_OBJECT_REORDER`. The core watches `EVENT_OBJECT_PARENTCHANGE` (one out-of-context hook per
process, only while a hot zone exists) and re-raises in `WM_SETCURSOR` as a fallback.
`test/zorder_test.cpp` needs `test/compat.manifest` (layered children need Windows 8+).

### Performance checks

Turn on Preferences > Advanced > Display > "Enhanced Playlist Tabs: log performance to the
console". The strip-space menu then has "Perf: create 500 test playlists" / "Perf: remove the
test playlists" (one summary line each; the per-event lines are suppressed during the bulk).
Measured numbers are in the README.

### Dialog labels in dark mode

Change a label's text or enabled state only through `set_label` / `enable` in
`configure_dialog.cpp` (`repaint_behind`); plain `SetWindowText` / `EnableWindow` piles text up
in dark mode (fixed in 1.2.3).

### Transparent background

- The backdrop cache is refetched on `WM_ERASEBKGND`, moves, size and theme-background changes,
  and on any `WM_PAINT` whose rectangle is not inside what the strip invalidated itself
  (`StripWindow::invalidate` keeps `own_dirty_`; never call `InvalidateRect(wnd_, ...)` directly
  in the strip). So a host that repaints its background and invalidates its children without
  `RDW_ERASE` is picked up (forum video: Panel Stack Splitter showed the old cover's background
  until a hover). If a host never invalidates its children, the strip cannot know.
- `transparent_opacity` (Colours page, 0 = fully transparent): the strip's background at that
  alpha over the backdrop, one `FillRectangle` of the dirty rect in `render`.
- `render_test` `transparent_test` covers the reuse, the refetch without an erase (real
  `WM_PAINT`s: the parent is shown layered at alpha 1), the opacity and the text colours.

### Size limits (no child panel)

- `panel_limits.h` (`layout_test`): with no hosted panel the strip's thickness is both the minimum
  and the maximum along it, as Columns UI's own Playlist tabs report with no child. Up to 1.4
  the maximum stayed open and Panel Stack Splitter gave the element more height than the strip
  (forum report, not reproduced here: PSS is not installed).
- `minmax_test`: Windows does not clamp a child to its own `WM_GETMINMAXINFO`, so a taller
  element means the host honoured the limits we reported.

### Mouse actions (Mouse page)

- `Settings::click_active_action`, `dblclick_action`, `middle_action` share `TabAction`;
  `SwitcherCore::run_tab_action` runs them. The codec still writes the 1.4 ids (`s_middle_click`,
  `s_dblclick_tab`) with mapped values (0 for actions 1.4 lacks) before the new ids, and reads
  them into the new fields; the new ids come later in the stream and win.
- The strip arms the click-on-active action on a plain press of the tab that was already active
  (not a selection-clearing click, not the second press of a double click) and runs it on
  release over the same tab without a drag. With a double-click action set it waits
  `GetDoubleClickTime()` (`click_timer`); `WM_LBUTTONDBLCLK` cancels it. `model_test`
  `click_test` covers the timing.

### Tracks context menu (tab menu "Items")

- `Settings::tracks_menu`. Submenu: an empty popup; `TrackPopupMenu` runs without
  `TPM_NONOTIFY` only then, and `core_message` fills it on its `WM_INITMENUPOPUP`
  (`fill_items_menu`: `contextmenu_manager::init_context_ex(..., caller_playlist_manager)`,
  `win32_build_menu` with ids from `menu_items_base`). The manager lives until the menu returns,
  then `execute_by_id`. On demand: `win32_run_menu_popup` after the tab menu closed. The perf log
  times the build. Not testable offline (needs foobar2000).

### Hover styles (Hover page)

- `Settings::hover_*` style the tabs other than the active one, `active_hover_*` the active tab
  (`StripWindow::hover_mark(active)`). `HoverStyle::plain` (active only, the default) is the old
  wash folded into the tab's own fill, so it shows nothing on a pill or tab indicator; `clamp`
  turns it into `fill` for the others. `StripWindow::draw_tab` draws the mark (fill, outline via
  `fill_shape`, which also strokes pills now, or underline) over the tab's own fill (chip,
  selection, the active fill). The fade is shared.
- The title of a hovered tab: `hover_text` / `active_hover_text` (`HoverText`: brighten,
  unchanged, the hover colour, a custom colour in `*_hover_text_argb`). Brighten is "to the full
  text colour" for the others and "lighter in OKLab, towards white" for the active tab. Up to 1.6
  the active tab had a bool (`s_active_hover_lighten`); the codec still writes it (brighten = 1)
  before `s_active_hover_text`, which wins.
- Text colours (Colours page): `custom_text` / `text_argb` for the other tabs (else the theme's
  text dimmed), `custom_active_text` / `active_text_argb` for the active tab and, when set, the
  selected tabs. Picked colours (these and the custom hover title) skip the strong-fill contrast
  rescue in `draw_tab` (`chosen`): the user asked for that colour.
- Automatic fill strength (`accent_strength` 0) for a pill or tab is `auto_fill_dark` /
  `auto_fill_light` (`settings.h`, 50 and 40 %; was 30 and 26). Both are at or above
  `strong_fill`, so the active title is checked for contrast against the fill. The Look page's
  slider rests on the matching value (`ConfigureState::dark`). Tests that need the title as drawn
  set `accent_strength` below 40 (the render tests pass it to `StripTheme::active_fill`).
- The Hover page shows both sets side by side ("Other tabs" | "The active tab"): one control
  set each (`HoverSet` in `hover_sets`: `HoverFields` + `HoverIds`, `IDC_HOVER_*` and
  `IDC_HOVER_ACTIVE_*`); `hover_to_controls` / `hover_from_controls` / `hover_values` /
  `hover_enabled` run per set. The fade is shared by both sets: the first row, above the columns.
- Chips (Look page): `chip_colour` (`ChipColour`: neutral = the text colour, accent = whatever
  the accent source gives, the cover's colour too, custom = `chip_argb`) and `chip_strength`
  (0 = `auto_chip_strength`, 5 %; else 2-60). `StripWindow::chip_fill` / `chip_fill_alpha`; from
  `strong_fill` on the title is checked for contrast against the chip unless it is a picked colour.
- The fade keeps a `hover_level` per `Item` (so it moves with reorders) and is read only while
  `hover_fading_`; otherwise `index == hover_` decides. Anything that resets `hover_` on an item
  change must call `stop_hover_fade()`.
- `render_test` `hover_test` checks each style by pixels and writes `test/out/hover_96.png`. In
  tests, pump only `WM_TIMER` for the strip: the real pointer is elsewhere, and a posted
  `WM_MOUSELEAVE` ends the hover.
- Pages are `IDD_PAGE_STRIP + i`, so the ids stay consecutive: Strip, Look, Hover, Colours, Fonts,
  then the rest.
- `save_png` in `render_test` writes 24 bpp BGR (WIC's PNG encoder turns 32 bpp into 24 bpp).

## Performance

- **One** playlist callback per process, however many elements exist, registered with
  playlist-level events only. Track events are added only while a title shows `%size%` or
  `%length%`, and are coalesced to one update per playlist per message-loop turn.
- A switch only calls `set_active_playlist` and repaints two tabs. The hosted element is not moved,
  resized or repainted by the strip.
- Creating, removing, renaming, hiding or moving a playlist updates one tab: tabs are keyed by
  playlist, so the other tabs keep their text layouts.
- `%length%` is summed on a CPU worker from cached track info (no file access), so it never holds
  up start-up or the UI.
- No timers, hooks or polling while idle. Auto-hide is event driven; a timer runs only while a delay
  is due, an animation plays or a drag hovers a tab.
- Only the strip is painted: double-buffered, dirty rectangles only, and no heap allocations in
  `WM_PAINT` (checked by the render test).
- Measured with 509 playlists (foobar2000 2.26, x64): playlists read in 17 ms at start-up; one
  playlist created in 0.2 ms (1 text layout built); one removed in 0.02 ms (none built); 500 removed
  in 25 ms; a switch 0.5 ms on the strip's side, warm strip paints under 1.6 ms with no
  allocations.

## Tests

`test\build_tests.bat` builds and runs these tests:

- `codec_test`: settings survive a round trip, fields from newer versions are kept, damaged data
  falls back to defaults; safe file names for Save.
- `layout_test`: tab positions for each width mode and alignment, and overflow.
- `render_test`: renders the strip offline, times it, counts allocations in the paint path and
  writes PNGs to `test\out\`.
- `zorder_test`: the window-manager behaviour auto-hide relies on (what does and does not
  report a child moving above the hot zone).
- `model_test`: keyed strip updates with 500 tabs (one created builds one layout; removals, moves
  and hide/show build none).
- `title_test`: which fields a title script uses, and the `%length%` text.
- `sort_test`: playlist sorting.
- `minmax_test`: how Windows and a host treat a child's min/max size info (no clamping).

The cover colour and contrast code has its own tests in `fb2k-common\test\`.

## Source map

| File | Job |
| --- | --- |
| `src/component.cpp` | Component identity, DirectWrite warm-up |
| `src/hosts/dui_element.cpp` | The Default UI container element and its hosted element |
| `src/hosts/cui_container.cpp` | The Columns UI container, its hosted panel, colours and fonts clients |
| `src/hosts/switcher_core.cpp` | Tabs, switching, menus, drag and drop, auto-hide |
| `src/hosts/strip_drop.cpp` | OLE drop target for the strip and the hot zone |
| `src/hosts/configure_dialog.cpp` | The Configure dialog, Rename and the remove confirmation |
| `src/playlists/playlist_model.cpp` | The one playlist callback, per-playlist flags, `%length%` sums |
| `src/playlists/playlist_title.cpp` | The title formatting fields |
| `src/playlists/user_lock.cpp` | Lock playlist |
| `src/model/` | Settings and their storage, title-field inspection, file names; `colour.h` and `cover_accent.h` forward to `fb2k-common` |
| `src/strip/strip_window.cpp` | The strip: drawing, input, tooltips, keyed item updates |
| `src/strip/strip_layout.cpp` | Tab positions and overflow |
| `src/strip/hot_zone.cpp` | The auto-hide hot zone |
| `src/platform/` | Drawing, cover loading and decoding, timing and logging |

Settings are stored in a versioned format that keeps fields it does not know, so adding an option
does not break an existing layout. Per-playlist state (hidden, locked) is stored in a playlist
property, so it travels with the playlist.

### Title fields

- `%is_playing%` / `%isplaying%` are on only while playback runs or is paused from that playlist;
  `%playlist_is_playing%` keeps the old meaning: `get_playing_playlist()`, which foobar2000 keeps
  after stop (checked in foobar2000 2.x). `%list_total%` is `%size%`, as foobar2000's own
  `%list_total%` is the track count of the playlist. Not offered: `%list_index%`, `%queue_index%`
  (track-only) and `%playback_time%` (would relabel every second).
- `SwitcherCore::on_playback` relabels at most two tabs, and only when the playing playlist or
  the playing/paused state changes (new tracks and stream titles in the same state do nothing).
- Queue fields: `QueueWatch` (a `playback_queue_callback` service, always registered) coalesces a
  burst of changes into one main-thread call. Only cores whose title uses a queue field read the
  queue; counts are kept per playlist key. A changed `%queue_total%` relabels every tab (unchanged
  texts keep their layouts); otherwise only the playlists whose count changed.
- Counts (`%queue_total%`, `%playlist_queue_total%`) are empty at 0, so `$if()` works.
- Old names keep working but are not listed (Titles page, README): `%is_playing%` (= `%isplaying%`),
  `%playlist_size%`, `%playlist_duration%`. Only foobar2000's name is shown where one exists.
- Titles page: the pattern field is multi-line (wraps; line breaks are dropped when read), and
  `fit_dropped_width` widens the examples list to its longest item.
- `render_test` draws the example marks (play, two U+275A, lock) in the "title marks" rows.
