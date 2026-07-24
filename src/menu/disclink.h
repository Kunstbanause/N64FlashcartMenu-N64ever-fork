/**
 * @file disclink.h
 * @brief 64DD disc -> base-ROM link setting (per region, keyed by game code).
 *
 * The disc->base-ROM link IS the setting (not an override of anything): an explicit,
 * hand-editable mapping keyed by the disc's 4-char game code, set automatically when you link
 * a disc (auto-match or file-browser pick) and editable by hand. Split into two
 * per-region files so the English and Japanese versions of a disc cache INDEPENDENTLY and
 * never clobber each other (the old single-favorite link could only hold one):
 *
 *   <sd>/menu/n64ever/disclink_jp.ini     <sd>/menu/n64ever/disclink_us.ini
 *     EFZJ = sd:/.../F-Zero X (Japan).n64    EFZE = sd:/.../F-Zero X (USA).n64
 *
 * Region is chosen from the game code's 4th char ('J' -> jp, anything else -> us). A line is
 * `CODE = <full base-ROM path>`. The lookup takes precedence over the favorite's secondary_path
 * at launch. See memory project-64dd-disc-linking.
 */
#ifndef DISCLINK_H
#define DISCLINK_H

#include <stdbool.h>
#include <stddef.h>

/** Look up the base-ROM path for a disc game code. Returns true and fills out_path if a
 *  mapping exists in the disc's region file. */
bool disclink_lookup (const char *storage_prefix, const char *game_code, char *out_path, size_t out_sz);

/** True if the disc game code has a link entry (cheap presence check). */
bool disclink_has (const char *storage_prefix, const char *game_code);

/** Create/replace the link entry for a disc game code (writes the region file). */
void disclink_store (const char *storage_prefix, const char *game_code, const char *base_path);

/** Delete ALL disc->base links (removes both the US and JP region files). */
void disclink_clear_all (const char *storage_prefix);

#endif /* DISCLINK_H */
