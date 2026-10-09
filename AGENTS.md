# AGENTS.md - foo_enhancedplaylisttabs

Notes for agents working on this component. See the workspace AGENTS.md one level up for the
general rules (file tools, builds through `cmd //c`, v145 toolset, background jobs).

## Build, test, release

- Build: `cmd //c build.bat Release x64` and `cmd //c build.bat Release Win32`. Read
  `build.log` / `build-Win32.log`; 0 warnings is required (/W4 /WX). The build fails on
  post-Windows 7 imports and on any Columns UI import on purpose (the DLL must load without
  Columns UI). `build.bat` also builds `columns_ui-sdk-public` (its plain Release is /MT).
- Tests: `cmd //c test\build_tests.bat` (codec, strip layout, strip render, z-order, keyed
  model, title fields, playlist sort). Results in `test/tests.out`; the last line must be
  `EXIT=0 0 0 0 0 0 0`.
- Cover colour and contrast (OKLab, APCA) are in the shared `../fb2k-common` library, with its
  own tests (`fb2k-common/test/build_tests.bat`, including a golden test over the user's
  covers). Change them there; Better Tabs, Media Bar and foo_onscreendisplay use the same code.
- Dialogs: `cmd //c "..\foobar2000-component-dev\scripts\dialog_check.bat foo_enhancedplaylisttabs.rc"`
  after every layout change; 0 problems required. Every page is 300 x 218 DU, the size of the host
  placeholder (`IDC_PAGE_HOST`): a taller page is clipped at runtime.
- Package: `cmd //c package.bat` -> `dist/foo_enhancedplaylisttabs.fb2k-component` (x86 at the
  root, x64 in `x64/`) and `dist/symbols/*.pdb`.
- Release: bump `src/version.h`, package, archive the PDBs as
  `../.archive/foo_enhancedplaylisttabs-<version>-symbols.zip`, commit, tag `v<version>`, push, and
  `gh release create` with the .fb2k-component attached. Only when the user says so.
- foobar2000 cannot be run from here: the user tests the DLL in their install. Never claim
  something works because it compiled.

## Things to know

### Include order

`<helpers/foobar2000+atl.h>` must be the first SDK include in every .cpp (before anything that
pulls in `<windows.h>`), or winsock definitions clash.

### Two hosts, one core

`SwitcherCore` (`src/hosts/switcher_core.*`) is UI-neutral; `dui_element.cpp` and
`cui_container.cpp` only supply the child, colours, fonts, keys and visibility through the
`host_*` hooks. Keep UI calls out of the core. The Columns UI container is a
`uie::splitter_window_v3` with `get_maximum_panel_count() == 1`; both hosts store the same
`InstanceData` blob (settings + one child record). Colour/font client changes fan out to every
live core, Default UI ones included (harmless). Use the `columns-ui-sdk` skill for SDK details.

The hosted child is shown with `ShowWindow`, never `SWP_SHOWWINDOW` (a Columns UI splitter child
stays empty otherwise), and `fill_background` paints `HostColours::layout` for a child's DC so
splitter dividers match Columns UI. Both ported from foo_bettertabs (see its AGENTS.md); not yet
verified at run time here.

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
- Do not initialise a `static const std::wstring` from `cond ? std::wstring(a) : std::wstring(b)`:
  under MSVC it came out empty (`pin_glyph`). Construct from the `const wchar_t*` instead.

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

- Dark popup menus cannot draw `MF_MENUBARBREAK` columns (they turn light). Long lists use
  `append_long_list` (submenus of 25).
- `MessageBox` cannot be dark: use `run_confirm_dialog` (CDarkModeHooks).
- Title formatting: `(`, `)`, `[`, `]`, `,` are syntax outside quotes. Examples must quote literal
  ones: `%title% '('%size%')'`.

### Auto-hide windows must stay the topmost children

The hot zone and the overlay strip only work while they are above the hosted element's window in
our z-order. `SetParent` back into the container and a child's own `SetWindowPos(HWND_TOP)` put it
above them without telling us: Windows sends no message and no `EVENT_OBJECT_REORDER`
(`test/zorder_test.cpp`). The core watches `EVENT_OBJECT_PARENTCHANGE` (one out-of-context hook
per process, only while a hot zone exists) and re-raises in `WM_SETCURSOR` as a fallback. Layered
children need a Windows 8+ manifest in a test exe (`test/compat.manifest`).

### Performance checks

Turn on Preferences > Advanced > Display > "Enhanced Playlist Tabs: log performance to the
console". The strip-space menu then has "Perf: create 500 test playlists" / "Perf: remove the
test playlists" (one summary line each; the per-event lines are suppressed during the bulk).
Numbers from the user's runs are in the README.

### Dialog labels in dark mode

Static text is drawn on a transparent background in dark mode. Change a label's text or enabled
state only through `set_label` / `enable` in `configure_dialog.cpp`: they skip no-op changes and
erase the page behind the control first (`repaint_behind`). A plain `SetWindowText` or
`EnableWindow` piles the new text on the old, which looks bold and fringed (fixed in 1.2.3).

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
  selection, the active fill). `active_hover_lighten` mixes the title towards white in OKLab
  before the contrast checks. The fade is shared.
- The Hover page edits one set at a time ("Settings for:"): switching it reloads the shared
  controls without reading them first (`hover_to_controls`); the title row swaps the others'
  combo for the active tab's Lighten box (`NOT WS_VISIBLE`, dialog_check skips hidden controls).
- The fade keeps a `hover_level` per `Item` (so it moves with reorders) and is read only while
  `hover_fading_`; otherwise `index == hover_` decides. Anything that resets `hover_` on an item
  change must call `stop_hover_fade()`.
- `render_test` `hover_test` checks each style by pixels and writes `test/out/hover_96.png`. In
  tests, pump only `WM_TIMER` for the strip: the real pointer is elsewhere, and a posted
  `WM_MOUSELEAVE` ends the hover.
- Pages are `IDD_PAGE_STRIP + i`, so the ids stay consecutive: Strip, Look, Hover, Colours, Fonts,
  then the rest.
- `save_png` in `render_test` writes 24 bpp BGR: WIC's PNG encoder turns a 32 bpp request into
  24 bpp, and the 32 bpp rows it was fed before scrambled every image in `test/out`.
