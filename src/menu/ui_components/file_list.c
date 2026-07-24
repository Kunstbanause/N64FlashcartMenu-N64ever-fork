/**
 * @file file_list.c
 * @brief Implementation of the file list UI component.
 * @ingroup ui_components
 */

#include <stdlib.h>

#include "../ui_components.h"
#include "../fonts.h"
#include "constants.h"

/**
 * @brief Vivid left-edge accent colour per entry type — adds colour to the
 * otherwise plain list without competing with the Grid view.
 */
static color_t type_accent_color(entry_type_t type) {
    switch (type) {
        case ENTRY_TYPE_DIR:       return RGBA32(0xFF, 0xC8, 0x28, 0xFF); /* yellow  */
        case ENTRY_TYPE_ROM:       return RGBA32(0x3C, 0x8C, 0xFF, 0xFF); /* blue    */
        case ENTRY_TYPE_DISK:      return RGBA32(0x3C, 0x8C, 0xFF, 0xFF);
        case ENTRY_TYPE_EMULATOR:  return RGBA32(0x9C, 0x6C, 0xFF, 0xFF); /* purple  */
        case ENTRY_TYPE_SAVE:      return RGBA32(0x40, 0xD0, 0x50, 0xFF); /* green   */
        case ENTRY_TYPE_ROM_PATCH:
        case ENTRY_TYPE_ROM_CHEAT:
        case ENTRY_TYPE_ROM_META:  return RGBA32(0x40, 0xD0, 0x50, 0xFF);
        case ENTRY_TYPE_IMAGE:     return RGBA32(0x30, 0xC8, 0xE0, 0xFF); /* cyan    */
        case ENTRY_TYPE_MUSIC:     return RGBA32(0x30, 0xC8, 0xE0, 0xFF);
        case ENTRY_TYPE_TEXT:      return RGBA32(0xFF, 0x9C, 0x30, 0xFF); /* orange  */
        case ENTRY_TYPE_ARCHIVE:   return RGBA32(0xFF, 0x9C, 0x30, 0xFF);
        default:                   return RGBA32(0x7A, 0x7A, 0x7A, 0xFF); /* gray    */
    }
}

/**
 * @brief Format the file size into a human-readable string.
 *
 * @param buffer Buffer to store the formatted string.
 * @param size Size of the file in bytes.
 * @return Number of characters written to the buffer.
 */
static int format_file_size(char *buffer, int64_t size) {
    if (size < 0) {
        return sprintf(buffer, "unknown");
    } else if (size == 0) {
        return sprintf(buffer, "empty");
    } else if (size < 8 * 1024) {
        return sprintf(buffer, "%lld B", size);
    } else if (size < 4 * 1024 * 1024) {
        return sprintf(buffer, "%4lld kB", size / 1024);
    } else if (size < 1 * 1024 * 1024 * 1024) {
        return sprintf(buffer, "%2lld MB", size / 1024 / 1024);
    } else {
        return sprintf(buffer, "%2lld GB", size / 1024 / 1024 / 1024);
    }
}

/**
 * @brief Draw the file list UI component.
 *
 * @param list Pointer to the list of file entries.
 * @param entries Number of entries in the list.
 * @param selected Index of the currently selected entry.
 */
