# Changelog

## N64ever — based on N64FlashcartMenu V0.3.2

This is the **N64ever** release of N64FlashcartMenu — a fork of upstream **V0.3.2** that
reworks the front-end, bringing:

- a **cover-art, Grid-based GUI** — a Favorites Grid as the home screen, not a file list
- **pinned library tabs** — pin any folder from the File Browser to give it its own tab on the
  grid (`L`/`R` to switch), alongside Favorites; read-only galleries with automatic filename
  captions to tell apart ROM hacks that share their base game's box art
- a breadth of **game-metadata support**, including a filename-keyed box-art fallback for
  homebrew/ROM hacks with no matching Game-Code art directory
- a **unified popup UI** — Inspect, a universal "More" menu, and full file management
  (Copy/Move/Rename/Create-folder via an on-screen keyboard), all over the grid
- **64DD disc linking**, an idle **cover-art screensaver**, **ROM boot** countdown, new font and much more

**Controls note:** the universal "More"/options menu is on `Z` everywhere (moved off `R` to make
room for `L`/`R` grid-tab switching); `L` alone is the remaining `L`/`Z` context actions
(combined-boot, save-changes, etc.).

**Read more in the docs:** **[README.md](README.md)**. Build notes: **[BUILD.md](BUILD.md)**

### Based on N64FlashcartMenu V0.3.2
- Upstream **V0.3.2** release notes: <https://github.com/Polprzewodnikowy/N64FlashcartMenu/releases/tag/V0.3.2>
- Upstream project & full upstream changelog: <https://github.com/Polprzewodnikowy/N64FlashcartMenu>

Licensed under the GNU AGPL-3.0 — see [LICENSE.md](LICENSE.md) and [NOTICE](NOTICE).
