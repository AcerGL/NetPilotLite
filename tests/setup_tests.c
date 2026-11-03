/* Real Windows files, processes, registry and shortcuts; mock product cannot mutate adapters. */
#include "../installer/web/install.c"
#include <fcntl.h>
#include <io.h>
static unsigned pass, fail;
#define T(x)                                                                                                 \
    do {                                                                                                     \
        if (x)                                                                                               \
            pass++;                                                                                          \
        else {                                                                                               \
            fail++;                                                                                          \
            wprintf(L"FAIL %d %hs Windows=%lu\n", __LINE__, #x, GetLastError());                             \
        }                                                                                                    \
    } while (0)
static BOOL put(const WCHAR *dir, const WCHAR *name, const void *value, DWORD size) {
    WCHAR p[NP_PATH];
    join(p, dir, name);
    return np_write_exact(p, value, size);
}
static BOOL same(const WCHAR *dir, const WCHAR *name, const void *value, DWORD size) {
    WCHAR p[NP_PATH];
    BYTE buf[64];
    join(p, dir, name);
    return size <= 64 && np_read_exact(p, buf, size) && !memcmp(buf, value, size);
}
static void seed(const WCHAR *old, const WCHAR *dir) {
    WCHAR p[NP_PATH], q[NP_PATH];
    T(np_install_mkdirs(dir));
    const WCHAR *names[] = {L"NetPilotLite.exe", L"NetPilotLite.exe.update"};
    for (unsigned i = 0; i < 2; i++) {
        join(p, old, names[i]);
        join(q, dir, names[i]);
        T(copy(p, q));
    }
    T(put(dir, L"user-settings.keep", "preserve", 8));
}
static BOOL version(const WCHAR *dir, DWORD major, DWORD minor) {
    WCHAR p[NP_PATH];
    NP_RELEASE r;
    NP_ERROR e = {0};
    join(p, dir, L"NetPilotLite.exe");
    return np_verify_release(p, &r, &e) && r.version[0] == major && r.version[1] == minor;
}
static void seed_metadata(const WCHAR *old,const WCHAR *dir){
    WCHAR p[NP_PATH],q[NP_PATH];const WCHAR *names[]={L".NetPilot-install\\manifest.json",L".NetPilot-install\\manifest.json.update"};
    for(unsigned i=0;i<2;i++){join(p,old,names[i]);join(q,dir,names[i]);T(copy(p,q));}
}
int wmain(int argc, WCHAR **argv) {
    _setmode(_fileno(stdout), _O_U8TEXT);
    if (argc != 2)
        return 2;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    WCHAR base[NP_PATH], stage[NP_PATH], bad[NP_PATH], old[NP_PATH], dir[NP_PATH], p[NP_PATH], root[NP_PATH];
    GetFullPathNameW(argv[1], NP_PATH, base, NULL);
    join(stage, base, L"stage");
    join(bad, base, L"bad");
    join(old, base, L"prior");
    join(root, base, L"actual-windows-run");
    /* Never reuse or recursively delete an arbitrary caller directory. */
    if (GetFileAttributesW(root) != INVALID_FILE_ATTRIBUTES) {
        wprintf(L"Use a fresh fixture directory; previous evidence is retained.\n");
        return 2;
    }
    T(np_install_mkdirs(root));
    NP_ERROR e = {0};
    INSTALL_JOURNAL original = {0};
    T(save_integration(root, &original));
    original.integration = TRUE;
    join(dir, root, L"Fresh 中文 space");
    T(np_setup_install(stage, dir, FALSE, TRUE, &e));
    wprintf(L"Fresh result Windows=%lu %s\n", e.win32, e.operation);
    T(version(dir, 1, 5));
    join(p, dir, L"resources\\nested\\fixture.dat");
    T(GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES);
    T(close_app(dir));
    HKEY k=NULL;
    WCHAR run[NP_PATH + 32];
    DWORD bytes = sizeof(run), type = 0;
    T(RegOpenKeyExW(HKEY_CURRENT_USER, run_key, 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS);
    if (k) {
        T(RegQueryValueExW(k, run_name, NULL, &type, (BYTE *)run, &bytes) == ERROR_SUCCESS &&
          wcsstr(run, dir) != NULL);
        RegCloseKey(k);
    }
    T(shortcut_path(p) && GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES);
    T(!np_setup_install(stage, dir, TRUE, FALSE, &e) && e.win32 == ERROR_REVISION_MISMATCH);
    join(dir, root, L"Upgrade running");
    seed(old, dir);
    seed_metadata(old,dir);
    T(start_app(dir, TRUE));
    T(np_setup_install(stage, dir, TRUE, FALSE, &e));
    wprintf(L"Upgrade result Windows=%lu %s\n", e.win32, e.operation);
    T(version(dir, 1, 5));
    T(same(dir, L"user-settings.keep", "preserve", 8));
    T(close_app(dir));
    join(dir, root, L"Failed health rollback");
    seed(old, dir);
    seed_metadata(old,dir);
    T(!np_setup_install(bad, dir, TRUE, FALSE, &e));
    T(e.win32 == ERROR_APP_INIT_FAILURE);
    T(version(dir, 1, 0));
    T(same(dir, L"user-settings.keep", "preserve", 8));
    T(close_app(dir));
    const WCHAR *faults[] = {L"prepared", L"replaced"};
    for (unsigned i = 0; i < 2; i++) {
        join(dir, root, faults[i]);
        seed(old, dir);
        SetEnvironmentVariableW(L"NP_SETUP_TEST_FAULT", faults[i]);
        T(!np_setup_install(stage, dir, TRUE, FALSE, &e) && e.win32 == ERROR_PROCESS_ABORTED);
        SetEnvironmentVariableW(L"NP_SETUP_TEST_FAULT", NULL);
        join(p, dir, L".NetPilotSetup-transaction\\journal.bin");
        T(GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES);
        T(!np_setup_install(stage, dir, TRUE, FALSE, &e) && e.win32 == ERROR_RECOVERY_FAILURE);
        T(GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES);
        T(np_setup_recover(dir, TRUE, &e));
        wprintf(L"Recovery %s Windows=%lu %s\n", faults[i], e.win32, e.operation);
        T(version(dir, 1, 0));
        T(same(dir, L"user-settings.keep", "preserve", 8));
        T(GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES);
        T(close_app(dir));
    }
    join(dir, root, L"Unknown collision");
    seed(old, dir);
    join(p, dir, L"resources\\nested");
    T(np_install_mkdirs(p));
    T(put(p, L"fixture.dat", "USER", 4));
    T(!np_setup_install(stage, dir, TRUE, FALSE, &e) && e.win32 == ERROR_ALREADY_EXISTS);
    T(same(p, L"fixture.dat", "USER", 4));
    T(version(dir, 1, 0));
    /* Undo only our separate test Run value and test shortcut, not NetPilot's settings. */
    T(undo_integration(root, &original));
    wprintf(L"Windows setup transaction: %u passed, %u failed. Test-only Run/shortcut restored; no NIC "
            L"mutation backend linked.\n",
            pass, fail);
    CoUninitialize();
    return fail ? 1 : 0;
}
