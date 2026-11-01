#ifndef ProductVersion
#define ProductVersion "1.3.1"
#endif
; IMMUTABLE: change only for a different product, never for an upgrade.
#define ProductId "{C47B553D-22CE-4CDD-BA10-2930FC8266D4}"
[Setup]
AppId={{C47B553D-22CE-4CDD-BA10-2930FC8266D4}
AppName=NetPilot Lite
AppVersion={#ProductVersion}
AppPublisher=AcerGL
AppPublisherURL=https://github.com/AcerGL/NetPilotLite
DefaultDirName={autopf}\NetPilot Lite
DefaultGroupName=NetPilot Lite
DisableProgramGroupPage=yes
DisableDirPage=no
UsePreviousAppDir=yes
UsePreviousTasks=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
SetupArchitecture=x64
MinVersion=10.0
WizardStyle=modern
WizardImageFile=wizard.bmp
WizardSmallImageFile=small.bmp
SetupIconFile=..\..\assets\NetPilotLite.ico
UninstallDisplayIcon={app}\NetPilotLite.exe
OutputDir=..\..\dist
OutputBaseFilename=NetPilotLite-Offline-Setup
VersionInfoVersion={#ProductVersion}.0
VersionInfoProductName=NetPilot Lite Offline Setup
VersionInfoProductVersion={#ProductVersion}.0
VersionInfoDescription=NetPilot Lite Offline Setup
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
SetupMutex=Global\NetPilotLite.Install.1
SetupLogging=yes
DisableWelcomePage=no
AlwaysRestart=no
Uninstallable=yes
[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "zhcn"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
[Files]
Source: "..\..\dist\NetPilotLite.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\dist\NetPilotLite.exe.update"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\bin\Release\InstallGuard.dll"; Flags: dontcopy
Source: "..\..\dist\NetPilotLite.exe"; DestName: "candidate.exe"; Flags: dontcopy
Source: "..\..\dist\NetPilotLite.exe.update"; DestName: "candidate.exe.update"; Flags: dontcopy
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\assets\NOTICE.md"; DestDir: "{app}"; DestName: "ASSET-NOTICE.md"; Flags: ignoreversion
[Icons]
Name: "{autoprograms}\NetPilot Lite"; Filename: "{app}\NetPilotLite.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\NetPilot Lite"; Filename: "{app}\NetPilotLite.exe"; WorkingDir: "{app}"; Tasks: desktopicon
[Run]
Filename: "{app}\NetPilotLite.exe"; Description: "{cm:LaunchProgram,NetPilot Lite}"; Flags: nowait postinstall skipifsilent runascurrentuser
[CustomMessages]
en.Scope=Only program files are installed. Network adapters, proxies and user settings are not changed.
zhcn.Scope=只部署程序文件，不修改网卡、代理、主题、背景或用户设置。
en.Migrate=Explicitly migrate a signed portable copy in the selected folder
zhcn.Migrate=明确选择：迁移所选目录中有有效签名的便携版
en.Repair=The same version is installed. Repair program files while keeping all user settings?
zhcn.Repair=当前版本相同。是否修复程序文件并保留全部用户设置？
en.Legacy=An older NetPilot MSI registration was detected. This installer will not remove it. Cancel and review its recovery state before manually uninstalling the old product. Continue installing Lite separately?
zhcn.Legacy=检测到旧 NetPilot MSI 安装记录。本安装器不会移除它。建议取消并检查旧版恢复状态后手动卸载；是否仍单独安装 Lite？
en.Pending=A previous installation did not finish. Restore the verified old EXE now, then rerun this installer to repair installation metadata?
zhcn.Pending=检测到未完成的安装。现在恢复已验证的旧 EXE，然后重新运行安装器修复安装登记？
en.Close=Save your work. Setup will request NetPilot Lite to exit normally. Continue?
zhcn.Close=请保存工作。安装器将请求 NetPilot Lite 正常退出。是否继续？
en.RemoveData=Program files were removed. User data is kept. Open the current user's NetPilotLite data folder to review and delete it manually? If UAC used another account, sign in as the original user instead.
zhcn.RemoveData=程序已卸载，用户数据已保留。是否打开当前账户的 NetPilotLite 数据目录，由你选择删除？如果 UAC 使用了其他管理员账户，请改用原用户账户清理。
[Code]
const UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#ProductId}_is1';
var ModePage: TInputOptionWizardPage;
    OldPath: String;
    Armed, Completed, HadOld: Boolean;
function NpVerify(Path: String): Cardinal; external 'NpVerify@files:InstallGuard.dll stdcall setuponly';
function NpPath(Path: String): Cardinal; external 'NpPath@files:InstallGuard.dll stdcall setuponly';
function NpEqual(Old, New: String): Cardinal; external 'NpEqual@files:InstallGuard.dll stdcall setuponly';
function NpCompare(Old, New: String): Cardinal; external 'NpCompare@files:InstallGuard.dll stdcall setuponly';
function NpClose(Path: String): Cardinal; external 'NpClose@files:InstallGuard.dll stdcall setuponly';
function NpJournal(Path: String): Cardinal; external 'NpJournal@files:InstallGuard.dll stdcall setuponly';
function NpBackup(Path: String): Cardinal; external 'NpBackup@files:InstallGuard.dll stdcall setuponly';
function NpRestore(Path: String): Cardinal; external 'NpRestore@files:InstallGuard.dll stdcall setuponly';
function NpCleanup(Path: String): Cardinal; external 'NpCleanup@files:InstallGuard.dll stdcall setuponly';
function NpHealth(Path: String): Cardinal; external 'NpHealth@files:InstallGuard.dll stdcall setuponly';
function Fail(Action: String; Code: Cardinal): String;
begin
  Result := Action + ': Windows error ' + IntToStr(Code);
  Log('NetPilot phase=' + Action + ' context=administrator result=failed code=' + IntToStr(Code));
end;
function Marker: String;
begin Result := ExpandConstant('{app}\.NetPilotLite-install-pending'); end;
function LegacyAt(Root: Integer): Boolean;
var Keys: TArrayOfString; I: Integer; Name: String; Msi: Cardinal;
begin
  Result := False;
  if RegGetSubkeyNames(Root, 'Software\Microsoft\Windows\CurrentVersion\Uninstall', Keys) then
    for I := 0 to GetArrayLength(Keys)-1 do
      if RegQueryStringValue(Root, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\' + Keys[I], 'DisplayName', Name) and
         RegQueryDWordValue(Root, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\' + Keys[I], 'WindowsInstaller', Msi) then
        if (Name = 'NetPilot') and (Msi = 1) then Result := True;
end;
function LegacyPresent: Boolean;
begin Result := LegacyAt(HKLM64) or LegacyAt(HKLM32) or LegacyAt(HKCU); end;
procedure InitializeWizard;
begin
  RegQueryStringValue(HKLM64, UninstallKey, 'Inno Setup: App Path', OldPath);
  ModePage := CreateInputOptionPage(wpSelectDir, 'NetPilot Lite', CustomMessage('Scope'), '', False, False);
  ModePage.Add(CustomMessage('Migrate'));
  if LegacyPresent then
    if SuppressibleMsgBox(CustomMessage('Legacy'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) <> IDYES then Abort;
end;
function NonEmptyDirectory(Dir: String): Boolean;
var Find: TFindRec;
begin
  Result := False;
  if FindFirst(AddBackslash(Dir) + '*', Find) then begin
    try
      repeat
        if (Find.Name <> '.') and (Find.Name <> '..') then begin Result := True; break; end;
      until not FindNext(Find);
    finally FindClose(Find); end;
  end;
end;
function PrepareToInstall(var NeedsRestart: Boolean): String;
var Exe, Candidate, Dir, CurrentVersion: String; Code: Cardinal;
begin
  Result := ''; Dir := ExpandConstant('{app}'); Exe := Dir + '\NetPilotLite.exe';
  Code := NpPath(Dir);
  if Code <> 0 then begin Result := Fail('Validate local non-reparse path', Code); exit; end;
  if (OldPath <> '') and (CompareText(RemoveBackslashUnlessRoot(OldPath), RemoveBackslashUnlessRoot(Dir)) <> 0) then begin
    Result := 'Existing installation must be upgraded in its registered directory: ' + OldPath; exit;
  end;
  if FileExists(Marker) then begin
    if not DirExists(Dir + '\.NetPilotLite-install-backup') then begin
      Code := NpVerify(Exe);
      if Code = 0 then begin DeleteFile(Marker); Result := 'Verified incomplete first installation. Rerun Setup for repair.'; end
      else Result := Fail('Incomplete first install; retain log and use a new empty directory', Code);
      exit;
    end;
    if SuppressibleMsgBox(CustomMessage('Pending'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES then begin
      Code := NpRestore(Dir); Log('NetPilot phase=recovery result=' + IntToStr(Code));
      if Code = 0 then begin
        DeleteFile(Marker); NpCleanup(Dir);
        Result := 'Old program restored. Rerun Setup to repair the installation record.';
      end else Result := Fail('Recovery; retain backup and log', Code);
    end else Result := 'Incomplete installation requires recovery; no files changed.';
    exit;
  end;
  if DirExists(Dir + '\.NetPilotLite-install-backup') then begin
    Result := 'An installation backup exists. Preserve it and review the installation log before proceeding.'; exit;
  end;
  ExtractTemporaryFile('candidate.exe'); ExtractTemporaryFile('candidate.exe.update');
  Candidate := ExpandConstant('{tmp}\candidate.exe');
  Code := NpVerify(Candidate);
  if Code <> 0 then begin Result := Fail('Verify embedded candidate', Code); exit; end;
  HadOld := FileExists(Exe);
  if HadOld then begin
    if DirExists(Dir + '\src') or DirExists(Dir + '\obj') or FileExists(Dir + '\NetPilotLite.sln') then begin
      Result := 'Development source directory is not an installation target.'; exit;
    end;
    if (OldPath = '') and not ModePage.Values[0] then begin
      Result := 'This is a portable or development copy. Explicit portable migration is required; unsigned builds are refused.'; exit;
    end;
    if FileExists(Dir + '\settings.ini') then begin
      Result := 'Legacy sidecar settings found. Back up and migrate them to LOCALAPPDATA\NetPilotLite before migration; they will not be overwritten.'; exit;
    end;
    Code := NpCompare(Exe, Candidate);
    if Code = 183 then begin
      if SuppressibleMsgBox(CustomMessage('Repair'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) <> IDYES then begin Result := 'Repair cancelled'; exit; end;
    end else if Code <> 0 then begin Result := Fail('Verify old identity / refuse downgrade', Code); exit; end;
    GetVersionNumbersString(Exe, CurrentVersion);
    Log('NetPilot current=' + CurrentVersion + ' target={#ProductVersion}');
  end else if DirExists(Dir) and (OldPath = '') then begin
    { Empty/new directories are accepted, existing arbitrary contents are not. }
    if NonEmptyDirectory(Dir) then begin
      Result := 'Unknown/development/legacy installation directory; select a new empty folder.'; exit;
    end;
  end;
  if SuppressibleMsgBox(CustomMessage('Close'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDYES) <> IDYES then begin Result := 'Cancelled'; exit; end;
  Code := NpClose(Exe);
  if Code <> 0 then begin Result := Fail('Wait for normal process exit (never force kill)', Code); exit; end;
  if HadOld then begin
    Code := NpBackup(Dir);
    if Code <> 0 then begin Result := Fail('Backup old signed image', Code); exit; end;
  end;
  ForceDirectories(Dir);
  Code := NpJournal(Dir);
  if Code <> 0 then begin Result := Fail('Flush installation journal', Code); exit; end;
  Armed := True;
  Log('NetPilot phase=prepared context=administrator result=ok; network control is not linked');
end;
procedure CurStepChanged(CurStep: TSetupStep);
var Code: Cardinal;
begin
  if CurStep = ssPostInstall then begin
    Code := NpEqual(ExpandConstant('{app}\NetPilotLite.exe'), ExpandConstant('{tmp}\candidate.exe'));
    if Code = 0 then Code := NpHealth(ExpandConstant('{app}\NetPilotLite.exe'));
    if Code <> 0 then RaiseException(Fail('Post-install signed image / startup probe', Code));
    if not DeleteFile(Marker) then RaiseException('Could not commit installation journal');
    Completed := True;
    Code := NpCleanup(ExpandConstant('{app}'));
    Log('NetPilot phase=commit result=ok backup-cleanup=' + IntToStr(Code));
  end;
end;
procedure DeinitializeSetup;
var Code: Cardinal;
begin
  if Armed and not Completed then begin
    if HadOld then begin
      Code := NpRestore(ExpandConstant('{app}'));
      Log('NetPilot phase=rollback result=' + IntToStr(Code));
      { Keep marker and backup: metadata must be repaired by a subsequent installer. }
    end;
  end;
end;
function PostMessage(hWnd: HWND; Msg: Cardinal; wParam: LongWord; lParam: LongInt): Boolean;
  external 'PostMessageW@user32.dll stdcall';
function InitializeUninstall: Boolean;
var W: HWND; I: Integer;
begin
  Result := not FileExists(ExpandConstant('{app}\.NetPilotLite-install-pending'));
  if Result then begin
    W := FindWindowByClassName('NetPilotLiteWindow1');
    if W <> 0 then begin
      PostMessage(W, $111, 106, 0);
      for I := 1 to 100 do begin
        if FindWindowByClassName('NetPilotLiteWindow1') = 0 then break;
        Sleep(200);
      end;
      Result := FindWindowByClassName('NetPilotLiteWindow1') = 0;
    end;
  end;
  if not Result then MsgBox('An unfinished installation requires repair first. Backup and user data are retained.', mbError, MB_OK);
end;
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var Code: Integer;
begin
  if CurUninstallStep = usPostUninstall then
    if not UninstallSilent then
      if MsgBox(CustomMessage('RemoveData'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        ShellExec('open', ExpandConstant('{localappdata}\NetPilotLite'), '', '', SW_SHOWNORMAL, ewNoWait, Code);
end;
