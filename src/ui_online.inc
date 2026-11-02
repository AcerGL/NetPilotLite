/* Workers own immutable request snapshots. Only the UI thread touches controls/settings. */
typedef struct ONLINE_JOB {
    HWND window;
    BOOL download, ok, mock, authorize, automatic;
    unsigned attempt;
    WCHAR token[NP_TOKEN_CHARS], boot[40];
    NP_PRIVATE_RELEASE package;
    NP_UPDATE_SETTINGS settings;
    NP_ONLINE_RELEASE release;
    NP_ERROR error;
    WCHAR file[NP_PATH];
} ONLINE_JOB;
static struct {
    BOOL busy, available, verified, started, retry_download, failed, install_after_download;
    BOOL next_auto;
    unsigned attempt;
    WCHAR boot[40];
    NP_PRIVATE_RELEASE package;
    volatile LONG cancel;
    NP_ONLINE_RELEASE release;
    WCHAR file[NP_PATH], status[1000];
} online;
static BOOL online_command(int id);
static void online_render(void) {
    if (!a.settings_content)
        return;
    WCHAR text[256];
    swprintf_s(text, 256, L"Stable · 最近检查：%s",
               a.prefs.update.last_check[0] ? a.prefs.update.last_check : L"尚未检查");
    SetWindowTextW(GetDlgItem(a.settings_content, 610), text);
    SetWindowTextW(
        GetDlgItem(a.settings_content, 611),
        online.status[0]
            ? online.status
            : L"私有 GitHub Release 更新：请先在下方授权；Token 仅保存在本机 CurrentUser 加密文件中。");
    int ids[] = {216, 217, 600, 601, 602, 603, 620, 621, 622, 623};
    for (int i = 0; i < _countof(ids); ++i)
        EnableWindow(GetDlgItem(a.settings_content, ids[i]), !online.busy);
    EnableWindow(GetDlgItem(a.settings_content, 604), online.available && !online.busy && !online.verified);
    EnableWindow(GetDlgItem(a.settings_content, 605),
                 online.verified && !online.busy && a.busy != 2 && !a.mock);
    EnableWindow(GetDlgItem(a.settings_content, 607), online.failed && !online.busy);
    EnableWindow(GetDlgItem(a.settings_content, 608), !online.busy);
}
static void markdown_notes(HWND w, const WCHAR *body) {
    /* Native RichEdit: headings/bullets/emphasis, literal links; no HTML, scripts or automatic navigation. */
    SetWindowTextW(w, body);
    SendMessageW(w, EM_SETSEL, 0, -1);
    CHARFORMAT2W normal = {sizeof(normal)};
    normal.dwMask = CFM_COLOR | CFM_BOLD;
    normal.crTextColor = a.colors.foreground;
    SendMessageW(w, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&normal);
    int pos = 0;
    while (body[pos]) {
        int start = pos;
        while (body[pos] && body[pos] != L'\n')
            ++pos;
        int end = pos;
        if (body[start] == L'#') {
            CHARFORMAT2W bold = normal;
            bold.dwEffects = CFE_BOLD;
            SendMessageW(w, EM_SETSEL, start, end);
            SendMessageW(w, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&bold);
        }
        if (body[pos])
            ++pos;
    }
    SendMessageW(w, EM_SETSEL, 0, 0);
    SendMessageW(w, EM_SETBKGNDCOLOR, 0, a.colors.card);
}
static void online_progress(void *context, ULONGLONG done, ULONGLONG total) {
    HWND w = context;
    static DWORD previous;
    DWORD now = GetTickCount();
    if (now - previous < 100 && done < total)
        return;
    previous = now;
    PostMessageW(w, WM_ONLINE_PROGRESS, (WPARAM)(total ? done * 100 / total : 0), 0);
}
static DWORD WINAPI online_worker(void *arg) {
    ONLINE_JOB *j = arg;
    if (j->mock) {
        np_error(&j->error, ERROR_NOT_SUPPORTED, 0,
                 L"模拟窗口不发起真实更新请求，请使用协议测试检查响应解析");
        j->ok = FALSE;
    } else if (j->authorize) {
        NP_AUTH auth = {0};
        auth.magic = 0x4e504154;
        wcscpy_s(auth.owner, 100, j->settings.owner);
        wcscpy_s(auth.repo, 100, j->settings.repo);
        wcscpy_s(auth.token, NP_TOKEN_CHARS, j->token);
        j->ok = np_auth_validate(&j->settings, j->token, &online.cancel, &j->error);
        if (j->ok && !InterlockedCompareExchange(&online.cancel, 0, 0))
            j->ok = np_auth_store(NULL, &auth, &j->error);
        SecureZeroMemory(&auth, sizeof(auth));
    } else if (j->download)
        j->ok = np_private_download(&j->package, &j->settings, j->file, &online.cancel, online_progress,
                                    j->window, &j->error);
    else {
        j->ok = np_private_check(&j->settings, &j->package, &online.cancel, &j->error);
        if (j->ok)
            j->release = j->package.info;
    }
    SecureZeroMemory(j->token, sizeof(j->token));
    if (!PostMessageW(j->window, WM_ONLINE_DONE, 0, (LPARAM)j)) {
        if (j->file[0])
            np_online_cleanup(j->file);
        free(j);
    }
    return 0;
}
static BOOL online_save_config(void) {
    NP_UPDATE_SETTINGS *s = &a.prefs.update;
    WCHAR owner[100], repo[100];
    GetWindowTextW(GetDlgItem(a.settings_content, 600), owner, 100);
    GetWindowTextW(GetDlgItem(a.settings_content, 601), repo, 100);
    if (!np_repo_valid(owner, repo)) {
        MessageBoxW(a.settings,
                    L"Owner / Repository 只能包含英文、数字、短横线、下划线和点，不能包含路径或 URL。",
                    L"仓库配置", MB_OK);
        return FALSE;
    }
    if (wcscmp(s->owner, owner) || wcscmp(s->repo, repo)) {
        online.available = online.verified = FALSE;
        if (online.file[0])
            np_online_cleanup(online.file);
        online.file[0] = 0;
    }
    wcscpy_s(s->owner, 100, owner);
    wcscpy_s(s->repo, 100, repo);
    s->startup = SendMessageW(GetDlgItem(a.settings_content, 603), BM_GETCHECK, 0, 0) == BST_CHECKED;
    save();
    return TRUE;
}
static void online_start(BOOL download) {
    if (online.busy || a.quit_pending)
        return;
    ONLINE_JOB *j = calloc(1, sizeof(*j));
    if (!j) {
        wcscpy_s(online.status, 1000, L"内存不足，无法启动更新检查，请重试。");
        online.failed = TRUE;
        online.retry_download = download;
        online_render();
        return;
    }
    j->window = a.window;
    j->settings = a.prefs.update;
    j->settings.installer = FALSE;
    j->package = online.package;
    j->automatic = online.next_auto;
    online.next_auto = FALSE;
    j->attempt = online.attempt;
    wcscpy_s(j->boot, 40, online.boot);
    j->download = download;
    j->release = online.release;
    j->mock = a.mock;
    online.busy = TRUE;
    online.failed = FALSE;
    online.retry_download = download;
    InterlockedExchange(&online.cancel, 0);
    ++a.async_jobs;
    wcscpy_s(online.status, 1000, download ? L"正在下载并验证发布者签名…" : L"正在检查 GitHub 稳定版…");
    online_render();
    HANDLE t = CreateThread(NULL, 0, online_worker, j, 0, NULL);
    if (t)
        CloseHandle(t);
    else {
        DWORD thread_error = GetLastError();
        free(j);
        --a.async_jobs;
        online.busy = FALSE;
        online.failed = TRUE;
        swprintf_s(online.status, 1000, L"无法启动更新检查线程，Windows %lu；可重试。", thread_error);
        online_render();
    }
}
#include "ui_update_prompt.inc"
static void online_done(ONLINE_JOB *j) {
    --a.async_jobs;
    online.busy = FALSE;
    if (!j)
        return;
    if (!j->download && !j->authorize) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        swprintf_s(a.prefs.update.last_check, 40, L"%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay,
                   t.wHour, t.wMinute, t.wSecond);
        save();
    }
    if (InterlockedCompareExchange(&online.cancel, 0, 0)) {
        online.install_after_download = FALSE;
        if (j->file[0])
            np_online_cleanup(j->file);
        wcscpy_s(online.status, 1000, L"本次更新已取消。");
    } else if (!j->ok) {
        online.failed = TRUE;
        error_text(&j->error, online.status, 1000);
    } else if (j->authorize) {
        wcscpy_s(
            online.status, 1000,
            L"授权验证成功：已加密保存；可手动检查更新。权限范围请在 GitHub 确认仅本仓库 Contents: Read。");
    } else if (j->download) {
        wcscpy_s(online.file, NP_PATH, j->file);
        online.verified = TRUE;
        wcscpy_s(online.status, 1000,
                 L"下载完成：已验证固定公钥 ECDSA 签名、SHA-256、x64 产品及版本。即将启动独立更新器。");
    } else {
        DWORD current[4] = {NP_VERSION_NUM};
        online.release = j->release;
        online.package = j->package;
        online.available = np_version_compare(j->release.version, current) > 0;
        if (online.available) {
            swprintf_s(online.status, 1000, L"新版本 %s · %s · %.2f MiB\n%s", j->release.tag,
                       j->release.published, (double)j->release.size / 1048576.0, j->release.name);
            message(L"发现新版本，可在“设置 → 更新”查看。不会自动下载或安装。");
        } else
            swprintf_s(online.status, 1000, L"最新稳定版：%s；%s", j->release.tag,
                       np_version_compare(j->release.version, current) == 0 ? L"已经是最新版本。"
                                                                            : L"当前版本较新，不会降级。");
        markdown_notes(GetDlgItem(a.settings_content, 612), j->release.body);
    }
    BOOL confirm_install =
        j->ok && j->download && online.verified && online.install_after_download && !a.quit_pending;
    if (confirm_install)
        online.install_after_download = FALSE;
    BOOL prompt = FALSE, authorize = FALSE;
    if (j->automatic) {
        BOOL terminal = j->ok || j->error.win32 == ERROR_LOGON_FAILURE || j->error.http_status == 403 ||
                        j->error.http_status == 404 || j->attempt >= 3;
        if (terminal) {
            BOOL persisted = np_boot_complete(NULL, j->boot);
            KillTimer(a.window, 7);
            prompt = persisted && j->ok && online.available && !np_ignored(NULL, online.release.version);
            authorize = persisted && !j->ok && j->error.win32 == ERROR_LOGON_FAILURE;
        }
    }
    free(j);
    online_render();
    if (authorize && !a.quit_pending) {
        if (MessageBoxW(a.window,
                        L"自动更新需要授权读取私有仓库。请在设置中粘贴仅限本仓库 Contents: Read 的 "
                        L"Fine-grained Token。现在打开设置？",
                        L"NetPilot Lite · 首次授权", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
            show_settings();
            settings_page(6);
        }
    }
    if (prompt && !a.quit_pending)
        update_prompt();
    if (confirm_install)
        online_command(605);
}
static void online_controls(HWND w) {
    slabel(w, 6, 0, L"当前版本 " NP_VERSION L" · Stable\n原地更新 NetPilotLite.exe，保留绑定与偏好。");
    slabel(w, 6, 62, L"GitHub Owner / Repository");
    HWND c =
        cell(w, 6, 0, 1, 98, 32, L"EDIT", a.prefs.update.owner, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 600);
    SendMessageW(c, EM_SETLIMITTEXT, 99, 0);
    c = cell(w, 6, 1, 1, 98, 32, L"EDIT", a.prefs.update.repo, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 601);
    SendMessageW(c, EM_SETLIMITTEXT, 99, 0);
    sbutton(w, 6, 0, 1, 142, L"保存仓库设置", 602);
    check(w, 6, 188, 603, L"每个 Windows 开机周期自动检查一次", a.prefs.update.startup);
    cell(w, 6, 0, 2, 232, 32, L"STATIC", L"", SS_NOPREFIX, 610);
    sbutton(w, 6, 0, 1, 278, L"检查更新", 216);
    sbutton(w, 6, 1, 1, 278, L"从本地文件更新…", 217);
    cell(w, 6, 0, 2, 330, 98, L"STATIC", L"", SS_NOPREFIX, 611);
    cell(w, 6, 0, 2, 440, 16, PROGRESS_CLASSW, L"", 0, 613);
    sbutton(w, 6, 0, 1, 474, L"下载并安装…", 604);
    sbutton(w, 6, 1, 1, 474, L"安装更新…", 605);
    sbutton(w, 6, 0, 1, 524, L"稍后更新 / 取消", 606);
    sbutton(w, 6, 1, 1, 524, L"查看更新日志", 218);
    sbutton(w, 6, 0, 1, 574, L"重试", 607);
    sbutton(w, 6, 1, 1, 574, L"查看 GitHub Release", 608);
    cell(w, 6, 0, 2, 630, 220, MSFTEDIT_CLASS, L"暂无远端更新日志。",
         ES_MULTILINE | ES_READONLY | WS_VSCROLL | WS_BORDER | WS_TABSTOP, 612);
    markdown_notes(GetDlgItem(a.settings_content, 612),
                   online.release.body[0] ? online.release.body : L"暂无远端更新日志。");
    slabel(w, 6, 870, L"私有仓库首次授权：只选本仓库 · Contents: Read");
    HWND token =
        cell(w, 6, 0, 2, 910, 32, L"EDIT", L"", ES_PASSWORD | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 620);
    SendMessageW(token, EM_SETLIMITTEXT, NP_TOKEN_CHARS - 1, 0);
    sbutton(w, 6, 0, 1, 956, L"验证 / 更换授权", 621);
    sbutton(w, 6, 1, 1, 956, L"移除本机授权", 622);
    sbutton(w, 6, 0, 2, 1004, L"打开 GitHub Token 设置（创建 / 撤销）", 624);
    check(w, 6, 1052, 623, L"随 Windows 登录启动（最小化到托盘）", np_autorun_enabled());
    slabel(w, 6, 1100,
           L"授权仅发往 api.github.com，不发送给下载 CDN。\n撤销远端 Token 请在 GitHub 设置中操作。");
    online_render();
}
static BOOL online_command(int id) {
    if (update_notice && (id == 216 || id == 602 || id == 607 || id == 621)) {
        SetForegroundWindow(update_notice);
        return TRUE;
    }
    if (id == 624) {
        ShellExecuteW(a.settings, L"open", L"https://github.com/settings/personal-access-tokens", NULL, NULL,
                      SW_SHOWNORMAL);
        return TRUE;
    }
    if (id == 622 && !online.busy) {
        NP_ERROR e = {0};
        if (a.mock || !np_auth_remove(NULL, &e))
            wcscpy_s(online.status, 1000, L"授权未移除（模拟模式或文件操作失败）。");
        else
            wcscpy_s(online.status, 1000, L"已移除本机授权；如需使 Token 在所有设备失效，请在 GitHub 撤销。");
        online_render();
        return TRUE;
    }
    if (id == 623) {
        NP_ERROR e = {0};
        BOOL value = SendMessageW(GetDlgItem(a.settings_content, 623), BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (!np_autorun(value, &e)) {
            error_text(&e, online.status, 1000);
            SendMessageW(GetDlgItem(a.settings_content, 623), BM_SETCHECK,
                         np_autorun_enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        online_render();
        return TRUE;
    }
    if (id == 621 && !online.busy && online_save_config()) {
        ONLINE_JOB *j = calloc(1, sizeof(*j));
        if (!j)
            return TRUE;
        j->window = a.window;
        j->settings = a.prefs.update;
        j->mock = a.mock;
        j->authorize = TRUE;
        GetWindowTextW(GetDlgItem(a.settings_content, 620), j->token, NP_TOKEN_CHARS);
        SetWindowTextW(GetDlgItem(a.settings_content, 620), L"");
        if (!np_token_valid(j->token)) {
            SecureZeroMemory(j, sizeof(*j));
            free(j);
            wcscpy_s(online.status, 1000, L"请输入有效格式的 Fine-grained Token（github_pat_ 开头）。");
            online_render();
            return TRUE;
        }
        online.busy = TRUE;
        online.failed = FALSE;
        InterlockedExchange(&online.cancel, 0);
        ++a.async_jobs;
        wcscpy_s(online.status, 1000, L"正在验证私有仓库和 Release 读取权限…");
        HANDLE t = CreateThread(NULL, 0, online_worker, j, 0, NULL);
        if (t)
            CloseHandle(t);
        else {
            SecureZeroMemory(j, sizeof(*j));
            free(j);
            online.busy = FALSE;
            --a.async_jobs;
            wcscpy_s(online.status, 1000, L"无法启动授权线程，请重试。");
        }
        online_render();
        return TRUE;
    }
    if (id == 607) {
        if (online.failed && !online.busy)
            online_start(online.retry_download && online.available);
        return TRUE;
    }
    if (id == 608) {
        WCHAR url[512];
        if (np_repo_valid(a.prefs.update.owner, a.prefs.update.repo)) {
            swprintf_s(url, 512, L"https://github.com/%s/%s/releases", a.prefs.update.owner,
                       a.prefs.update.repo);
            if ((INT_PTR)ShellExecuteW(a.settings, L"open", url, NULL, NULL, SW_SHOWNORMAL) <= 32) {
                wcscpy_s(online.status, 1000, L"无法打开默认浏览器，请手动访问配置仓库的 Releases 页面。");
                online_render();
            }
        }
        return TRUE;
    }
    if (id == 602 || id == 603) {
        if (online_save_config()) {
            if (a.prefs.update.startup && !a.mock)
                SetTimer(a.window, 7, 1500, NULL);
            else
                KillTimer(a.window, 7);
            wcscpy_s(online.status, 1000, L"仓库配置已保存。");
            online_render();
        }
        return TRUE;
    }
    if (id == 216) {
        if (online_save_config()) {
            online.install_after_download = FALSE;
            online.available = online.verified = FALSE;
            if (online.file[0])
                np_online_cleanup(online.file);
            online.file[0] = 0;
            online_start(FALSE);
        }
        return TRUE;
    }
    if (id == 604) {
        if (online.available && !online.verified) {
            online.install_after_download = TRUE;
            online_start(TRUE);
        }
        return TRUE;
    }
    if (id == 606) {
        online.install_after_download = FALSE;
        InterlockedExchange(&online.cancel, 1);
        if (!online.busy) {
            if (online.file[0])
                np_online_cleanup(online.file);
            online.file[0] = 0;
            online.verified = FALSE;
            wcscpy_s(online.status, 1000, L"已选择稍后更新。当前版本继续运行。");
            online_render();
        }
        return TRUE;
    }
    if (id == 605) {
        if (!online.verified || online.busy || a.mock || a.busy == 2)
            return TRUE;
        NP_ERROR e = {0};
        NP_RELEASE checked;
        if (!np_verify_release(online.file, &checked, &e) ||
            memcmp(checked.version, online.release.version, 16)) {
            online.verified = FALSE;
            error_text(&e, online.status, 1000);
            online_render();
            return TRUE;
        }
        save_now();
        if (!np_update_start(a.settings, online.file, &e)) {
            error_text(&e, online.status, 1000);
            online_render();
        } else {
            wcscpy_s(online.status, 1000,
                     L"安装状态：已启动受信任的更新器，等待旧实例正常退出及新版启动验证。");
            online_render();
            message(L"已启动受控更新器，等待本实例正常退出…");
        }
        return TRUE;
    }
    return FALSE;
}

static void online_boot_tick(void) {
    if (a.mock || !a.prefs.update.startup || a.quit_pending) {
        KillTimer(a.window, 7);
        return;
    }
    if (online.busy)
        return;
    if (!np_boot_id(online.boot)) {
        KillTimer(a.window, 7);
        wcscpy_s(online.status, 1000, L"无法读取 Windows 启动标识；请手动检查更新。");
        online_render();
        return;
    }
    if (!np_boot_due(NULL, online.boot)) {
        KillTimer(a.window, 7);
        return;
    }
    if (np_boot_attempt(NULL, online.boot, GetTickCount64(), &online.attempt)) {
        online.next_auto = TRUE;
        online_start(FALSE);
    } else if (online.attempt >= 3) {
        np_boot_complete(NULL, online.boot);
        KillTimer(a.window, 7);
    }
}
