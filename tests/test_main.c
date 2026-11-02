#include "lite.h"
#include "routing.h"
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <string.h>
#include <wchar.h>

static unsigned passed, failed;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (x) {                                                                                             \
            ++passed;                                                                                        \
        } else {                                                                                             \
            ++failed;                                                                                        \
            wprintf(L"FAIL line %d: %hs\n", __LINE__, #x);                                                   \
        }                                                                                                    \
    } while (0)
static void run_tests(void) {
    NP_MOCK m;
    NP_BACKEND b;
    NP_RESULT r;
    NP_BINDING target;
#define RESET()                                                                                              \
    do {                                                                                                     \
        np_mock_init(&m);                                                                                    \
        b = np_mock_backend(&m);                                                                             \
        target = m.snapshot.adapters[0].identity;                                                            \
    } while (0)
    RESET();
    CHECK(m.writes == 0);
    CHECK(np_auto_select(&m.snapshot) == 0);
    r = np_execute(&b, &target, FALSE, 1000);
    CHECK(r.success && r.state == NP_DISABLED);
    CHECK(m.writes == 1 && m.reads >= 2);
    CHECK(np_identity_equal(&m.last_target, &target));
    CHECK(m.snapshot.adapters[1].state == NP_ENABLED_CONNECTED);
    CHECK(!wcscmp(m.snapshot.wifi, L"Wi-Fi：已启用 · 已连接（模拟）"));
    r = np_execute(&b, &target, TRUE, 1000);
    CHECK(r.success && r.state == NP_ENABLED_DISCONNECTED);
    CHECK(m.writes == 2);
    r = np_execute(&b, &target, TRUE, 1000);
    CHECK(r.success && r.unchanged && m.writes == 2);
    RESET();
    m.remote = TRUE;
    r = np_execute(&b, &target, FALSE, 1000);
    CHECK(!r.success && r.error.win32 == ERROR_ACCESS_DENIED && m.writes == 0);
    m.snapshot.adapters[0].state = NP_DISABLED;
    r = np_execute(&b, &target, TRUE, 1000);
    CHECK(r.success && m.writes == 1); /* Remote enable remains allowed. */
    RESET();
    m.fail_write = TRUE;
    r = np_execute(&b, &target, FALSE, 1000);
    CHECK(!r.success && r.error.win32 == ERROR_ACCESS_DENIED && r.error.config_ret == 0x33);
    CHECK(m.reads == 2 && r.observed && np_enabled(r.state));
    RESET();
    m.ignore_write = TRUE;
    r = np_execute(&b, &target, FALSE, 600);
    CHECK(!r.success && r.error.win32 == WAIT_TIMEOUT && m.writes == 1);
    CHECK(m.reads >= 3 && m.snapshot.adapters[0].state == NP_ENABLED_CONNECTED);
    RESET();
    target.guid.Data1++;
    r = np_execute(&b, &target, FALSE, 1000);
    CHECK(!r.success && m.writes == 0);
    RESET();
    wcscpy_s(target.instance, NP_ID_LEN, L"PCI\\WRONG_DEVICE");
    r = np_execute(&b, &target, FALSE, 1000);
    CHECK(!r.success && m.writes == 0);
    RESET();
    m.snapshot.adapters[0].physical_ethernet = FALSE;
    r = np_execute(&b, &target, FALSE, 1000);
    CHECK(!r.success && m.writes == 0);
    RESET();
    m.snapshot.adapters[0].state = NP_DEVICE_ERROR;
    r = np_execute(&b, &target, FALSE, 1000);
    CHECK(!r.success && m.writes == 0);
    RESET();
    m.snapshot.adapters[0].state = NP_UNKNOWN;
    r = np_execute(&b, &target, TRUE, 1000);
    CHECK(!r.success && m.writes == 0);
    RESET();
    m.fail_read = TRUE;
    r = np_execute(&b, &target, FALSE, 1000);
    CHECK(!r.success && m.writes == 0 && r.error.win32 == ERROR_READ_FAULT);
    RESET();
    m.snapshot.adapters[1].identity = target;
    r = np_execute(&b, &target, FALSE, 1000);
    CHECK(!r.success && m.writes == 0);
    RESET();
    m.snapshot.adapters[1].intel_i219 = TRUE;
    CHECK(np_auto_select(&m.snapshot) == -1);
    RESET();
    m.snapshot.adapters[0].intel_i219 = FALSE;
    CHECK(np_auto_select(&m.snapshot) == -1);
    RESET();
    NP_ADAPTER temp = m.snapshot.adapters[0];
    m.snapshot.adapters[0] = m.snapshot.adapters[1];
    m.snapshot.adapters[1] = temp;
    r = np_execute(&b, &target, FALSE, 1000);
    CHECK(r.success && m.snapshot.adapters[1].state == NP_DISABLED);
    CHECK(m.snapshot.adapters[0].state == NP_ENABLED_CONNECTED);
    RESET();
    m.snapshot.adapters[0].state = NP_DISABLED;
    CHECK(np_auto_select(&m.snapshot) == 0);
    CHECK(np_find(&m.snapshot, &target) != NULL);
    r = np_execute(&b, &target, TRUE, 1000);
    CHECK(r.success && np_enabled(r.state));
    CHECK(np_enabled(NP_ENABLED_DISCONNECTED));
    CHECK(!np_enabled(NP_DISABLED));
    CHECK(!np_enabled(NP_DEVICE_ERROR));
    CHECK(wcscmp(np_state_text(NP_ENABLED_DISCONNECTED), np_state_text(NP_DISABLED)) != 0);
    RESET();
    CHECK(np_auto_wifi(&m.snapshot) == 2);
    NP_BINDING wifi = m.snapshot.adapters[2].identity;
    r = np_execute(&b, &wifi, FALSE, 1000);
    CHECK(r.success && m.snapshot.adapters[2].state == NP_DISABLED &&
          m.snapshot.adapters[0].state == NP_ENABLED_CONNECTED);
    r = np_execute(&b, &wifi, TRUE, 1000);
    CHECK(r.success && m.snapshot.adapters[2].state == NP_ENABLED_DISCONNECTED);
    RESET();
    wifi = m.snapshot.adapters[2].identity;
    m.remote = TRUE;
    r = np_execute(&b, &wifi, FALSE, 1000);
    CHECK(!r.success && m.writes == 0);
    RESET();
    wifi = m.snapshot.adapters[2].identity;
    m.snapshot.adapters[2].state = NP_DISABLED;
    r = np_hotspot(&b, &target, &wifi, FALSE, 1000);
    CHECK(!r.success && m.writes == 0 && m.snapshot.adapters[0].state == NP_ENABLED_CONNECTED);
    r = np_hotspot(&b, &target, &wifi, TRUE, 1000);
    CHECK(!r.success && m.writes == 1 && m.snapshot.adapters[0].state == NP_ENABLED_CONNECTED);
    RESET();
    wifi = m.snapshot.adapters[2].identity;
    r = np_hotspot(&b, &target, &wifi, FALSE, 1000);
    CHECK(r.success && m.writes == 1 && m.snapshot.adapters[0].state == NP_DISABLED &&
          m.snapshot.adapters[2].state == NP_ENABLED_CONNECTED);
    RESET();
    wifi = m.snapshot.adapters[2].identity;
    m.remote = TRUE;
    r = np_hotspot(&b, &target, &wifi, FALSE, 1000);
    CHECK(!r.success && m.writes == 0);
    RESET();
    m.snapshot.adapters[0].state = NP_DISABLED;
    r = np_execute(&b, &target, TRUE, 1000);
    CHECK(r.success && m.snapshot.adapters[2].state == NP_ENABLED_CONNECTED);
    NP_BACKEND real = np_windows_backend();
    NP_ERROR error = {0};
    CHECK(!real.set_enabled(real.context, &target, FALSE, &error) && error.win32 == ERROR_ACCESS_DENIED);
    wprintf(L"Mock checks: %u passed, %u failed. Real mutation functions are compiled out of this test "
            L"executable.\n",
            passed, failed);
}
#include "routing_tests.inc"
#include "features_tests.inc"
#include "private_tests.inc"
#include "installer_tests.inc"
int wmain(int argc, WCHAR **argv) {
    _setmode(_fileno(stdout), _O_U8TEXT);
    if(argc==2&&!wcscmp(argv[1],L"--private-tests"))return private_tests();
    if(argc==3&&!wcscmp(argv[1],L"--package-test"))return package_test(argv[2]);
    if(argc==3&&!wcscmp(argv[1],L"--private-download-test"))return private_download_test(argv[2]);
    if (argc == 3 && !wcscmp(argv[1], L"--installer-tests")) return installer_tests(argv[2]);
    if (argc == 3 && !wcscmp(argv[1], L"--verify-catalog")) {
        NP_RELEASE r; BYTE hash[32]; WCHAR side[NP_PATH];
        swprintf_s(side,NP_PATH,L"%s.update",argv[2]);
        BOOL ok=np_read_exact(side,&r,sizeof(r)) && np_catalog_signature(&r) && np_hash_file(argv[2],hash) && !memcmp(hash,r.hash,32);
        wprintf(L"Release catalog signature: %s\n",ok ? L"PASS" : L"FAIL"); return ok ? 0:1;
    }
    if (argc == 3 && !wcscmp(argv[1], L"--verify-installer")) {
        NP_RELEASE release; NP_ERROR e = {0};
        BOOL ok = np_verify_installer(argv[2], &release, &e);
        wprintf(L"Offline installer verification: %s error=%lu; image was NOT executed.\n", ok ? L"PASS" : L"FAIL", e.win32);
        return ok ? 0 : 1;
    }
    if (argc == 3 && !wcscmp(argv[1], L"--verify-release")) {
        NP_RELEASE release;
        NP_ERROR e = {0};
        BOOL ok = np_verify_release(argv[2], &release, &e);
        if (ok)
            wprintf(L"Release verification passed: product/x64/version=%lu.%lu.%lu.%lu/size/SHA256/pinned "
                    L"ECDSA signature. File was not executed.\n",
                    release.version[0], release.version[1], release.version[2], release.version[3]);
        else
            wprintf(L"Release verification failed: Win32 0x%08lX %s\n", e.win32, e.operation);
        return ok ? 0 : 1;
    }
    if (argc >= 2 && !wcscmp(argv[1],L"--feature-tests"))return feature_tests(argc==3?argv[2]:NULL);
    if(argc==2&&!wcscmp(argv[1],L"--apps-probe"))return apps_probe();
    if(argc==2&&!wcscmp(argv[1],L"--online-probe"))return online_probe();
    if(argc==2&&!wcscmp(argv[1],L"--transport-probe")) {
        NP_ERROR e={0}; BOOL ok=np_online_transport_probe(&e);
        wprintf(L"Transport probe: success=%d Windows=%lu HTTP=%lu %s\n",ok,e.win32,e.http_status,e.operation);
        return ok ? 0 : 1;
    }
    if(argc==2&&!wcscmp(argv[1],L"--feature-ui"))return np_app_run(GetModuleHandleW(NULL),SW_SHOW,TRUE,6);
    if (argc == 2 && !wcscmp(argv[1], L"--routing-tests"))
        return routing_tests();
    if (argc == 2 && !wcscmp(argv[1], L"--routing-probe"))
        return routing_probe();
    if (argc == 2 && !wcscmp(argv[1], L"--routing-ui"))
        return np_app_run(GetModuleHandleW(NULL), SW_SHOW, TRUE, 5);
    if (argc == 2 && !wcscmp(argv[1], L"--soak"))
        return np_app_run(GetModuleHandleW(NULL), SW_SHOW, TRUE, 4);
    if (argc == 2 && !wcscmp(argv[1], L"--ui-acceptance"))
        return np_app_run(GetModuleHandleW(NULL), SW_SHOW, TRUE, 3);
    if (argc == 2 && !wcscmp(argv[1], L"--perf"))
        return np_app_run(GetModuleHandleW(NULL), SW_SHOW, TRUE, 2);
    if (argc == 2 && !wcscmp(argv[1], L"--gui-smoke"))
        return np_app_run(GetModuleHandleW(NULL), SW_SHOW, TRUE, TRUE);
    if (argc == 2 && !wcscmp(argv[1], L"--preview"))
        return np_app_run(GetModuleHandleW(NULL), SW_SHOW, TRUE, FALSE);
    if (argc == 2 && !wcscmp(argv[1], L"--probe")) {
        NP_BACKEND backend = np_windows_backend();
        NP_SNAPSHOT s;
        NP_ERROR e = {0};
        if (!backend.scan(NULL, &s, &e)) {
            wprintf(L"Read-only probe failed: Windows=%lu CM=0x%lX %s\n", e.win32, e.config_ret, e.operation);
            return 1;
        }
        wprintf(
            L"READ ONLY: %zu physical adapter(s); unique I219-V=%d; unique AX203=%d; local-session-safe=%d\n",
            s.count, np_auto_select(&s), np_auto_wifi(&s), backend.safe_local_session(NULL));
        for (size_t i = 0; i < s.count; ++i)
            wprintf(L"%s\n  PnP: %s\n  State: %s; problem=%lu; DN=0x%lX\n", s.adapters[i].name,
                    s.adapters[i].identity.instance, np_state_text(s.adapters[i].state),
                    s.adapters[i].problem, s.adapters[i].device_status);
        wprintf(L"%s\nNo state changes performed.\n", s.wifi);
        return 0;
    }
    if (argc != 1)
        return 2;
    run_tests();
    return failed ? 1 : 0;
}
