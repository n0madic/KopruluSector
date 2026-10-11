#!/usr/bin/env python3
"""Read-only research probe; output only in scratch, not an importer."""
import ctypes as c
import argparse, hashlib, json, pathlib, struct, sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--stormlib',required=True)
p.add_argument('--app',type=pathlib.Path,required=True)
p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--listfile',type=pathlib.Path,required=True)
p.add_argument('--version',default='unverified')
args=p.parse_args()
LIB,APP,OUT = args.stormlib,args.app,args.out
OUT.mkdir(parents=True,exist_ok=True)
s = c.CDLL(LIB)
U = c.c_uint32
H = c.c_void_p
s.SFileOpenArchive.argtypes = [c.c_char_p,U,U,c.POINTER(H)]
s.SFileOpenArchive.restype = c.c_bool
s.SFileOpenFileEx.argtypes = [H,c.c_char_p,U,c.POINTER(H)]
s.SFileOpenFileEx.restype = c.c_bool
s.SFileGetFileSize.argtypes = [H,c.POINTER(U)]
s.SFileGetFileSize.restype = U
s.SFileReadFile.argtypes = [H,c.c_void_p,U,c.POINTER(U),c.c_void_p]
s.SFileReadFile.restype = c.c_bool
s.SFileCloseFile.argtypes = [H]
s.SFileCloseArchive.argtypes = [H]
def sha(b): return hashlib.sha256(b).hexdigest()
def file_sha(path):
    digest=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''): digest.update(block)
    return digest.hexdigest()
def read(h,name):
    f=H()
    if not s.SFileOpenFileEx(h,name.encode('ascii'),0,c.byref(f)): return None
    try:
        high=U(); n=s.SFileGetFileSize(f,c.byref(high))
        if high.value or n==0xffffffff: raise ValueError('Unexpected MPQ file size')
        buf=c.create_string_buffer(n); got=U()
        if not s.SFileReadFile(f,buf,n,c.byref(got),None) or got.value!=n: raise IOError(name)
        return buf.raw[:n]
    finally: s.SFileCloseFile(f)
def tbl(data):
    n=struct.unpack_from('<H',data)[0]
    offsets=struct.unpack_from('<'+'H'*n,data,2)
    result=[]
    for off in offsets:
        end=data.find(b'\0',off)
        result.append(data[off:end if end>=0 else len(data)].decode('latin1'))
    return result
archives=[]
for name in ('Patch_rt.mpq','BrooDat.mpq','StarDat.mpq','BroodWar.mpq','StarCraft.mpq'):
    p=APP/name
    if not p.exists(): continue
    h=H()
    if not s.SFileOpenArchive(str(p).encode(),0,0x100,c.byref(h)): raise IOError(str(p))
    archives.append((name,h))
paths=['scripts\\aiscript.bin','scripts\\bwscript.bin','scripts\\iscript.bin','rez\\stat_txt.tbl','arr\\mapdata.tbl']
paths += ['arr\\'+name+'.dat' for name in ('units','weapons','flingy','sprites','images','upgrades','techdata','sfxdata','portdata','mapdata','orders')]
manifest={'retail_version':args.version,'scope':'static read-only; no original runtime proof','archives':[{'name':name,'bytes':(APP/name).stat().st_size,'sha256':file_sha(APP/name)} for name,_ in archives], 'listfile_sha256':file_sha(args.listfile),'physical_files':[],'effective':{}}
try:
    for archive,h in archives:
        for name in paths:
            data=read(h,name)
            if data is None: continue
            row={'archive':archive,'path':name,'bytes':len(data),'sha256':sha(data)}
            manifest['physical_files'].append(row)
            if name not in manifest['effective']:
                manifest['effective'][name]=row
                target=OUT/pathlib.Path(name.replace('\\','/'))
                target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(data)
    mapdat=(OUT/'arr/mapdata.dat').read_bytes(); maps=tbl((OUT/'arr/mapdata.tbl').read_bytes())
    refs=struct.unpack('<'+'I'*(len(mapdat)//4),mapdat)
    manifest['mapdata_tbl_count']=len(maps)
    manifest['mapdata_dat_count']=len(refs)
    manifest['map_catalog']=[{'index':i,'tbl_reference':ref,'path':maps[ref-1] if ref else None} for i,ref in enumerate(refs)]
    listpaths=args.listfile.read_text().splitlines()
    campaign_paths=set(p for p in listpaths if p.lower().startswith('campaign\\') and p.lower().endswith('scenario.chk'))
    campaign_paths.update(m['path'].rstrip('\\')+'\\staredit\\scenario.chk' for m in manifest['map_catalog'] if m['path'])
    campaign=[]
    for name in sorted(campaign_paths):
        for archive,h in archives:
            data=read(h,name)
            if data is None: continue
            target=OUT/pathlib.Path(name.replace('\\','/')); target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(data)
            row={'path':name,'archive':archive,'bytes':len(data),'sha256':sha(data),'sections':[]}
            p=0; conditions={}; actions={}; ai=[]
            while p<len(data):
                if p+8>len(data): raise ValueError('Truncated CHK header')
                tag,size=struct.unpack_from('<4sI',data,p); p+=8
                if p+size>len(data): raise ValueError('Truncated CHK data')
                part=data[p:p+size]; p+=size
                tag=tag.decode('latin1'); row['sections'].append({'tag':tag,'bytes':size})
                if tag=='VER ': row['chk_version']=struct.unpack_from('<H',part)[0]
                if tag in ('TRIG','MBRF'):
                    if size%2400: raise ValueError('Nonintegral trigger')
                    row[tag+'_count']=row.get(tag+'_count',0)+size//2400
                    if tag!='TRIG': continue
                    for t in range(0,size,2400):
                        for j in range(16):
                            kind=part[t+j*20+15]
                            if kind: conditions[kind]=conditions.get(kind,0)+1
                        for j in range(64):
                            a=t+320+j*32; kind=part[a+26]
                            if kind: actions[kind]=actions.get(kind,0)+1
                            if kind in (15,16): ai.append({'id':part[a+20:a+24].decode('latin1'),'location':struct.unpack_from('<I',part,a)[0],'action':kind,'action_flags':part[a+28],'trigger_index':t//2400,'action_index':j,'execution_flags':struct.unpack_from('<I',part,t+2368)[0],'owners':list(part[t+2372:t+2400])})
            row['condition_types']=conditions; row['action_types']=actions; row['ai_calls']=ai
            campaign.append(row); break
    manifest['campaign_found']=campaign
finally:
    for _,h in archives: s.SFileCloseArchive(h)
(OUT/'manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n')
print(json.dumps({'physical_files':len(manifest['physical_files']),'effective_files':len(manifest['effective']),'mapdata_dat':len(refs),'mapdata_tbl':len(maps),'campaign_found':len(campaign),'output':str(OUT/'manifest.json')},ensure_ascii=False))
