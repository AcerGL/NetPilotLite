"""Emit independent signed Release assets. This path never creates a ZIP or offline installer."""
import argparse,pathlib,subprocess,json,hashlib,re,shutil
p=argparse.ArgumentParser();p.add_argument('--key',required=True);p.add_argument('--tag',required=True);p.add_argument('--output',default='dist/public');p.add_argument('--notes',default='Single native Setup, signed manifest, file-by-file installation and in-place recovery.');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];out=pathlib.Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
v=re.search(r'#define NP_VERSION L"([0-9.]+)"',(root/'src/update.h').read_text(encoding='utf-8')).group(1)
if a.tag!='v'+v or not re.fullmatch(r'v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)',a.tag):raise SystemExit('Tag/source mismatch')
tool=root/'bin/Release/ReleaseTool.exe';key=a.key if a.key=='ENV' else str(pathlib.Path(a.key).resolve())
def sign(purpose,path):subprocess.run([str(tool),purpose,key,str(path)],cwd=root,check=True)
sign('sign',root/'bin/Release/NetPilotLite.exe');sign('sign-setup',root/'bin/Release/NetPilotLite-Setup-x64.exe')
runtime=[('NetPilotLite.exe','NetPilotLite.exe'),('NetPilotLite.exe.update','NetPilotLite.exe.update')]
files=[]
for asset,installed in runtime:
 target=out/asset;shutil.copy2(root/'bin/Release'/asset,target)
 files.append(dict(asset_name=asset,install_path=installed,size=target.stat().st_size,sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
for name in ['NetPilotLite-Setup-x64.exe','NetPilotLite-Setup-x64.exe.update']:shutil.copy2(root/'bin/Release'/name,out/name)
manifest=dict(version=v,architecture='x64',files=files,release_notes=a.notes)
(out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');sign('sign-catalog',out/'manifest.json')
names=[x[0] for x in runtime]+['NetPilotLite-Setup-x64.exe','NetPilotLite-Setup-x64.exe.update','manifest.json','manifest.json.update']
(out/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256((out/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in names),encoding='ascii')
print('Signed independent assets:',out)
