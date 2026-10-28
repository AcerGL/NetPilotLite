#include "dual.h"
#include <stdio.h>
int dp_tests(void){
    unsigned pass=0,fail=0;char *out=NULL;DWORD n;
#define T(x) do{if(x)++pass;else ++fail;}while(0)
    const char *plain="HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";
    T(dp_http_decode(plain,strlen(plain),&out,&n)&&n==2&&!strcmp(out,"{}"));free(out);
    const char *chunk="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}\r\n0\r\n\r\n";
    T(dp_http_decode(chunk,strlen(chunk),&out,&n)&&n==2&&!strcmp(out,"{}"));free(out);
    const char *bad[]={
        "HTTP/1.1 401 Unauthorized\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFF\r\na",
        "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n+2\r\n{}\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\njunk",
        "HTTP/1.1 200 OK\r\n\r\n{}"
    };
    for(size_t i=0;i<_countof(bad);++i)T(!dp_http_decode(bad[i],strlen(bad[i]),&out,&n));
    for(size_t i=0;i<strlen(plain);++i){if(dp_http_decode(plain,i,&out,&n)){++fail;free(out);break;}if(i==strlen(plain)-1)++pass;}
    DWORD error,pid;T(!dp_get(L"/restart",&out,&n,&pid,&error)&&error==ERROR_ACCESS_DENIED);
    T(!dp_get(L"/configs?force=true",&out,&n,&pid,&error)&&error==ERROR_ACCESS_DENIED);
    WCHAR directory[NP_PATH],fixture[NP_PATH],exported[NP_PATH];GetModuleFileNameW(NULL,directory,NP_PATH);
    WCHAR *slash=wcsrchr(directory,L'\\');if(!slash)return 1;slash[1]=0;
    swprintf_s(fixture,NP_PATH,L"%sdual-test-%lu.yaml",directory,GetCurrentProcessId());
    swprintf_s(exported,NP_PATH,L"%sdual-test-%lu.js",directory,GetCurrentProcessId());
    const char *yaml="mode: global\nproxies:\n - {name: 'demo\"node', type: vmess, password: TEST_ONLY_SECRET, udp: true}\n - {name: chained, type: vmess, dialer-proxy: other}\n - {name: multi, type: vmess, mptcp: true}\nrules: ['MATCH,DIRECT']\n";
    DP_STATE *s=calloc(1,sizeof(*s));if(!s)return 1;
    T(np_write_exact(fixture,yaml,(DWORD)strlen(yaml)));
    T(dp_read_config(fixture,s)&&s->config_valid&&s->node_count==3&&s->rule_count==1);
    T(s->nodes[0].eligible&&s->nodes[0].udp&&!s->nodes[1].eligible&&!s->nodes[2].eligible);
    s->adapters.count=2;s->adapters.adapters[0].physical_ethernet=TRUE;s->adapters.adapters[0].intel_i219=TRUE;
    s->adapters.adapters[1].physical_wifi=TRUE;s->adapters.adapters[1].intel_ax203=TRUE;
    s->adapters.adapters[0].identity.guid.Data1=1;s->adapters.adapters[1].identity.guid.Data1=2;
    wcscpy_s(s->adapters.adapters[0].alias,NP_NAME_LEN,L"校园 Ethernet");wcscpy_s(s->adapters.adapters[1].alias,NP_NAME_LEN,L"手机 Wi-Fi");
    DP_APP a={L"C:\\演示\\Game.exe",0};NP_ERROR e={0};
    NP_BACKEND backend=np_windows_backend();T(!backend.set_enabled(NULL,NULL,FALSE,&e)&&e.win32==ERROR_ACCESS_DENIED);
    T(dp_export(exported,s,0,&a,1,&e));
    BYTE before[32],after[32];T(np_hash_file(exported,before));
    T(!dp_export(exported,s,0,&a,1,&e)&&e.win32==ERROR_FILE_EXISTS);
    T(np_hash_file(exported,after)&&!memcmp(before,after,32));
    HANDLE f=CreateFileW(exported,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);char content[32768]={0};DWORD got=0;
    BOOL read=f!=INVALID_HANDLE_VALUE&&ReadFile(f,content,sizeof(content)-1,&got,NULL);if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);
    T(read&&strstr(content,"demo\\\"node")&&strstr(content,"C:\\\\")&&!strstr(content,"TEST_ONLY_SECRET"));
    T(!dp_export(exported,s,1,&a,1,&e));
    wcscpy_s(s->global_interface,128,L"WLAN");T(!dp_export(exported,s,0,&a,1,&e));s->global_interface[0]=0;
    s->config_valid=FALSE;T(!dp_export(exported,s,0,&a,1,&e));s->config_valid=TRUE;
    s->adapters.count=3;s->adapters.adapters[2]=s->adapters.adapters[0];T(!dp_export(exported,s,0,&a,1,&e));
    /* Only fixture files created above are deleted; no user or live config is touched. */
    DeleteFileW(fixture);DeleteFileW(exported);
    const char *duplicate="interface-name: WLAN\ninterface-name: Other\nproxies: []\nrules: []\n";
    T(np_write_exact(fixture,duplicate,(DWORD)strlen(duplicate)));
    T(!dp_read_config(fixture,s)&&!s->config_valid);DeleteFileW(fixture);free(s);
    char text[256];sprintf_s(text,256,"Native readonly protocol/YAML/export tests: %u passed, %u failed. Synthetic fixtures only; no live API requests or network changes.\n",pass,fail);
    swprintf_s(fixture,NP_PATH,L"%sdual-native-tests.txt",directory);
    FILE *report=NULL;_wfopen_s(&report,fixture,L"wb");if(report){fwrite(text,1,strlen(text),report);fclose(report);}return fail?1:0;
}
