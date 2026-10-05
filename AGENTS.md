# AGENTS.md - foo_enhancedplaylisttabs

Notes for agents working on this component. See the workspace AGENTS.md one level up for the
general rules (file tools, builds through `cmd //c`, v145 toolset, background jobs).

## Build, test, release

- Build: `cmd //c build.bat Release x64` and `cmd //c build.bat Release Win32`. Read
  `build.log` / `build-Win32.log`; 0 warnings is required (/W4 /WX). The build fails on
  post-Windows 7 imports on purpose.
- Tests: `cmd //c test\build_tests.bat` (codec, strip layout, cover accent, strip render,
  z-order, keyed model, title fields). Results in `test/tests.out`; the last line must be
  `EXIT=0 0 0 0 0 0 0`.
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
