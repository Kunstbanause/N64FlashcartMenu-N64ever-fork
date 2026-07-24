/**
 * @file tabs.c
 * @brief Implementation of the tabs UI component.
 * @ingroup ui_components
 */

#include "../ui_components.h"
#include "constants.h"

/**
 * @brief Common tab labels used for the main menu.
 */
static const char *tabs[] = {
    "Favorites",
    "History",
    "Files",
    NULL
};

/**
 * @brief Draw the common tabs used for the main menu.
 *
 * Tab indices: 0=Favorites, 1=Files, 2=History.
 *
 * @param selected Index of the currently selected tab.
 */
void ui_components_tabs_common_draw(int selected)
{
    uint8_t tabs_count = 3;
    float width = (VISIBLE_AREA_X1 - VISIBLE_AREA_X0) / (float)tabs_count;
    ui_components_tabs_draw(tabs, tabs_count, selected, width);
}
