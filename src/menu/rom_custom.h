#ifndef ROM_CUSTOM_H__
#define ROM_CUSTOM_H__

#include <stdbool.h>

#define ROM_CUSTOM_MAX_FIELDS 6

typedef struct {
    char label[64];
    char value[128];
} rom_custom_field_t;

typedef struct {
    char description[512];
    bool has_description;
    rom_custom_field_t fields[ROM_CUSTOM_MAX_FIELDS];
    int field_count;
} rom_custom_t;

/**
 * @brief Load custom JSON metadata for a ROM from /menu/n64ever/gameconfigs/<stem>.json.
 *
 * Format: {"description":"...","fields":[{"label":"...","value":"..."},...]}
 *
 * @param storage_prefix  Storage prefix, e.g. "sd:".
 * @param rom_full_path   Full path to the ROM file.
 * @param out             Output struct, zeroed on entry, populated on success.
 * @return true if any data was loaded (description or fields), false otherwise.
 */
bool rom_custom_load(const char *storage_prefix, const char *rom_full_path, rom_custom_t *out);

#endif // ROM_CUSTOM_H__
