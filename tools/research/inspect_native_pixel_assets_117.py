#!/usr/bin/env python3
"""Extract selected pixel lookup assets into private scratch, publishing hashes only."""
import argparse,ctypes as c,hashlib,json
from pathlib import Path

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for k in ('app','stormlib','scratch','retail-manifest','out'):p.add_argument('--'+k,type=Path,required=True)
 p.add_argument('--paths',help='Comma-separated MPQ paths; default: player/selection colors and badlands palette lookup assets')
 a=p.parse_args();retail=json.loads(a.retail_manifest.read_text());hashes={r['name']:r['sha256'] for r in retail['archives']}
 names=a.paths.split(',') if a.paths else ['game\\tunit.pcx','game\\tminimap.pcx','game\\tselect.pcx','tileset\\badlands.wpe']+['tileset\\badlands\\'+v+'.pcx' for v in ('dark','ofire','gfire','bfire','bexpl','trans50','red','green','shift')]
 assert all(name and not Path(name.replace('\\','/')).is_absolute() and '..' not in Path(name.replace('\\','/')).parts for name in names)
 lib=c.CDLL(str(a.stormlib));H,U=c.c_void_p,c.c_uint32
 lib.SFileOpenArchive.argtypes=[c.c_char_p,U,U,c.POINTER(H)];lib.SFileOpenArchive.restype=c.c_bool
 lib.SFileOpenFileEx.argtypes=[H,c.c_char_p,U,c.POINTER(H)];lib.SFileOpenFileEx.restype=c.c_bool
 lib.SFileGetFileSize.argtypes=[H,c.POINTER(U)];lib.SFileGetFileSize.restype=U
 lib.SFileReadFile.argtypes=[H,c.c_void_p,U,c.POINTER(U),c.c_void_p];lib.SFileReadFile.restype=c.c_bool
 lib.SFileCloseFile.argtypes=[H];lib.SFileCloseArchive.argtypes=[H]
 archives=[];rows={}
 try:
  for name in ('Patch_rt.mpq','BrooDat.mpq','StarDat.mpq','BroodWar.mpq','StarCraft.mpq'):
   path=a.app/name
   if not path.exists():continue
   assert hashlib.sha256(path.read_bytes()).hexdigest()==hashes[name],name
   h=H();assert lib.SFileOpenArchive(str(path).encode(),0,0x100,c.byref(h)),name;archives.append((name,h))
  for name in names:
   for archive,h in archives:
    f=H()
    if not lib.SFileOpenFileEx(h,name.encode('ascii'),0,c.byref(f)):continue
    try:
     hi=U();n=lib.SFileGetFileSize(f,c.byref(hi));assert not hi.value and n!=0xFFFFFFFF
     b=c.create_string_buffer(n);got=U();assert lib.SFileReadFile(f,b,n,c.byref(got),None) and got.value==n
     raw=b.raw[:n];target=a.scratch/name.replace('\\','/');target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw)
     rows[name]=dict(archive=archive,bytes=n,sha256=hashlib.sha256(raw).hexdigest());break
    finally:lib.SFileCloseFile(f)
   else:raise FileNotFoundError(name)
 finally:
  for _,h in archives:lib.SFileCloseArchive(h)
 result=dict(scope='Read-only extraction to private scratch; no retail code execution or asset redistribution',extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),stormlib_sha256=hashlib.sha256(a.stormlib.read_bytes()).hexdigest(),retail_manifest_sha256=hashlib.sha256(a.retail_manifest.read_bytes()).hexdigest(),archives_sha256=hashes,assets=rows)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(assets=len(rows),sha256=result['extractor_sha256'])))
if __name__=='__main__':main()
