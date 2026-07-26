#ifndef FS_FILTER_H__
#define FS_FILTER_H__

/**
 * @file fs_filter.h
 * @brief ROM/disk extension tables and hidden-file filtering shared by the browser's
 *        folder-favorite scan and the library module's recursive folder scan.
 *
 * Kept as a single header (rather than a .c + extern) so both call sites compile the
 * exact same table text -- there is no second copy that can silently drift out of sync.
 */

#include <string.h>

#include "path.h"
#include "../utils/fs.h"

static const char *n64_rom_extensions[] = { "z64", "n64", "v64", "rom", NULL };
static const char *disk_extensions[]    = { "ndd", NULL };

static const char *hidden_root_paths[] = {
    "/menu.bin",
    "/menu",
    "/N64FlashcartMenu.n64",
    "/ED64",
    "/ED64P",
    "/sc64menu.n64",
    // Windows garbage
    "/System Volume Information",
    // macOS garbage
    "/.fseventsd",
    "/.Spotlight-V100",
    "/.Trashes",
    "/.VolumeIcon.icns",
    "/.metadata_never_index",
    NULL,
};

struct fs_filter_substr { const char *str; size_t len; };
#define FS_FILTER_SUBSTR(str) ((struct fs_filter_substr){ str, sizeof(str) - 1 })

static const struct fs_filter_substr hidden_basenames[] = {
    FS_FILTER_SUBSTR("desktop.ini"), // Windows Explorer settings
    FS_FILTER_SUBSTR("Thumbs.db"),   // Windows Explorer thumbnails
    FS_FILTER_SUBSTR(".DS_Store"),   // macOS Finder settings
};
#define HIDDEN_BASENAMES_COUNT (sizeof(hidden_basenames) / sizeof(hidden_basenames[0]))

static const struct fs_filter_substr hidden_prefixes[] = {
    FS_FILTER_SUBSTR("._"), // macOS "AppleDouble" metadata files
};
#define HIDDEN_PREFIXES_COUNT (sizeof(hidden_prefixes) / sizeof(hidden_prefixes[0]))

static bool path_is_hidden (path_t *path) {
    char *stripped_path = strip_fs_prefix(path_get(path));

    // Check for hidden files based on full path
    for (size_t i = 0; hidden_root_paths[i] != NULL; i++) {
        if (strcmp(stripped_path, hidden_root_paths[i]) == 0) {
            return true;
        }
    }

    char *basename = file_basename(stripped_path);
    size_t basename_len = strlen(basename);

    // Check for hidden files based on filename
    for (size_t i = 0; i < HIDDEN_BASENAMES_COUNT; i++) {
        if (basename_len == hidden_basenames[i].len &&
            strncmp(basename, hidden_basenames[i].str, hidden_basenames[i].len) == 0) {
            return true;
        }
    }

    // Check for hidden files based on filename prefix
    for (size_t i = 0; i < HIDDEN_PREFIXES_COUNT; i++) {
        if (basename_len > hidden_prefixes[i].len &&
            strncmp(basename, hidden_prefixes[i].str, hidden_prefixes[i].len) == 0) {
            return true;
        }
    }

    return false;
}

#endif // FS_FILTER_H__
