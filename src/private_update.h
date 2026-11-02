#ifndef NP_PRIVATE_UPDATE_H
#define NP_PRIVATE_UPDATE_H
#include "online.h"
#define NP_TOKEN_CHARS 256
#define NP_PACKAGE L"NetPilotLite-Update-x64.zip"
#define NP_PACKAGE_JSON L"NetPilotLite-Update-x64.json"
typedef struct NP_AUTH {
    DWORD magic;
    WCHAR owner[100], repo[100], token[NP_TOKEN_CHARS];
} NP_AUTH;
typedef struct NP_PRIVATE_RELEASE {
    NP_ONLINE_RELEASE info;
    ULONGLONG asset_id[3], size[3]; /* zip, JSON, JSON signature */
} NP_PRIVATE_RELEASE;
BOOL np_token_valid(const WCHAR *);
BOOL np_auth_store(const WCHAR *, const NP_AUTH *, NP_ERROR *);
BOOL np_auth_load(const WCHAR *, const NP_UPDATE_SETTINGS *, NP_AUTH *, NP_ERROR *);
BOOL np_auth_remove(const WCHAR *, NP_ERROR *);
BOOL np_auth_validate(const NP_UPDATE_SETTINGS *, const WCHAR *, volatile LONG *, NP_ERROR *);
BOOL np_private_http(const NP_UPDATE_SETTINGS *, const WCHAR *, const WCHAR *, BOOL, BYTE **, DWORD *, HANDLE,
                     ULONGLONG, volatile LONG *, NP_DOWNLOAD_PROGRESS, void *, NP_ERROR *);
BOOL np_private_parse(const char *, size_t, const NP_UPDATE_SETTINGS *, NP_PRIVATE_RELEASE *, NP_ERROR *);
BOOL np_private_check(const NP_UPDATE_SETTINGS *, NP_PRIVATE_RELEASE *, volatile LONG *, NP_ERROR *);
BOOL np_private_download(const NP_PRIVATE_RELEASE *, const NP_UPDATE_SETTINGS *, WCHAR[NP_PATH],
                         volatile LONG *, NP_DOWNLOAD_PROGRESS, void *, NP_ERROR *);
BOOL np_package_extract(const WCHAR *, const WCHAR *, const NP_PRIVATE_RELEASE *, WCHAR[NP_PATH], NP_ERROR *);
BOOL np_boot_id(WCHAR[40]);
BOOL np_boot_due(const WCHAR *, const WCHAR *);
BOOL np_boot_complete(const WCHAR *, const WCHAR *);
BOOL np_boot_attempt(const WCHAR *, const WCHAR *, ULONGLONG, unsigned *);
BOOL np_startup_initialize(NP_ERROR *);
BOOL np_ignored(const WCHAR *, const DWORD[4]);
BOOL np_ignore_version(const WCHAR *, const WCHAR *);
BOOL np_autorun(BOOL, NP_ERROR *);
BOOL np_autorun_enabled(void);
#endif
