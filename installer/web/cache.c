#include "settings.h"
#include <shlobj.h>
#include <stdio.h>
BOOL np_data_directory(WCHAR *out, size_t cap) {
    WCHAR base[NP_PATH];
    DWORD n = GetTempPathW(NP_PATH, base);
    if (!n || n >= NP_PATH || swprintf_s(out, cap, L"%sNetPilotLite-Downloads", base) < 0) return FALSE;
    return CreateDirectoryW(out, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}
