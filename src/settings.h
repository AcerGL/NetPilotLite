#ifndef NP_SETTINGS_H
#define NP_SETTINGS_H
#include "lite.h"
#include "update.h"
#include "routing.h"
#include "online.h"
typedef struct NP_COLORS {
    COLORREF background, foreground, secondary, card, border, accent, button, button_text, success, warning,
        error;
} NP_COLORS;
typedef struct NP_SETTINGS {
    DWORD schema;
    NP_UPDATE_SETTINGS update;
    BOOL has[2], binding_invalid, close_tray, maximized;
    NP_BINDING binding[2];
    int theme, background_mode, image_mode, mask;
    COLORREF custom_accent, custom_background, custom_card;
    RECT window;
    WCHAR image[NP_PATH];
    /* Additive schema-2 keys: older builds preserve unknown fields on rollback. */
    WCHAR cover[NP_PATH];
    int card_opacity, font_scale, density;
    BOOL animations, tray_round;
    int tray_style, tray_opacity, tray_size, tray_icon;
    int tray_density; /* 0 compact, 1 comfortable */
    COLORREF tray_accent;
    int rule_count;
    NP_RULE rules[NP_MAX_RULES]; /* Observation only. Never loaded as firewall policy. */
} NP_SETTINGS;
void np_settings_defaults(NP_SETTINGS *);
BOOL np_settings_load(NP_SETTINGS *, const WCHAR *, NP_ERROR *);
BOOL np_settings_save(const NP_SETTINGS *, const WCHAR *, NP_ERROR *);
BOOL np_data_directory(WCHAR *, size_t);
NP_COLORS np_theme(const NP_SETTINGS *);
#endif
