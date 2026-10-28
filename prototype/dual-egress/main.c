#include "dual.h"
#include "apps.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>
static HWND window,summary,list,nodes,processes,target,refresh;
static DP_STATE *state;static NP_APP_LIST *app_list;static DP_APP plans[DP_APPS];static size_t plan_count;
static BOOL busy;static int view;static UINT dpi=96;static HFONT font;static HBRUSH background;
#define DONE (WM_APP+11)
static int px(int n){return MulDiv(n,(int)dpi,96);}
static HWND control(const WCHAR *cls,const WCHAR *text,DWORD style,int x,int y,int w,int h,int id){HWND c=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),window,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE);return c;}
static void row(int i,int col,const WCHAR *text){if(!col){LVITEMW v={0};v.mask=LVIF_TEXT;v.iItem=i;v.pszText=(LPWSTR)text;ListView_InsertItem(list,&v);}else ListView_SetItemText(list,i,col,(LPWSTR)text);}
static void render_rows(void){
    ListView_DeleteAllItems(list);while(ListView_DeleteColumn(list,0));
    const WCHAR *a[]={L"应用",L"协议",L"命中规则",L"节点 / 策略链首项",L"预期出口",L"Mihomo 入站源地址",L"实际物理出口",L"可信度 / 限制",L"进程完整路径",L"出站类别（链推断）"};
    const WCHAR *b[]={L"核心进程 PID",L"协议",L"Windows 本地源",L"远端",L"源地址归属",L"路由推测",L"物理出口",L"可信度 / 限制"};
    const WCHAR *c[]={L"应用路径",L"目标",L"状态"};
    for(int i=0;i<(view==0?10:view==1?8:3);++i){LVCOLUMNW col={0};col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=(LPWSTR)(view==0?a[i]:view==1?b[i]:c[i]);col.cx=px(view==2&&i==0?450:i==7||i==8?300:150);ListView_InsertColumn(list,i,&col);}
    if(view==2){for(size_t i=0;i<plan_count;++i){row((int)i,0,plans[i].path);row((int)i,1,plans[i].target==0?L"Ethernet Direct":plans[i].target==1?L"Wi-Fi Proxy":L"原 Mihomo 默认规则");row((int)i,2,L"仅预览，未应用");}return;}
    if(!state)return;
    if(view==0)for(size_t i=0;i<state->flow_count;++i){DP_FLOW *f=&state->flows[i];row((int)i,0,f->app[0]?f->app:L"进程未报告");row((int)i,1,f->protocol);row((int)i,2,f->rule);row((int)i,3,f->node);row((int)i,4,f->expected);row((int)i,5,f->inbound_source);row((int)i,6,f->actual);row((int)i,7,f->confidence);row((int)i,8,f->path[0]?f->path:L"未报告");row((int)i,9,!wcscmp(f->node,L"DIRECT")?L"DIRECT":!wcscmp(f->node,L"REJECT")?L"REJECT":f->node[0]?L"PROXY / 自定义出站，需核对":L"未知");}
    else {int k=0;for(size_t i=0;i<state->sockets.count;++i){NP_CONNECTION *c1=&state->sockets.rows[i];if(!state->core_pid||c1->pid!=state->core_pid)continue;WCHAR pid[30];swprintf_s(pid,30,L"%lu",c1->pid);row(k,0,pid);row(k,1,c1->protocol);row(k,2,c1->local_text);row(k,3,c1->remote_text);row(k,4,c1->source);row(k,5,c1->route);row(k,6,L"未验证");row(k,7,c1->note);++k;}}
}
typedef struct RESULT {DP_STATE *state;NP_APP_LIST *apps;} RESULT;
static DWORD WINAPI audit_worker(void *unused){(void)unused;RESULT *r=calloc(1,sizeof(*r));if(r){r->state=calloc(1,sizeof(DP_STATE));if(r->state)dp_audit(r->state);r->apps=np_apps_scan(TRUE);}PostMessageW(window,DONE,0,(LPARAM)r);return 0;}
static void start(void){if(busy)return;busy=TRUE;EnableWindow(refresh,FALSE);SetWindowTextW(summary,L"正在只读查询配置、命名管道和 Windows 连接表…");HANDLE t=CreateThread(NULL,0,audit_worker,NULL,0,NULL);if(t)CloseHandle(t);else{busy=FALSE;EnableWindow(refresh,TRUE);}}
static void add(const WCHAR *path){if(!np_app_exe(path)){MessageBoxW(window,L"请选择真实 EXE；包身份或无法解析的进程不能直接转换成 PROCESS-PATH。",L"应用选择",MB_OK);return;}size_t i;for(i=0;i<plan_count;++i)if(!_wcsicmp(plans[i].path,path))break;if(i==DP_APPS)return;if(i==plan_count)++plan_count;wcscpy_s(plans[i].path,NP_PATH,path);plans[i].target=(int)SendMessageW(target,CB_GETCURSEL,0,0);view=2;render_rows();}
static void export_preview(void){if(!state||busy)return;WCHAR path[NP_PATH]=L"NetPilotLite-dual-preview.js";OPENFILENAMEW o={sizeof(o)};o.hwndOwner=window;o.lpstrFilter=L"Clash Verge Script 预览 (*.js)\0*.js\0\0";o.lpstrFile=path;o.nMaxFile=NP_PATH;o.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;o.lpstrDefExt=L"js";if(!GetSaveFileNameW(&o))return;NP_ERROR e={0};int n=(int)SendMessageW(nodes,CB_GETCURSEL,0,0);if(n<0||!dp_export(path,state,(size_t)n,plans,plan_count,&e)){WCHAR text[300];swprintf_s(text,300,L"%s\nWindows=%lu",e.operation,e.win32);MessageBoxW(window,text,L"未导出",MB_ICONERROR);return;}MessageBoxW(window,L"只导出了预览脚本，没有应用。请先运行匹配版本 Core 的配置检查，再人工审查最终 YAML。当前热点故障保护仍未验证。",L"预览导出",MB_OK);}
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    switch(m){case WM_CREATE:{window=w;dpi=GetDpiForWindow(w);font=CreateFontW(-px(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        control(L"STATIC",L"NetPilot Lite · 双网智能分流",0,18,12,700,30,0);
        refresh=control(L"BUTTON",L"刷新只读诊断",WS_TABSTOP,18,48,145,32,1);
        HWND disabled=control(L"BUTTON",L"启用分流（待验证）",0,180,48,190,32,10);EnableWindow(disabled,FALSE);
        control(L"BUTTON",L"查看 Mihomo 连接",WS_TABSTOP,390,48,180,32,2);control(L"BUTTON",L"查看核心 sockets",WS_TABSTOP,588,48,180,32,3);control(L"BUTTON",L"查看预览规则",WS_TABSTOP,786,48,160,32,4);
        summary=control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|WS_VSCROLL,18,94,990,206,0);
        control(L"STATIC",L"Wi-Fi 副本来源节点：",0,18,314,200,24,0);nodes=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,218,308,380,300,0);
        control(L"BUTTON",L"导出 Script 预览…",WS_TABSTOP,620,308,210,32,5);
        control(L"STATIC",L"运行中的应用：",0,18,354,180,24,0);processes=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,190,348,408,300,0);
        target=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,620,348,250,180,0);
        SendMessageW(target,CB_ADDSTRING,0,(LPARAM)L"Ethernet Direct");SendMessageW(target,CB_ADDSTRING,0,(LPARAM)L"Wi-Fi Proxy");SendMessageW(target,CB_ADDSTRING,0,(LPARAM)L"System Default（原规则）");SendMessageW(target,CB_SETCURSEL,1,0);
        control(L"BUTTON",L"添加/更改",WS_TABSTOP,884,348,124,32,6);control(L"BUTTON",L"从 EXE 选择…",WS_TABSTOP,18,390,180,32,7);control(L"BUTTON",L"移除选中预览规则",WS_TABSTOP,218,390,220,32,8);
        control(L"STATIC",L"暂停/恢复：本原型未应用任何配置，关闭即可。源地址归属 ≠ 已验证物理出口。",0,460,396,540,24,0);
        list=control(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|WS_BORDER,18,438,990,270,9);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_GRIDLINES);
        ListView_SetBkColor(list,RGB(244,239,252));ListView_SetTextBkColor(list,RGB(244,239,252));render_rows();start();return 0;}
    case WM_SIZE:if(list){MoveWindow(list,px(18),px(438),max(px(500),LOWORD(lp)-px(36)),max(px(100),HIWORD(lp)-px(456)),TRUE);MoveWindow(summary,px(18),px(94),max(px(500),LOWORD(lp)-px(36)),px(206),TRUE);}return 0;
    case WM_COMMAND:switch(LOWORD(wp)){case 1:start();break;case 2:view=0;render_rows();break;case 3:view=1;render_rows();break;case 4:view=2;render_rows();break;case 5:export_preview();break;case 6:{int sel=(int)SendMessageW(processes,CB_GETCURSEL,0,0);LRESULT i=sel>=0?SendMessageW(processes,CB_GETITEMDATA,sel,0):-1;if(app_list&&i>=0&&(size_t)i<app_list->count)add(app_list->rows[i].path);break;}case 7:{WCHAR path[NP_PATH]=L"";OPENFILENAMEW o={sizeof(o)};o.hwndOwner=w;o.lpstrFilter=L"Windows EXE\0*.exe\0\0";o.lpstrFile=path;o.nMaxFile=NP_PATH;o.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&o))add(path);break;}case 8:if(view==2){int i=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(i>=0&&(size_t)i<plan_count){memmove(plans+i,plans+i+1,(plan_count-i-1)*sizeof(*plans));--plan_count;render_rows();}}break;}return 0;
    case DONE:{RESULT *r=(RESULT*)lp;busy=FALSE;EnableWindow(refresh,TRUE);if(r){free(state);state=r->state;np_apps_free(app_list);app_list=r->apps;free(r);}if(!state){SetWindowTextW(summary,L"诊断失败：内存不足");return 0;}SetWindowTextW(summary,state->summary);SendMessageW(nodes,CB_RESETCONTENT,0,0);for(size_t i=0;i<state->node_count;++i){WCHAR text[400];swprintf_s(text,400,L"%s [%s] %s",state->nodes[i].name,state->nodes[i].type,state->nodes[i].eligible?L"可生成绑定副本（未验证）":L"不支持/链式节点");SendMessageW(nodes,CB_ADDSTRING,0,(LPARAM)text);}SendMessageW(nodes,CB_SETCURSEL,0,0);SendMessageW(processes,CB_RESETCONTENT,0,0);if(app_list)for(size_t i=0;i<app_list->count;++i)if(app_list->rows[i].path[0]){LRESULT index=SendMessageW(processes,CB_ADDSTRING,0,(LPARAM)app_list->rows[i].name);SendMessageW(processes,CB_SETITEMDATA,index,i);}SendMessageW(processes,CB_SETCURSEL,0,0);render_rows();return 0;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetBkColor((HDC)wp,RGB(235,230,248));SetTextColor((HDC)wp,RGB(44,45,79));return (LRESULT)background;
    case WM_CLOSE:if(busy){MessageBoxW(w,L"只读查询尚未完成，请稍等。每个管道请求最多 10 秒。",L"退出",MB_OK);return 0;}DestroyWindow(w);return 0;
    case WM_DESTROY:free(state);np_apps_free(app_list);DeleteObject(font);PostQuitMessage(0);return 0;
    }return DefWindowProcW(w,m,wp,lp);
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE old,PWSTR command,int show){
    (void)old;(void)command;int argc;WCHAR **argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc==3&&!wcscmp(argv[1],L"--audit")){DP_STATE *s=calloc(1,sizeof(*s));BOOL ok=s&&dp_audit(s)&&dp_write_report(argv[2],s);free(s);LocalFree(argv);return ok?0:1;}
    if(argc==2&&!wcscmp(argv[1],L"--test")){LocalFree(argv);return dp_tests();}
    if(argc!=1){LocalFree(argv);return ERROR_INVALID_PARAMETER;}LocalFree(argv);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);INITCOMMONCONTROLSEX ic={sizeof(ic),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&ic);
    background=CreateSolidBrush(RGB(235,230,248));WNDCLASSW c={0};c.hInstance=h;c.lpfnWndProc=proc;c.hbrBackground=background;c.hCursor=LoadCursorW(NULL,IDC_ARROW);c.hIcon=LoadIconW(h,MAKEINTRESOURCEW(1));c.lpszClassName=L"NetPilotLite.DualEgress.Prototype";RegisterClassW(&c);
    dpi=GetDpiForSystem();window=CreateWindowW(c.lpszClassName,L"NetPilot Lite · 双出口只读原型",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,px(1050),px(780),NULL,NULL,h,NULL);if(!window)return 1;ShowWindow(window,show);
    MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}DeleteObject(background);return 0;
}