void ui_components_file_list_draw(entry_t *list, int entries, int selected, bool *is_fav, bool show_file_size) {
    int starting_position = 0;

    if (entries > FILE_LIST_ENTRIES && selected >= (FILE_LIST_ENTRIES / 2)) {
        starting_position = selected - (FILE_LIST_ENTRIES / 2);
        if (starting_position >= entries - FILE_LIST_ENTRIES) {
            starting_position = entries - FILE_LIST_ENTRIES;
        }
    }

    if (entries == 0) {
        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "\n"
            "^%02X** empty directory **",
            STL_GRAY
        );
    } else {
        rdpq_paragraph_t *file_list_layout;
        rdpq_paragraph_t *layout;

        size_t name_lengths[FILE_LIST_ENTRIES];
        size_t total_length = 1;

        for (int i = 0; i < FILE_LIST_ENTRIES; i++) {
            int entry_index = starting_position + i;

            if (entry_index >= entries) {
                name_lengths[i] = 0;
            } else {
                size_t length = strlen(list[entry_index].name);
                name_lengths[i] = length;
                total_length += length;
            }
        }

        file_list_layout = malloc(sizeof(rdpq_paragraph_t) + (sizeof(rdpq_paragraph_char_t) * total_length));
        memset(file_list_layout, 0, sizeof(rdpq_paragraph_t));
        file_list_layout->capacity = total_length;

        rdpq_paragraph_builder_begin(
            &(rdpq_textparms_t) {
                .width = FILE_LIST_MAX_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
                .height = FILE_LIST_AREA_H,
                .wrap = WRAP_ELLIPSES,
                .line_spacing = TEXT_LINE_SPACING_ADJUST,
            },
            FNT_DEFAULT,
            file_list_layout
        );

        for (int i = 0; i < FILE_LIST_ENTRIES; i++) {
            int entry_index = starting_position + i;

            entry_t *entry = &list[entry_index];

            menu_font_style_t style;

            switch (entry->type) {
                case ENTRY_TYPE_DIR: style = STL_YELLOW; break;
                case ENTRY_TYPE_ROM: style = STL_DEFAULT; break;
                case ENTRY_TYPE_DISK: style = STL_DEFAULT; break;
                case ENTRY_TYPE_EMULATOR: style = STL_DEFAULT; break;
                case ENTRY_TYPE_SAVE: style = STL_GREEN; break;
                case ENTRY_TYPE_IMAGE: style = STL_BLUE; break;
                case ENTRY_TYPE_MUSIC: style = STL_BLUE; break;
                case ENTRY_TYPE_TEXT: style = STL_ORANGE; break;
                case ENTRY_TYPE_ARCHIVE: style = STL_ORANGE; break;
                case ENTRY_TYPE_ARCHIVED: style = STL_DEFAULT; break;
                case ENTRY_TYPE_ROM_PATCH: style = STL_GREEN; break;
                case ENTRY_TYPE_ROM_CHEAT: style = STL_GREEN; break;
                case ENTRY_TYPE_ROM_META: style = STL_GREEN; break;
                case ENTRY_TYPE_OTHER: style = STL_GRAY; break;
                default: style = STL_GRAY; break;
            }

            rdpq_paragraph_builder_style(style);

            rdpq_paragraph_builder_span(entry->name, name_lengths[i]);

            if ((entry_index + 1) >= entries) {
                break;
            }

            rdpq_paragraph_builder_newline();
        }

        layout = rdpq_paragraph_builder_end();

        int highlight_height = (layout->bbox.y1 - layout->bbox.y0) / layout->nlines;

        /* When the list doesn't fill the viewport, anchor it vertically centred
           instead of top-aligned so short directories don't cling to the top. */
        int list_top_y = FILE_LIST_TOP_Y;
        if (entries < FILE_LIST_ENTRIES) {
            list_top_y += ((FILE_LIST_ENTRIES - entries) * highlight_height) / 2;
        }

        int highlight_y = list_top_y + ((selected - starting_position) * highlight_height);

        /* Alternating row tint — very faint dark-blue on odd rows for texture. */
        for (int i = 0; i < FILE_LIST_ENTRIES && (starting_position + i) < entries; i++) {
            if (i & 1) {
                int ry = list_top_y + i * highlight_height;
                ui_components_box_draw(VISIBLE_AREA_X0, ry,
                                       VISIBLE_AREA_X1 - FILE_LIST_SCROLL_ARROW_GAP,
                                       ry + highlight_height,
                                       RGBA32(0x18, 0x18, 0x28, 0xFF));
            }
        }

        ui_components_box_draw(
            FILE_LIST_HIGHLIGHT_X,
            highlight_y,
            VISIBLE_AREA_X1 - FILE_LIST_SCROLL_ARROW_GAP,
            highlight_y + highlight_height,
            FILE_LIST_HIGHLIGHT_COLOR
        );

        rdpq_paragraph_render(
            layout,
            VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
            list_top_y
        );

        rdpq_paragraph_free(layout);

        /* Type-glyph dot: a 4×10 pill centred in each row, coloured by type. */
        for (int i = 0; i < FILE_LIST_ENTRIES; i++) {
            int ei = starting_position + i;
            if (ei >= entries) break;
            int mid_y = list_top_y + i * highlight_height + highlight_height / 2;
            ui_components_box_draw(VISIBLE_AREA_X0 + 2, mid_y - 5,
                                   VISIBLE_AREA_X0 + 6, mid_y + 5,
                                   type_accent_color(list[ei].type));
        }

        /* Right column: presents-as region + favourite star, right-aligned. When
           file sizes are shown they sit in a fixed-width column further right, so
           the stars stay aligned regardless of size length. Room is reserved on
           the far right for the scroll indicator. */
        int star_reserve = FILE_LIST_SCROLL_ARROW_GAP + (show_file_size ? FILE_LIST_SIZE_COL_W : 0);

        rdpq_paragraph_builder_begin(
            &(rdpq_textparms_t) {
                .width = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2) - star_reserve,
                .height = FILE_LIST_AREA_H,
                .align = ALIGN_RIGHT,
                .wrap = WRAP_ELLIPSES,
                .line_spacing = TEXT_LINE_SPACING_ADJUST,
            },
            FNT_DEFAULT,
            NULL
        );

        for (int i = starting_position; i < entries; i++) {
            entry_t *entry = &list[i];

            if (entry->type != ENTRY_TYPE_DIR) {
                if (entry->presents_region) {
                    char reg[2] = { entry->presents_region, '\0' };
                    rdpq_paragraph_builder_style(STL_GREEN);
                    rdpq_paragraph_builder_span(reg, 1);
                    rdpq_paragraph_builder_style(STL_DEFAULT);
                } else {
                    rdpq_paragraph_builder_span(" ", 1);
                }
                if (is_fav && is_fav[i]) {
                    rdpq_paragraph_builder_style(STL_YELLOW);
                    rdpq_paragraph_builder_span(" *", 2);
                    rdpq_paragraph_builder_style(STL_DEFAULT);
                } else {
                    rdpq_paragraph_builder_span("  ", 2);
                }
            } else {
                /* Directories are colour-coded already — no "[DIR]" tag. */
                rdpq_paragraph_builder_span(" ", 1);
            }

            if ((i + 1) == (starting_position + FILE_LIST_ENTRIES)) {
                break;
            }

            rdpq_paragraph_builder_newline();
        }

        layout = rdpq_paragraph_builder_end();

        rdpq_paragraph_render(
            layout,
            VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
            list_top_y
        );

        rdpq_paragraph_free(layout);

        /* Optional file size, in its own right-aligned column on the far right. */
        if (show_file_size) {
            char file_size[16];
            rdpq_paragraph_builder_begin(
                &(rdpq_textparms_t) {
                    .width = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2) - FILE_LIST_SCROLL_ARROW_GAP,
                    .height = FILE_LIST_AREA_H,
                    .align = ALIGN_RIGHT,
                    .wrap = WRAP_ELLIPSES,
                    .line_spacing = TEXT_LINE_SPACING_ADJUST,
                },
                FNT_DEFAULT,
                NULL
            );
            for (int i = starting_position; i < entries; i++) {
                entry_t *entry = &list[i];
                if (entry->type != ENTRY_TYPE_DIR) {
                    rdpq_paragraph_builder_span(file_size, format_file_size(file_size, entry->size));
                } else {
                    rdpq_paragraph_builder_span(" ", 1);
                }
                if ((i + 1) == (starting_position + FILE_LIST_ENTRIES)) {
                    break;
                }
                rdpq_paragraph_builder_newline();
            }
            layout = rdpq_paragraph_builder_end();
            rdpq_paragraph_render(layout, VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL, list_top_y);
            rdpq_paragraph_free(layout);
        }

        /* Scroll indicator on the right: a thin rainbow track with a bright
           rainbow orb for the rough position, plus ▲/▼ arrows above/below. */
        static uint8_t scroll_hue = 0;
        scroll_hue += 1;   /* halved for the 60fps render */
        if (entries > FILE_LIST_ENTRIES) {
            int col_cx    = VISIBLE_AREA_X1 - (FILE_LIST_SCROLL_ARROW_GAP / 2);
            int track_top = FILE_LIST_TOP_Y + 2;
            int track_bot = LAYOUT_ACTIONS_SEPARATOR_Y - 4;

            if (track_bot > track_top) {
                /* Vertical rainbow gradient track — full height, no arrow padding. */
                int segs = (track_bot - track_top) / 6;
                if (segs < 1) segs = 1;
                for (int s = 0; s < segs; s++) {
                    int sy0 = track_top + (track_bot - track_top) * s / segs;
                    int sy1 = track_top + (track_bot - track_top) * (s + 1) / segs;
                    ui_components_box_draw(col_cx - 1, sy0, col_cx + 1, sy1,
                        ui_components_rainbow_color((uint8_t)(scroll_hue + 200 * s / segs), 110));
                }
                int orb   = 4;
                float frac = (entries > 1) ? (float)selected / (float)(entries - 1) : 0.0f;
                int orb_cy = track_top + orb + (int)((track_bot - track_top - 2 * orb) * frac);
                ui_components_box_draw(col_cx - orb, orb_cy - orb, col_cx + orb, orb_cy + orb,
                    ui_components_rainbow_color(scroll_hue, 255));
            }
        }
    }
}
