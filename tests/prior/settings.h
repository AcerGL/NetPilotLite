#ifndef NP_SETTINGS_H
#define NP_SETTINGS_H
#include "lite.h"
#include "update.h"
typedef struct NP_COLORS {
    COLORREF background, foreground, secondary, card, border, accent, button, button_text, success, warning,
        error;
} NP_COLORS;
typedef struct NP_SETTINGS {
    DWORD schema;
    BOOL has[2], binding_invalid, close_tray, maximized;
    NP_BINDING binding[2];
    int theme, background_mode, image_mode, mask;
    COLORREF custom_accent, custom_background, custom_card;
    RECT window;
    WCHAR image[NP_PATH];
} NP_SETTINGS;
void np_settings_defaults(NP_SETTINGS *);
BOOL np_settings_load(NP_SETTINGS *, const WCHAR *, NP_ERROR *);
BOOL np_settings_save(const NP_SETTINGS *, const WCHAR *, NP_ERROR *);
BOOL np_data_directory(WCHAR *, size_t);
NP_COLORS np_theme(const NP_SETTINGS *);
#endif
