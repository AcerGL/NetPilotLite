#include "settings.h"
#include <shlobj.h>
#include <objbase.h>
#include <stdio.h>
void np_settings_defaults(NP_SETTINGS *s) {
    ZeroMemory(s, sizeof(*s));
    s->schema = 2;
    s->theme = 2;
    s->close_tray = TRUE;
    s->mask = 35;
    s->custom_accent = RGB(116, 139, 231);
    s->custom_background = RGB(247, 242, 252);
    s->custom_card = RGB(255, 255, 255);
    s->window = (RECT){100, 100, 1120, 880};
}
BOOL np_data_directory(WCHAR *out, size_t chars) {
    PWSTR local = NULL;
    if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, KF_FLAG_DEFAULT, NULL, &local)))
        return FALSE;
    int n = swprintf_s(out, chars, L"%s\\NetPilotLite", local);
    CoTaskMemFree(local);
    return n > 0 && (CreateDirectoryW(out, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
}
static BOOL config_path(const WCHAR *dir, WCHAR *path) {
    WCHAR d[NP_PATH];
    if (!dir) {
        if (!np_data_directory(d, _countof(d)))
            return FALSE;
        dir = d;
    }
    return swprintf_s(path, NP_PATH, L"%s\\settings.ini", dir) > 0;
}
static int integer(const WCHAR *p, const WCHAR *key, int def) {
    return (int)GetPrivateProfileIntW(L"Preferences", key, def, p);
}
BOOL np_settings_load(NP_SETTINGS *s, const WCHAR *dir, NP_ERROR *e) {
    WCHAR p[NP_PATH], backup[NP_PATH], text[NP_ID_LEN];
    np_settings_defaults(s);
    if (!config_path(dir, p))
        goto fail;
    DWORD attr = GetFileAttributesW(p);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        swprintf_s(backup, _countof(backup), L"%s.bak", p);
        if (GetFileAttributesW(backup) != INVALID_FILE_ATTRIBUTES) {
            DWORD prior = GetPrivateProfileIntW(L"Config", L"SchemaVersion", 0, backup);
            if (prior < 1 || prior > 2 || !CopyFileW(backup, p, TRUE)) {
                s->binding_invalid = TRUE;
                np_error(e, ERROR_RECOVERY_FAILURE, 0, L"配置替换中断；保留备份，拒绝覆盖");
                return FALSE;
            }
            attr = GetFileAttributesW(p);
        }
    }
    if (attr == INVALID_FILE_ATTRIBUTES) {
        if (dir)
            return TRUE;
        int loaded = np_binding_load(&s->binding[0]);
        s->has[0] = loaded != 0;
        s->binding_invalid = loaded < 0;
        if (loaded < 0) {
            np_error(e, ERROR_INVALID_DATA, 0, L"旧版网卡绑定损坏，保留原记录并等待手动确认");
            return FALSE;
        }
        return TRUE;
    }
    DWORD schema = GetPrivateProfileIntW(L"Config", L"SchemaVersion", 0, p);
    if (schema < 1 || schema > 2) {
        s->binding_invalid = TRUE;
        np_error(e, ERROR_REVISION_MISMATCH, 0, L"配置版本损坏或高于当前版本；保留原文件，暂不覆盖");
        return FALSE;
    }
    if (schema < 2) {
        swprintf_s(backup, _countof(backup), L"%s.migration.bak", p);
        if (!CopyFileW(p, backup, TRUE) && GetLastError() != ERROR_FILE_EXISTS)
            goto fail;
    }
    for (int i = 0; i < 2; ++i) {
        const WCHAR *section = i ? L"WiFi" : L"Ethernet";
        s->has[i] = GetPrivateProfileIntW(section, L"Bound", 0, p) != 0;
        if (s->has[i]) {
            NP_BINDING *b = &s->binding[i];
            b->version = 1;
            GetPrivateProfileStringW(section, L"GUID", L"", text, _countof(text), p);
            BOOL ok = SUCCEEDED(CLSIDFromString(text, &b->guid));
            GetPrivateProfileStringW(section, L"Instance", L"", b->instance, NP_ID_LEN, p);
            if (!ok || !b->instance[0])
                s->binding_invalid = TRUE;
        }
    }
    s->theme = integer(p, L"Theme", 2);
    if (s->theme < 0 || s->theme > 4)
        s->theme = 2;
    s->background_mode = integer(p, L"BackgroundMode", 0);
    s->image_mode = integer(p, L"ImageMode", 0);
    s->mask = integer(p, L"Mask", 35);
    if (s->background_mode < 0 || s->background_mode > 2)
        s->background_mode = 0;
    if (s->image_mode < 0 || s->image_mode > 2)
        s->image_mode = 0;
    if (s->mask < 0 || s->mask > 100)
        s->mask = 35;
    s->custom_accent = (COLORREF)integer(p, L"Accent", (int)s->custom_accent) & 0xffffff;
    s->custom_background = (COLORREF)integer(p, L"Background", (int)s->custom_background) & 0xffffff;
    s->custom_card = (COLORREF)integer(p, L"Card", (int)s->custom_card) & 0xffffff;
    s->close_tray = integer(p, L"CloseToTray", 1) != 0;
    s->maximized = integer(p, L"Maximized", 0) != 0;
    s->window.left = integer(p, L"X", 100);
    s->window.top = integer(p, L"Y", 100);
    s->window.right = s->window.left + integer(p, L"Width", 1020);
    s->window.bottom = s->window.top + integer(p, L"Height", 780);
    GetPrivateProfileStringW(L"Preferences", L"Image", L"", s->image, _countof(s->image), p);
    if (s->binding_invalid) {
        np_error(e, ERROR_INVALID_DATA, 0, L"绑定配置损坏，请在网卡列表中手动确认；原文件已保留");
        return FALSE;
    }
    return TRUE;
fail:
    np_error(e, GetLastError(), 0, L"读取用户配置");
    return FALSE;
}
BOOL np_settings_save(const NP_SETTINGS *s, const WCHAR *dir, NP_ERROR *e) {
    WCHAR p[NP_PATH], tmp[NP_PATH], bak[NP_PATH], v[80];
    BOOL ok = TRUE;
    if (!config_path(dir, p))
        goto fail;
    DWORD schema = GetPrivateProfileIntW(L"Config", L"SchemaVersion", 2, p);
    if (schema > 2) {
        SetLastError(ERROR_REVISION_MISMATCH);
        goto fail;
    }
    swprintf_s(tmp, _countof(tmp), L"%s.tmp", p);
    swprintf_s(bak, _countof(bak), L"%s.bak", p);
    DeleteFileW(tmp);
    BOOL existed = GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
    if (existed) {
        if (!CopyFileW(p, tmp, TRUE))
            goto fail;
    } else {
        WORD bom = 0xfeff;
        if (!np_write_exact(tmp, &bom, 2))
            goto fail;
    }
#define STR(section, key, value)                                                                             \
    do {                                                                                                     \
        if (!WritePrivateProfileStringW(section, key, value, tmp))                                           \
            ok = FALSE;                                                                                      \
    } while (0)
#define NUM(key, value)                                                                                      \
    do {                                                                                                     \
        swprintf_s(v, _countof(v), L"%ld", (LONG)(value));                                                   \
        STR(L"Preferences", key, v);                                                                         \
    } while (0)
    STR(L"Config", L"SchemaVersion", L"2");
    for (int i = 0; i < 2; ++i) {
        const WCHAR *section = i ? L"WiFi" : L"Ethernet";
        STR(section, L"Bound", s->has[i] ? L"1" : L"0");
        if (s->has[i]) {
            StringFromGUID2(&s->binding[i].guid, v, _countof(v));
            STR(section, L"GUID", v);
            STR(section, L"Instance", s->binding[i].instance);
        }
    }
    NUM(L"Theme", s->theme);
    NUM(L"BackgroundMode", s->background_mode);
    NUM(L"ImageMode", s->image_mode);
    NUM(L"Mask", s->mask);
    NUM(L"Accent", s->custom_accent);
    NUM(L"Background", s->custom_background);
    NUM(L"Card", s->custom_card);
    NUM(L"CloseToTray", s->close_tray);
    NUM(L"Maximized", s->maximized);
    NUM(L"X", s->window.left);
    NUM(L"Y", s->window.top);
    NUM(L"Width", s->window.right - s->window.left);
    NUM(L"Height", s->window.bottom - s->window.top);
    STR(L"Preferences", L"Image", s->image);
    WritePrivateProfileStringW(NULL, NULL, NULL, tmp);
#undef STR
#undef NUM
    if (!ok)
        goto fail;
    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH, NULL);
    if (f == INVALID_HANDLE_VALUE)
        goto fail;
    ok = FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok)
        goto fail;
    if (existed) {
        DeleteFileW(bak);
        ok = ReplaceFileW(p, tmp, bak, 0, NULL, NULL);
    } else
        ok = MoveFileExW(tmp, p, MOVEFILE_WRITE_THROUGH);
    if (ok)
        return TRUE;
