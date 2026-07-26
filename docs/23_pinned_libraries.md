## Pinned Libraries

Favorites is a single flat list. If you keep ROM hacks, homebrew, or personal builds alongside
retail games, they all pile into the same grid. **Pinned libraries** let you pin a folder from the
File Browser so it shows up as its own tab on the games grid, right next to Favorites.

### Tabs on the games grid

`L` and `R` cycle through the tabs on the games grid: Favorites, then each pinned library, in the
order you pinned them. If you haven't pinned anything yet, the second tab is a placeholder
explaining how to pin one — there are always at least two tabs to switch between.

The File Browser is **not** one of these tabs. Reach it from any tab via `Z` → **File Browser**.

### Pinning a folder

1. Open the File Browser (`Z` → **File Browser** from the grid).
2. Navigate to (or highlight) the folder you want to pin.
3. Press `Z` to open its menu, and choose **Pin as Library**.
4. Switch to the grid and cycle with `L`/`R` to find the new tab.

The same menu row toggles to **Unpin Library** once a folder is pinned. Pinned folders (name and
path only) are saved to `sd:/menu/n64ever/libraries.ini` and restored automatically on the next boot.

A library's contents are scanned once per boot, the first time you switch to its tab (recursively,
flattened — every ROM/disk in the folder and its subfolders, filtered the same way Favorites'
"Fav inside folder" already is). If you add or remove ROMs on the SD card while the menu is
running, use **Rescan library** in the grid's `Z` menu to pick up the change without rebooting.

### Library tabs are read-only galleries

A library tab shows whatever is in the pinned folder — it isn't a list you reorder or curate from
the grid. Sort A-Z, Clear all, Always-sort-A-Z, and move mode (hold `B`) are all Favorites-only and
hidden on a library tab. The Favorite/Unfavorite row becomes **Add to Favorites** instead, so you
can still promote something from a library into your real Favorites list without it affecting the
library itself.

### Duplicate box art (ROM hacks)

Many ROM hacks correctly match their base game's box art, so several hacks of the same game can
look identical in a library tab. To tell them apart, library tabs always show the ROM's filename as
a caption under its cover art. Favorites keeps its clean art-only look by default; if you'd like the
same captions there too, turn on **Favorites captions** in the grid's `Z` → **Grid settings** menu.

### Homebrew / no-art ROMs

See [Game Art Images](./19_gamepak_boxart.md) for how box art is resolved, including the
filename-keyed fallback under `sd:/menu/metadata/homebrew/` for ROMs (homebrew, hacks, or anything
else) with no matching Game-Code art directory.
