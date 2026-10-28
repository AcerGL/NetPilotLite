#ifndef NP_APPS_H
#define NP_APPS_H
#include "update.h"
#define NP_MAX_APPS 1024
typedef struct NP_APP_ENTRY {
    WCHAR name[256], publisher[256], version[80], location[NP_PATH], path[NP_PATH];
    WCHAR family[128], aumid[256], uninstall[NP_PATH], note[180];
    DWORD pid, error;
    BOOL package, candidate;
    HICON icon;
} NP_APP_ENTRY;
typedef struct NP_APP_LIST {
    size_t count;
    BOOL truncated;
    DWORD registry_errors, process_errors;
    HRESULT shell_error;
    NP_APP_ENTRY rows[NP_MAX_APPS];
} NP_APP_LIST;
BOOL np_app_exe(const WCHAR *);
BOOL np_display_icon_path(const WCHAR *, WCHAR *, size_t);
NP_APP_LIST *np_apps_scan(BOOL running);
void np_apps_free(NP_APP_LIST *);
#endif
