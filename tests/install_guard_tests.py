"""Only signed images copied inside a fresh artifacts directory. No installer/adapter writes."""
import ctypes as c, hashlib, json, pathlib, shutil, sys, uuid, os
project=pathlib.Path(__file__).resolve().parents[1]
root=project/'artifacts'/('guard-'+uuid.uuid4().hex)
root.mkdir()
lib=c.WinDLL(str(project/'bin/Release/InstallGuard.dll'))
for name in ('NpVerify','NpPath','NpClose','NpBackup','NpRestore','NpCleanup','NpHealth','NpJournal'):
    fn=getattr(lib,name); fn.argtypes=[c.c_wchar_p]; fn.restype=c.c_ulong
lib.NpEqual.argtypes=[c.c_wchar_p,c.c_wchar_p];lib.NpEqual.restype=c.c_ulong
lib.NpCompare.argtypes=[c.c_wchar_p,c.c_wchar_p];lib.NpCompare.restype=c.c_ulong
checks=[]
def check(name, condition):
    checks.append({'name':name,'passed':bool(condition)})
    print(('PASS ' if condition else 'FAIL ')+name)
def copy_product(source,folder):
    folder.mkdir(exist_ok=True)
    for name in ['NetPilotLite.exe','NetPilotLite.exe.update']:shutil.copy2(source/name,folder/name)
def digest(file):return hashlib.sha256(file.read_bytes()).hexdigest()
old=pathlib.Path(sys.argv[1]).resolve(); new=project/'dist'
copy_product(old,root/'installed 中文 path'); folder=root/'installed 中文 path';exe=folder/'NetPilotLite.exe'
newexe=new/'NetPilotLite.exe'
check('signed old identity',lib.NpVerify(str(exe))==0)
check('signed new identity',lib.NpVerify(str(newexe))==0)
check('exact deployed payload comparison',lib.NpEqual(str(newexe),str(newexe))==0)
check('different signed payload rejected',lib.NpEqual(str(exe),str(newexe))!=0)
check('upgrade allowed',lib.NpCompare(str(exe),str(newexe))==0)
check('same version requires explicit repair',lib.NpCompare(str(newexe),str(newexe))==183)
check('downgrade refused',lib.NpCompare(str(newexe),str(exe))==1306)
link=root/'hardlink.exe';os.link(exe,link)
check('hardlinked target refused',lib.NpVerify(str(exe))==5)
link.unlink()
check('relative directory refused',lib.NpPath('relative')==5)
check('UNC path refused',lib.NpPath(r'\\server\share')==5)
check('traversal refused',lib.NpPath(str(root)+r'\..\outside')==5)
check('signed old backup',lib.NpBackup(str(folder))==0)
check('existing backup not overwritten',lib.NpBackup(str(folder))==183)
check('durable transaction journal',lib.NpJournal(str(folder))==0)
check('journal collision refused',lib.NpJournal(str(folder))!=0)
# User-state sentinel outside install directory, captures unrelated bytes during replacement/restore.
data=root/'LocalAppData'/'NetPilotLite';data.mkdir(parents=True)
for name in ('settings.ini','background.jpg','rules.json'):(data/name).write_bytes(b'user configuration \x00\xff '+name.encode())
before={x.name:digest(x) for x in data.iterdir()}
original=digest(exe);copy_product(new,folder)
check('in-place new image verified',lib.NpVerify(str(exe))==0 and digest(exe)==digest(newexe))
code=lib.NpRestore(str(folder))
check('restore old signed EXE on failure',code==0 and digest(exe)==original)
check('backup retained for interrupted transaction', (folder/'.NetPilotLite-install-backup'/'NetPilotLite.exe').exists())
check('user settings/background/rules unchanged',before=={x.name:digest(x) for x in data.iterdir()})
# Tamper recovery image: must refuse rather than replace with untrusted bytes.
backup=folder/'.NetPilotLite-install-backup'/'NetPilotLite.exe'
with backup.open('r+b') as f:f.write(b'bad')
check('tampered recovery image refused',lib.NpRestore(str(folder))!=0 and digest(exe)==original)
check('explicit cleanup only fixed backup files',lib.NpCleanup(str(folder))==0)
check('config survives backup cleanup',before=={x.name:digest(x) for x in data.iterdir()})
(root/'results.json').write_text(json.dumps(checks,indent=2))
print(f'{sum(x["passed"] for x in checks)}/{len(checks)} guard checks; no real installation. Evidence: {root}')
sys.exit(0 if all(x['passed'] for x in checks) else 1)
