/* Download transport is mocked; production installer is verified as data and never run. */
static int installer_tests(const WCHAR *fixture) {
    NP_RELEASE signed_release;
    NP_ERROR e = {0};
    CHECK(np_verify_installer(fixture, &signed_release, &e));
    CHECK(!np_verify_release(fixture, &signed_release, &e));
    CHECK(np_verify_installer(fixture, &signed_release, &e));
    CHECK(!np_release_signature(&signed_release));
    CHECK(np_installer_signature(&signed_release));
    NP_UPDATE_SETTINGS settings = {0};
    wcscpy_s(settings.owner,100,NP_GITHUB_OWNER); wcscpy_s(settings.repo,100,NP_GITHUB_REPO);
    wcscpy_s(settings.channel,16,L"Stable"); settings.installer=TRUE;
    char json[4096];
    sprintf_s(json,4096,"{\"tag_name\":\"v%lu.%lu.%lu\",\"name\":\"Installer test\",\"body\":\"notes\",\"published_at\":\"2026-10-27\",\"draft\":false,\"prerelease\":false,\"assets\":[{\"name\":\"NetPilotLite-Offline-Setup.exe\",\"browser_download_url\":\"https://github.com/AcerGL/NetPilotLite/releases/download/v%lu.%lu.%lu/NetPilotLite-Offline-Setup.exe\",\"size\":%llu},{\"name\":\"NetPilotLite-Offline-Setup.exe.update\",\"browser_download_url\":\"https://github.com/AcerGL/NetPilotLite/releases/download/v%lu.%lu.%lu/NetPilotLite-Offline-Setup.exe.update\",\"size\":128}]}",
              signed_release.version[0],signed_release.version[1],signed_release.version[2],
              signed_release.version[0],signed_release.version[1],signed_release.version[2],signed_release.size,
              signed_release.version[0],signed_release.version[1],signed_release.version[2]);
    NP_ONLINE_RELEASE *r=calloc(1,sizeof(*r));
    CHECK(np_release_json(json,strlen(json),&settings,r,&e));
    CHECK(r->installer && r->size==signed_release.size);
    settings.installer=FALSE;
    CHECK(!np_release_json(json,strlen(json),&settings,r,&e));
    settings.installer=TRUE;
    CHECK(np_release_json(json,strlen(json),&settings,r,&e));
    CHECK(!np_asset_url(L"https://github.com/AcerGL/NetPilotLite/releases/download/v1.3.0/NetPilotLite-Web-Setup.exe",&settings,L"v1.3.0",L"NetPilotLite-Web-Setup.exe"));
    CHECK(!np_asset_url(L"https://evil.invalid/NetPilotLite-Offline-Setup.exe",&settings,r->tag,L"NetPilotLite-Offline-Setup.exe"));
    WCHAR root[NP_PATH], relative[100], file[NP_PATH];
    swprintf_s(relative,100,L"artifacts\\installer-http-%lu-%llu",GetCurrentProcessId(),GetTickCount64());
    GetFullPathNameW(relative,NP_PATH,root,NULL); CHECK(CreateDirectoryW(root,NULL));
    np_http_fixture.active=TRUE; np_http_fixture.status=200; np_http_fixture.json=json;
    GetFullPathNameW(fixture,NP_PATH,np_http_fixture.exe,NULL);
    wcscpy_s(np_http_fixture.directory,NP_PATH,root);
    volatile LONG cancel=0;
    CHECK(np_online_check(&settings,r,&cancel,&e));
    CHECK(np_online_download(r,&settings,file,&cancel,NULL,NULL,&e));
    CHECK(np_online_verify(file,r,&e));
    /* Installer purpose cannot be changed to the portable EXE update purpose. */
    r->installer=FALSE; CHECK(!np_online_verify(file,r,&e)); r->installer=TRUE;
    CHECK(np_online_verify(file,r,&e));
    r->version[0]++; CHECK(!np_online_verify(file,r,&e)); r->version[0]--;
    wcscpy_s(r->digest,80,L"sha256:0000000000000000000000000000000000000000000000000000000000000000");
    CHECK(!np_online_verify(file,r,&e)); r->digest[0]=0;
    HANDLE f=CreateFileW(file,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    BYTE corrupt=0; DWORD n; CHECK(f!=INVALID_HANDLE_VALUE && WriteFile(f,&corrupt,1,&n,NULL));
    if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);
    CHECK(!np_online_verify(file,r,&e));
    np_online_cleanup(file); CHECK(GetFileAttributesW(file)==INVALID_FILE_ATTRIBUTES);
    np_http_fixture.interrupt=TRUE;
    CHECK(!np_online_download(r,&settings,file,&cancel,NULL,NULL,&e) && !file[0]);
    np_http_fixture.interrupt=FALSE; cancel=1;
    CHECK(!np_online_download(r,&settings,file,&cancel,NULL,NULL,&e) && !file[0]); cancel=0;
    DWORD errors[]={404,403,429,407,500,12002,12007};
    for(int i=0;i<_countof(errors);++i) {
        np_http_fixture.status=errors[i];
        CHECK(!np_online_check(&settings,r,&cancel,&e));
    }
    np_http_fixture.active=FALSE;
    signed_release.signature[3]^=1; CHECK(!np_installer_signature(&signed_release));
    free(r);
    wprintf(L"Installer protocol: %u passed, %u failed; no installer or network adapters executed.\n",passed,failed);
    return failed ? 1:0;
}
