#!/usr/bin/env python3
"""Read effective retail image GRPs into scratch; publish provenance only."""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import struct

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--app',type=Path,required=True);p.add_argument('--stormlib',type=Path,required=True)
    p.add_argument('--scratch',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--image-ids',required=True);p.add_argument('--retail-manifest',type=Path,required=True)
    a=p.parse_args();retail=json.loads(a.retail_manifest.read_text());archive_hashes={x['name']:x['sha256'] for x in retail['archives']};ids=sorted({int(x) for x in a.image_ids.split(',')})
    if not ids or any(not 0<=x<999 for x in ids):p.error('Image IDs must be between 0 and 998')
    lib=c.CDLL(str(a.stormlib));H,U=c.c_void_p,c.c_uint32
    lib.SFileOpenArchive.argtypes=[c.c_char_p,U,U,c.POINTER(H)];lib.SFileOpenArchive.restype=c.c_bool
    lib.SFileOpenFileEx.argtypes=[H,c.c_char_p,U,c.POINTER(H)];lib.SFileOpenFileEx.restype=c.c_bool
    lib.SFileGetFileSize.argtypes=[H,c.POINTER(U)];lib.SFileGetFileSize.restype=U
    lib.SFileReadFile.argtypes=[H,c.c_void_p,U,c.POINTER(U),c.c_void_p];lib.SFileReadFile.restype=c.c_bool
    lib.SFileCloseFile.argtypes=[H];lib.SFileCloseArchive.argtypes=[H]
    archives=[];provenance={}
    def read(name):
        for archive,h in archives:
            f=H()
            if not lib.SFileOpenFileEx(h,name.encode('ascii'),0,c.byref(f)):continue
            try:
                high=U();n=lib.SFileGetFileSize(f,c.byref(high))
                if high.value or n==0xFFFFFFFF:raise ValueError('Unexpected archive file size')
                buf=c.create_string_buffer(n);got=U()
                if not lib.SFileReadFile(f,buf,n,c.byref(got),None) or got.value!=n:raise IOError(name)
                raw=buf.raw[:n];provenance[name]=dict(archive=archive,bytes=n,sha256=hashlib.sha256(raw).hexdigest())
                return raw
            finally:lib.SFileCloseFile(f)
        raise FileNotFoundError(name)
    try:
        for name in ('Patch_rt.mpq','BrooDat.mpq','StarDat.mpq','BroodWar.mpq','StarCraft.mpq'):
            path=a.app/name
            if not path.exists():continue
            assert hashlib.sha256(path.read_bytes()).hexdigest()==archive_hashes[name],name
            h=H()
            if not lib.SFileOpenArchive(str(path).encode(),0,0x100,c.byref(h)):raise IOError(path)
            archives.append((name,h))
        images=read('arr\\images.dat');table=read('arr\\images.tbl');entries=[];lo_entries=[]
        count=struct.unpack_from('<H',table)[0]
        for image_id in ids:
            ref=struct.unpack_from('<I',images,image_id*4)[0]
            if not 1<=ref<=count:raise ValueError('Invalid image filename reference')
            start=struct.unpack_from('<H',table,2+(ref-1)*2)[0]
            filename=table[start:table.index(0,start)].decode('ascii');path='unit\\'+filename
            raw=read(path);target=a.scratch/Path(path.replace('\\','/'))
            target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw)
            frames,width,height=struct.unpack_from('<3H',raw)
            entries.append(dict(image_id=image_id,filename_reference=ref,path=path,
                frames=frames,width=width,height=height,**provenance[path]))
        for image_id in ids:
            for kind,field in enumerate(('attack_overlay','damage_overlay','special_overlay','landing_dust_overlay','lift_off_dust_overlay','shield_overlay')):
                column={'shield_overlay':8,'attack_overlay':9,'damage_overlay':10,'special_overlay':11,'landing_dust_overlay':12,'lift_off_dust_overlay':13}[field]
                ref=struct.unpack_from('<I',images,999*10+(column-7)*999*4+image_id*4)[0]
                if not ref:continue
                start=struct.unpack_from('<H',table,2+(ref-1)*2)[0]
                filename=table[start:table.index(0,start)].decode('ascii');path='unit\\'+filename
                raw=read(path);target=a.scratch/Path(path.replace('\\','/'));target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw)
                lo_entries.append(dict(image_id=image_id,kind=kind,field=field,filename_reference=ref,path=path,**provenance[path]))
        target=a.scratch/'arr/images.tbl';target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(table)
        result=dict(scope='Read-only MPQ inspection, no original runtime',extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            stormlib_sha256=hashlib.sha256(a.stormlib.read_bytes()).hexdigest(),retail_manifest_sha256=hashlib.sha256(a.retail_manifest.read_bytes()).hexdigest(),archive_sha256=archive_hashes,provenance=provenance,images=entries,lo_files=lo_entries)
        a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(dict(images=len(entries),distinct_paths=len({x['path'] for x in entries}))))
    finally:
        for _,h in archives:lib.SFileCloseArchive(h)

if __name__=='__main__':main()
