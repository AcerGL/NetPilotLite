#include "lite.h"
int WINAPI wWinMain(HINSTANCE i,HINSTANCE p,PWSTR c,int s){(void)p;(void)c;HANDLE mutex=CreateMutexW(NULL,FALSE,L"Local\\NetPilotLite.UpdateTest.1");if(!mutex)return 1;if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mutex);return 0;}int result=np_app_run(i,s,TRUE,FALSE);CloseHandle(mutex);return result;}
