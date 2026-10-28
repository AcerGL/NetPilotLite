#ifndef NP_UPDATE_H
#define NP_UPDATE_H
#include "lite.h"
#define NP_VERSION L"1.3.0"
#define NP_VERSION_NUM 1, 3, 0, 0
#define NP_PATH 1024
#define NP_SIGNATURE_ERROR ((DWORD)0x80096010L) /* TRUST_E_BAD_DIGEST */
#pragma pack(push, 1)
typedef struct NP_RELEASE {
    BYTE magic[8];
    DWORD version[4];
    ULONGLONG size;
    BYTE hash[32];
    BYTE signature[64];
} NP_RELEASE;
#pragma pack(pop)
BOOL np_hash(const void *, DWORD, BYTE[32]);
BOOL np_hash_file(const WCHAR *, BYTE[32]);
BOOL np_read_exact(const WCHAR *, void *, DWORD);
BOOL np_write_exact(const WCHAR *, const void *, DWORD);
BOOL np_file_version(const WCHAR *, DWORD[4]);
BOOL np_verify_release(const WCHAR *, NP_RELEASE *, NP_ERROR *);
BOOL np_release_signature(const NP_RELEASE *);
BOOL np_release_matches(const WCHAR *, const NP_RELEASE *);
BOOL np_installer_version(const WCHAR *, DWORD[4]);
BOOL np_installer_signature(const NP_RELEASE *);
BOOL np_catalog_signature(const NP_RELEASE *);
BOOL np_installer_matches(const WCHAR *, const NP_RELEASE *);
BOOL np_verify_installer(const WCHAR *, NP_RELEASE *, NP_ERROR *);
BOOL np_update_start(HWND, const WCHAR *, NP_ERROR *);
BOOL np_update_recovery_pending(void);
BOOL np_update_recover_start(NP_ERROR *);
BOOL np_update_health_argument(const WCHAR *);
void np_update_health_ready(void);
int np_updater_apply(const WCHAR *, const WCHAR *, BOOL);
#endif
