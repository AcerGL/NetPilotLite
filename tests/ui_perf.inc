/* Included only by the read-only test product. Identical workload for the baseline and release. */
#include <psapi.h>
static double perf_ms(void) {
    LARGE_INTEGER q, f;
    QueryPerformanceCounter(&q);
    QueryPerformanceFrequency(&f);
    return 1000.0 * (double)q.QuadPart / (double)f.QuadPart;
}
static double perf_cpu(void) {
    FILETIME c, e, k, u;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    ULARGE_INTEGER kk = {{k.dwLowDateTime, k.dwHighDateTime}}, uu = {{u.dwLowDateTime, u.dwHighDateTime}};
    return (double)(kk.QuadPart + uu.QuadPart) / 10000.0;
}
static int perf_compare(const void *aa, const void *bb) {
    double x = *(const double *)aa, y = *(const double *)bb;
    return (x > y) - (x < y);
}
static void perf_tick(void) {
    if (a.busy || a.loading_images)
        return;
    KillTimer(a.window, 2);
    const int count = 600;
    double timings[600], sum = 0;
    a.dpi = 96;
    set_fonts();
    SetWindowPos(a.window, NULL, 30, 30, 780, 620, SWP_NOZORDER);
    a.prefs.background_mode = 2;
    np_image_resource(200, &a.background_image);
    appearance();
    layout();
    RedrawWindow(a.window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    double cpu = perf_cpu(), begin = perf_ms();
    for (int i = 0; i < count; ++i) {
        double t = perf_ms();
        if (i % 20 == 0)
            SetWindowPos(a.window, NULL, 0, 0, 780 + (i / 20 % 2) * 40, 620,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SendMessageW(a.window, WM_MOUSEWHEEL, MAKEWPARAM(0, (SHORT)(i % 12 < 6 ? -120 : 120)), 0);
        RedrawWindow(a.window, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN);
        GdiFlush();
        timings[i] = perf_ms() - t;
        sum += timings[i];
    }
    double elapsed = perf_ms() - begin, used = perf_cpu() - cpu;
    qsort(timings, count, sizeof(double), perf_compare);
    PROCESS_MEMORY_COUNTERS pm = {sizeof(pm)};
    K32GetProcessMemoryInfo(GetCurrentProcess(), &pm, sizeof(pm));
    wprintf(L"UI_PERF frames=%d wall_ms=%.2f cpu_ms=%.2f one_core_pct=%.2f mean_ms=%.3f p95_ms=%.3f "
            L"max_ms=%.3f GDI_before=%lu GDI_after=%lu working_set_KiB=%zu peak_KiB=%zu mock_writes=%u\n",
            count, elapsed, used, used * 100 / elapsed, sum / count, timings[count * 95 / 100],
            timings[count - 1], before, GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
            pm.WorkingSetSize / 1024, pm.PeakWorkingSetSize / 1024, a.mock_data.writes);
    capture(L"artifacts\\ui-perf.bmp");
    DestroyWindow(a.window);
}