fail:
    np_error(e, GetLastError(), 0, L"原子保存用户配置；旧配置保留");
    return FALSE;
}
NP_COLORS np_theme(const NP_SETTINGS *s) {
    int theme = s->theme;
    if (theme == 3) {
        DWORD light = 1, n = sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &light, &n);
        theme = light ? 1 : 0;
    }
    NP_COLORS c =
        theme == 0
            ? (NP_COLORS){RGB(19, 24, 36),   RGB(239, 243, 255), RGB(170, 183, 207), RGB(31, 39, 55),
                          RGB(63, 75, 98),   RGB(134, 160, 255), RGB(64, 84, 145),   RGB(255, 255, 255),
                          RGB(77, 200, 156), RGB(245, 193, 90),  RGB(255, 133, 151)}
            : (NP_COLORS){RGB(244, 246, 251), RGB(36, 43, 65),   RGB(97, 107, 130), RGB(255, 255, 255),
                          RGB(213, 220, 235), RGB(80, 112, 218), RGB(80, 112, 218), RGB(255, 255, 255),
                          RGB(25, 126, 92),   RGB(160, 102, 8),  RGB(185, 57, 79)};
    if (theme == 2) {
        c.background = RGB(248, 240, 250);
        c.card = RGB(255, 250, 255);
        c.accent = RGB(137, 117, 208);
        c.button = RGB(137, 117, 208);
        c.border = RGB(232, 208, 231);
    }
    if (theme == 4) {
        c.background = s->custom_background;
        c.card = s->custom_card;
        c.accent = s->custom_accent;
        c.button = s->custom_accent;
        int lum = (GetRValue(c.card) * 299 + GetGValue(c.card) * 587 + GetBValue(c.card) * 114) / 1000;
        if (lum < 128) {
            c.foreground = RGB(240, 243, 253);
            c.secondary = RGB(181, 190, 210);
            c.border = RGB(63, 75, 98);
            c.success = RGB(77, 200, 156);
            c.warning = RGB(245, 193, 90);
            c.error = RGB(255, 133, 151);
        }
        int buttonlum =
            (GetRValue(c.button) * 299 + GetGValue(c.button) * 587 + GetBValue(c.button) * 114) / 1000;
        c.button_text = buttonlum > 150 ? RGB(20, 25, 40) : RGB(255, 255, 255);
    }
    return c;
}
