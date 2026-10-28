#include "dual.h"
#include <stdio.h>
#include <string.h>
#define LIMIT (4*1024*1024)
static BOOL io(HANDLE h,BOOL write,void *buf,DWORD cap,DWORD *done){
    OVERLAPPED o={0};o.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL);if(!o.hEvent)return FALSE;
    BOOL ok=write?WriteFile(h,buf,cap,done,&o):ReadFile(h,buf,cap,done,&o);
    DWORD err=GetLastError();
    if(!ok && err==ERROR_IO_PENDING){
        if(WaitForSingleObject(o.hEvent,2500)==WAIT_OBJECT_0)ok=GetOverlappedResult(h,&o,done,FALSE);
        else{CancelIoEx(h,&o);GetOverlappedResult(h,&o,done,TRUE);SetLastError(ERROR_TIMEOUT);}
        err=GetLastError();
    }
    CloseHandle(o.hEvent);SetLastError(err);return ok;
}
static const char *crlf(const char *p,const char *end){for(;p+1<end;++p)if(p[0]=='\r'&&p[1]=='\n')return p;return NULL;}
BOOL dp_http_decode(const char *raw,size_t size,char **out,DWORD *length){
    *out=NULL;*length=0;
    if(size<15 || size>LIMIT || memcmp(raw,"HTTP/1.1 200",12))return FALSE;
    const char *end=raw+size,*h=crlf(raw,end);if(!h || raw[12]!=' ')return FALSE;
    h+=2;BOOL chunked=FALSE,has_length=FALSE;size_t expected=0;
    while(h<end){const char *line=crlf(h,end);if(!line)return FALSE;
        if(line==h){h+=2;break;}
        size_t n=(size_t)(line-h);
        if(n>=18&&!_strnicmp(h,"Transfer-Encoding:",18)){
            if(chunked)return FALSE;const char *v=h+18;while(v<line&&*v==' ')++v;
            if(line-v!=7||_strnicmp(v,"chunked",7))return FALSE;chunked=TRUE;
        }
        if(n>=15&&!_strnicmp(h,"Content-Length:",15)){
            if(has_length)return FALSE;has_length=TRUE;const char *v=h+15;while(v<line&&*v==' ')++v;
            if(v==line)return FALSE;
            for(;v<line;++v){if(*v<'0'||*v>'9'||expected>LIMIT/10)return FALSE;expected=expected*10+(size_t)(*v-'0');if(expected>LIMIT)return FALSE;}
        }
        h=line+2;
    }
    if((chunked&&has_length)||(!chunked&&!has_length))return FALSE;
    const char *head=h;size_t remaining=(size_t)(end-head);
    char *data=malloc(remaining+1);if(!data)return FALSE;
    size_t used=0;
    if(chunked){
        const char *pos=head;
        for(;;){
            const char *line=crlf(pos,end);if(!line || line==pos || line-pos>6)goto bad;
            size_t n=0;for(const char *v=pos;v<line;++v){unsigned d=*v>='0'&&*v<='9'?*v-'0':*v>='a'&&*v<='f'?*v-'a'+10:*v>='A'&&*v<='F'?*v-'A'+10:16;if(d==16)goto bad;n=n*16+d;}if(n>LIMIT)goto bad;
            pos=line+2;if(!n){if(end-pos!=2||pos[0]!='\r'||pos[1]!='\n')goto bad;break;}
            if(end-pos<(ptrdiff_t)n+2 || pos[n]!='\r' || pos[n+1]!='\n')goto bad;
            memcpy(data+used,pos,n);used+=n;pos+=n+2;
        }
    }else{if(remaining!=expected)goto bad;memcpy(data,head,remaining);used=remaining;}
    data[used]=0;*out=data;*length=(DWORD)used;return TRUE;
bad:free(data);return FALSE;
}
BOOL dp_get(const WCHAR *path,char **json,DWORD *length,DWORD *pid,DWORD *error){
    /* Exactly these GET operations. No configurable host, secret forwarding or mutating endpoints. */
    if(wcscmp(path,L"/version") && wcscmp(path,L"/configs") && wcscmp(path,L"/connections") && wcscmp(path,L"/proxies")){*error=ERROR_ACCESS_DENIED;return FALSE;}
    *json=NULL;*length=0;*error=0;
    HANDLE h=CreateFileW(L"\\\\.\\pipe\\verge-mihomo",GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
    if(h==INVALID_HANDLE_VALUE){*error=GetLastError();return FALSE;}
    ULONG server=0;GetNamedPipeServerProcessId(h,&server);if(pid)*pid=server;
    char request[256];char uri[50];WideCharToMultiByte(CP_UTF8,0,path,-1,uri,50,NULL,NULL);
    int count=sprintf_s(request,256,"GET %s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",uri);
    DWORD n=0;BOOL ok=count>0 && io(h,TRUE,request,(DWORD)count,&n) && n==(DWORD)count;
    char *raw=malloc(LIMIT+1);DWORD used=0;ULONGLONG deadline=GetTickCount64()+10000;
    while(ok && raw && used<LIMIT && GetTickCount64()<deadline){
        BOOL read=io(h,FALSE,raw+used,min(32768,LIMIT-used),&n);
        if(!read){if(GetLastError()==ERROR_BROKEN_PIPE)break;ok=FALSE;break;}
        if(!n)break;used+=n;
    }
    DWORD e=GetLastError();CloseHandle(h);
    ok=ok && raw && used<LIMIT && GetTickCount64()<deadline;
    if(raw)raw[used]=0;
    if(ok)ok=dp_http_decode(raw,used,json,length);
    free(raw);if(!ok)*error=e?e:ERROR_INVALID_DATA;return ok;
}
