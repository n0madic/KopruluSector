#!/usr/bin/env python3
"""Isolated original UNIT property application with inert unit/sprite fixtures.

No original allocation or game execution. Active special states, positive hangar
creation and mineral sprite changes are excluded by explicit input/fixture
selection; no native callees are replaced by stubs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import subprocess
import sys
import capstone
import pefile

HASH="94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
PYMS="bfc5d3aad0b5614a5aff72c223f8efa00afddfa4"
UNIT,PROP,SPRITE,STARTS,STACK,STOP=0x800000,0x802000,0x804000,0x806000,0x900000,0xA00000
ALLOW=[(0x46A420,0x46A601),(0x41BB50,0x41BBBF),(0x434940,0x4349D6),
       (0x434A80,0x434A89),(0x49A470,0x49A496),(0x46A180,0x46A207),
       (0x41CC20,0x41CC47),(0x41CBA0,0x41CC14),(0x424380,0x4243A1),(0x4247B0,0x424805),
       (0x46A210,0x46A221),(0x46A275,0x46A27A),
       (0x46A280,0x46A31E),(0x469600,0x46964F)]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ("exe","data","manifest","pyms"):
        p.add_argument("--"+name,type=Path,required=True)
    p.add_argument("--out",type=Path)
    p.add_argument("--execute-original-code",action="store_true")
    a=p.parse_args();raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==HASH
    if not a.execute_original_code:
        print(json.dumps({"target_sha256":HASH,"functions":["0x46a420 property application","0x46a280 start locations","0x469600 mode filter"],
                          "stubs":[],"limits":"Inert unit/sprite; positive hangar/active states excluded; mineral animation preselected"},indent=2));return
    assert a.out
    assert subprocess.check_output(["git","-C",str(a.pyms),"rev-parse","HEAD"],text=True).strip()==PYMS
    sys.path.insert(0,str(a.pyms));from PyMS.FileFormats.DAT.UnitsDAT import UnitsDAT
    dat=(a.data/"arr/units.dat").read_bytes();units=UnitsDAT();units.load_data(dat)
    mraw=a.manifest.read_bytes();manifest=json.loads(mraw)
    assert hashlib.sha256(dat).hexdigest()==manifest["effective"]["arr\\units.dat"]["sha256"]
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP
    pe=pefile.PE(data=raw);image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    def w32(u,addr,v):u.mem_write(addr,struct.pack("<I",v&0xFFFFFFFF))
    def r32(u,addr):return struct.unpack("<I",u.mem_read(addr,4))[0]
    def machine():
        u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,size);u.mem_write(0x400000,image)
        u.mem_map(UNIT,0x10000);u.mem_map(STACK,0x4000);u.mem_map(STOP,4096)
        # Explicit synthetic empty upgrade levels avoid ambient PE data as game state.
        u.mem_write(0x534590,b"\0"*552);u.mem_write(0x53660C,b"\0"*180)
        return u
    def call(u,entry,ecx,edx=0):
        done=False;count=0
        def step(mu,pc,n,_):
            nonlocal done,count
            if pc==STOP:done=True;mu.emu_stop();return
            if not any(lo<=pc and pc+n<=hi for lo,hi in ALLOW):raise RuntimeError(f"Unapproved instruction {pc:#x}")
            count+=1
        hook=u.hook_add(UC_HOOK_CODE,step);w32(u,STACK+0x2000,STOP)
        u.reg_write(UC_X86_REG_ESP,STACK+0x2000);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
        try:u.emu_start(entry,STOP+1,count=10000)
        finally:u.hook_del(hook)
        assert done;return u.reg_read(UC_X86_REG_EAX),count
    def mineral_animation(amount):return 20 if amount>=750 else 15 if amount>=500 else 14 if amount>=250 else 13
    def property_case(key):
        typ,owner,flags,hpct,spct,epct,state,maxhp,maxsh,hero,resource,hangar,container=key
        u=machine();u.mem_write(UNIT+0x64,struct.pack("<H",typ));u.mem_write(UNIT+0x4C,bytes([owner]))
        w32(u,UNIT+0xC,SPRITE);w32(u,UNIT+8,maxhp);w32(u,UNIT+0x60,maxsh<<8)
        u.mem_write(UNIT+0xA2,struct.pack("<H",100<<8));w32(u,UNIT+0xDC,0)
        w32(u,0x520708+typ*4,maxhp);u.mem_write(0x520540+typ*2,struct.pack("<H",maxsh))
        w32(u,0x521660+typ*4,(64 if hero else 0)|(0x2000 if container else 0))
        u.mem_write(UNIT+0xD0,struct.pack("<H",0xA55A))
        # This fixture makes the real mineral threshold helper take its unchanged
        # animation path; dependent sprite animation is not silently substituted.
        animation=mineral_animation(resource&65535)
        u.mem_write(UNIT+0xD2,bytes([animation]))
        props=bytearray(20);struct.pack_into("<I",props,0,flags);props[4]=owner
        props[5:8]=bytes([hpct,spct,epct]);struct.pack_into("<I",props,8,resource)
        struct.pack_into("<H",props,12,hangar);struct.pack_into("<H",props,14,state)
        u.mem_write(PROP,bytes(props));_,count=call(u,0x46A420,UNIT,PROP)
        maxenergy=250 if hero else 200
        health=maxhp
        if flags&0x20000:
            quotient=((maxhp*hpct)&0xFFFFFFFF)//100
            health=min(maxhp,max(1,quotient))
        shields=(min(maxsh,(maxsh*spct)//100)<<8) if flags&0x40000 else maxsh<<8
        energy=(min(maxenergy,((maxenergy*epct)//100)&255)<<8) if flags&0x80000 else 100<<8
        status=0x4000000 if flags&state&16 else 0
        resources=(resource&65535) if flags&0x100000 and container else 0xA55A
        observed={"health_fixed":r32(u,UNIT+8),"shields_fixed":r32(u,UNIT+0x60),
                  "energy_fixed":struct.unpack("<H",u.mem_read(UNIT+0xA2,2))[0],"status":r32(u,UNIT+0xDC),
                  "resources":struct.unpack("<H",u.mem_read(UNIT+0xD0,2))[0],"mineral_animation":u.mem_read(UNIT+0xD2,1)[0]}
        assert observed==dict(health_fixed=health,shields_fixed=shields,energy_fixed=energy,status=status,
                              resources=resources,mineral_animation=animation),(key,observed,health,shields,energy)
        return {"input":{"type":typ,"owner":owner,"valid_flags":flags,"health_percent":hpct,"shield_percent":spct,
                         "energy_percent":epct,"state_flags":state,"maximum_hp_fixed":maxhp,"maximum_shields":maxsh,"hero_flag":hero,
                         "resource_value_u32":resource,"hangar_value_u16":hangar,"resource_container_flag":container},
                "observed":observed,"instructions":count,"execution_scope":"complete_function_on_inert_unit"}
    synthetic=[]
    for hero in (False,True):
        for flags in (0,0x20000,0x40000,0x80000,0xE0000,0xE0010):
            for percent in (0,1,50,100,101,128,255):
                synthetic.append(property_case((0,0,flags,percent,percent,percent,16,40*256,100,hero,0,0,False)))
    for typ in (0,176,188):
        for container in (False,True):
            for resource in (0,1,249,250,499,500,749,750,65535,65536,0xFFFFFFFF):
                synthetic.append(property_case((typ,0,0x100000,100,100,100,0,40*256,100,False,resource,0,container)))
    for typ in (0,72,81,82,83):
        synthetic.append(property_case((typ,0,0x200000,100,100,100,0,40*256,100,False,0,0,False)))
    # Filter is independently exercised with its original mode byte.
    allowed_ids={176,177,178,188,89,90,95,93,94,96}
    for mode in (0,1):
        for owner in (0,11):
            for typ in (0,89,90,93,94,95,96,175,176,177,178,179,188,214):
                u=machine();u.mem_write(PROP+8,struct.pack("<H",typ));u.mem_write(PROP+16,bytes([owner]));u.mem_write(0x672949,bytes([mode]))
                ret,count=call(u,0x469600,PROP)
                assert ret==int(mode and (owner!=11 or typ not in allowed_ids))
                synthetic.append({"kind":"UNIT_mode_filter","mode_byte":mode,"owner":owner,"type":typ,"skip":ret,"instructions":count})
    unique={};maps=[];start_cases=[];positive_hangars=[];allrecords=0
    for row in manifest["campaign_found"]:
        d=(a.data/row["path"].replace("\\","/")).read_bytes();assert hashlib.sha256(d).hexdigest()==row["sha256"]
        parts=[];pos=0
        while pos<len(d):
            tag,n=struct.unpack_from("<4sI",d,pos);pos+=8;part=d[pos:pos+n];pos+=n;assert len(part)==n;parts.append((tag,part))
        stats=[((x.hit_points.whole<<8)|x.hit_points.fraction,x.shield_amount,bool(x.special_ability_flags&64),bool(x.special_ability_flags&0x2000)) for x in units.entries]
        # Apply only unit HP/shield override fields whose native body was separately
        # executed in native-map-overrides probes; this setup is a host fixture.
        for tag,part in parts:
            if tag in (b"UNIS",b"UNIx"):
                for typ in range(228):
                    if part[typ]==0:
                        stats[typ]=(struct.unpack_from("<I",part,228+typ*4)[0],struct.unpack_from("<H",part,1140+typ*2)[0],*stats[typ][2:])
        count=eligible=starts=0;exclusions={};map_keys=set()
        for tag,part in parts:
            if tag!=b"UNIT":continue
            assert len(part)%36==0
            for offset in range(0,len(part),36):
                record=part[offset:offset+36];count+=1;typ=struct.unpack_from("<H",record,8)[0];owner=record[16]
                flags=struct.unpack_from("<I",record,12)[0];state=struct.unpack_from("<H",record,26)[0]
                if typ==214:
                    starts+=1;u=machine();u.mem_write(PROP,record);w32(u,0x508D50,0);w32(u,0x67CC90,0)
                    u.mem_write(0x5264B0,b"\xff"*4);ret,ins=call(u,0x46A280,PROP,STARTS)
                    x,y=struct.unpack_from("<HH",record,4);assert ret==1
                    assert bytes(u.mem_read(STARTS+owner*4,4))==struct.pack("<HH",x,y)
                    cam=struct.unpack("<HH",u.mem_read(0x5264B0,4))
                    assert cam==((max(0,(x>>5)-10),max(0,(y>>5)-6)) if owner==0 else (65535,65535))
                    start_cases.append({"map_path":row["path"],"record_sha256":hashlib.sha256(record).hexdigest(),
                                        "owner":owner,"x":x,"y":y,"camera_tile":list(cam),"instructions":ins});continue
                resource=struct.unpack_from("<I",record,20)[0];hangar=struct.unpack_from("<H",record,24)[0]
                reason="broodling_timer" if typ==40 else "positive_hangar_children" if flags&0x200000 and (hangar&255) else "active_special_state" if flags&state&15 else None
                if reason=="positive_hangar_children":positive_hangars.append({"map_path":row["path"],"record_index":offset//36,
                    "record_sha256":hashlib.sha256(record).hexdigest(),"type":typ,"owner":owner,"hangar_count":hangar})
                if reason:exclusions[reason]=exclusions.get(reason,0)+1;continue
                assert typ<228 and owner<12
                hp,sh,hero,container=stats[typ]
                key=(typ,owner,flags,record[17],record[18],record[19],state,hp,sh,hero,resource,hangar,container);eligible+=1
                if key not in unique:unique[key]=property_case(key)
                map_keys.add(key)
        allrecords+=count;maps.append({"map_path":row["path"],"map_sha256":row["sha256"],"chk_version":row["chk_version"],
                                     "UNIT_records":count,"eligible_property_records":eligible,"distinct_property_inputs":len(map_keys),
                                     "start_locations":starts,"excluded_property_records":exclusions})
    def witness(lo,hi):
        off=pe.get_offset_from_rva(lo-0x400000)
        return [{"va":hex(i.address),"rva":hex(i.address-0x400000),"file_offset":hex(pe.get_offset_from_rva(i.address-0x400000)),
                 "bytes":i.bytes.hex(),"mnemonic":i.mnemonic,"operands":i.op_str} for i in md.disasm(raw[off:off+hi-lo],lo)]
    total=len(synthetic)+len(unique)+len(start_cases)
    result={"target_sha256":HASH,"probe_script_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),"runtime_architecture":platform.machine(),
            "python_version":platform.python_version(),"unicorn_version":unicorn.__version__,"stubs":[],"original_code_executed":True,"complete_game_executed":False,
            "synthetic_checks":len(synthetic),"campaign_distinct_property_checks":len(unique),"campaign_start_location_checks":len(start_cases),
            "total_native_checks":total,"all_expected_results_matched":True,"UNIT_records_audited":allrecords,
            "eligible_property_records":sum(r["eligible_property_records"] for r in maps),"campaign_input_coverage":maps,
            "synthetic_cases":synthetic,"campaign_distinct_property_cases":list(unique.values()),"campaign_start_cases":start_cases,
            "positive_hangar_creation_dependencies":positive_hangars,
            "manifest_sha256":hashlib.sha256(mraw).hexdigest(),"units_dat_sha256":hashlib.sha256(dat).hexdigest(),"pyms_commit":PYMS,
            "witnesses":{"UNIT_loader":witness(0x468F30,0x469125),"property_application":witness(0x46A420,0x46A601),
                         "start_locations":witness(0x46A280,0x46A31E),"UNIT_mode_filter":witness(0x469600,0x46964F),
                         "maximum_energy":witness(0x434940,0x4349D6),"hp_setter":witness(0x41BB50,0x41BBBF),
                         "mineral_predicate":witness(0x424380,0x4243A1),"mineral_thresholds":witness(0x4247B0,0x424805),
                         "hangar_capacity":witness(0x41CBA0,0x41CC47)},
            "limits":["Complete selected functions on inert unit/sprite fixtures; no allocation/creation or game execution.",
                      "Campaign property inputs deduplicate equal decoded arguments; eligible records are not separate native calls.",
                      "Default HP/shield/hero setup uses decoded DAT and separately proven map override semantics, not native DAT/CHK load.",
                      "Empty upgrade levels and nonbuilding/nonvisible fixture state intentionally exclude dependent UI/order branches.",
                      "Resource fixtures preselect the matching mineral animation so its real helper avoids sprite mutation; zero hangar paths only.",
                      "Positive hangar generation, active cloak/burrow/transit/hallucination and Broodling timer branches remain unexecuted."]}
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({k:result[k] for k in ("synthetic_checks","campaign_distinct_property_checks","campaign_start_location_checks","total_native_checks","UNIT_records_audited","eligible_property_records","all_expected_results_matched")}))

if __name__=="__main__":main()
