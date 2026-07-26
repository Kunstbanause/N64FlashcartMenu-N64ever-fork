/**
 * @file library.h
 * @brief Pinned library galleries (folders pinned from the browser as extra grid tabs).
 * @ingroup menu
 */

#ifndef LIBRARY_H__
#define LIBRARY_H__

#include <stdbool.h>

#include "bookkeeping.h"
#include "path.h"

#define LIBRARIES_MAX      8    /**< Max number of pinned libraries */
#define LIBRARY_ITEMS_MAX  1024 /**< Max ROMs/disks per library (recursive, flattened) */

/** @brief A single pinned library (a folder rendered as its own grid tab). */
typedef struct {
    char       name[32];   /**< Tab label, defaults to the folder's basename */
    path_t    *path;       /**< Pinned folder */
    bookkeeping_item_t *items; /**< NULL until first visit (scan-once-per-boot) */
    int        count;      /**< Number of items in `items` */
    bool       scanned;    /**< True once library_scan() has populated `items` this boot */
} library_t;

/**
 * @brief Set the path to libraries.ini. Call once during menu_init, before library_load().
 *
 * @param ini_path Path to libraries.ini
 */
void library_init(const char *ini_path);

/** @brief Load the pinned-library list (name + path only -- items scan lazily). */
void library_load(void);

/** @brief Persist the pinned-library list (name + path only) to libraries.ini. */
void library_save(void);

/** @brief Free all libraries' scanned items and paths. Call from menu_deinit. */
void library_deinit(void);

/** @brief Number of pinned libraries. */
int library_count(void);

/**
 * @brief Get a pinned library by index.
 *
 * @param i Index in [0, library_count())
 * @return Pointer to the library, or NULL if out of range
 */
library_t *library_get(int i);

/**
 * @brief Pin a folder as a new library.
 *
 * @param name   Tab label (copied; truncated to fit)
 * @param folder Folder to pin (copied; caller retains ownership of `folder`)
 * @return false if already at LIBRARIES_MAX or the folder is already pinned
 */
bool library_add(const char *name, path_t *folder);

/**
 * @brief Unpin (and free) the library at index i, compacting the list.
 *
 * @param i Index in [0, library_count())
 */
void library_remove(int i);

/**
 * @brief Find the index of a pinned library by its folder path, or -1 if not pinned.
 *
 * @param folder Folder to look up
 */
int library_find_by_path(path_t *folder);

/**
 * @brief Recursively (and lazily) scan a library's folder into `items`, flattened.
 *
 * Filters with the same rules as the browser's folder-favorite scan: accepts
 * n64_rom_extensions/disk_extensions, rejects hidden files (incl. macOS AppleDouble
 * sidecars). Safe to call again for a manual rescan (frees the previous item set first).
 *
 * @param lib Library to scan
 */
void library_scan(library_t *lib);

/** @brief Free a library's scanned item array (paths included) without unpinning it. */
void library_free_items(library_t *lib);

#endif // LIBRARY_H__
