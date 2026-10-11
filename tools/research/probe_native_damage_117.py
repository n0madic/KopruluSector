#!/usr/bin/env python3
"""Isolated exact-hash damage probes; default mode prints a plan only.

Execution uses original x86 instructions and real callees in private emulator
memory. Artificial DAT/unit/image fixtures are recorded. There are no helper
stubs, PE process, Windows APIs, network or guest host-file calls. Image creation
cases exercise the original exhausted-pool branch. Death and visibility prefixes
stop before a named original callee rather than replacing it.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import pefile
from inspect_native_damage_117 import TARGET_HASH, BLOCKS

UNIT, OTHER, SPRITE, IMAGE, LO, AI = 0x800000, 0x800200, 0x806000, 0x806100, 0x809000, 0x804000
STACK, STOP = 0x900000, 0xA00000
ALLOW = list(BLOCKS.values()) + [
    (0x409C90,0x409CB9), (0x41FAA0,0x41FAD1), (0x4200E0,0x420120),
    (0x420000,0x420027), (0x4255F0,0x4255F6), (0x428B50,0x428B9C),
    (0x41FE20,0x41FE3B), (0x438350,0x438385),
    (0x4A8F30,0x4A8F56), (0x4A9630,0x4A96A3), (0x4A96B0,0x4A975A)]
FACTORS = [[0,0,0,0], [0,128,192,256], [0,256,128,64], [0,256,256,256], [0,256,256,256]]
PLAN = {"target_sha256": TARGET_HASH, "instruction_bound": 20000,
    "stubs": "None", "scope": "Damage arithmetic, real nonlethal pipeline, hallucinated hits, attribution and lethal prefixes",
    "visual_fixture": "Valid artificial main image/LO offsets; image pool exhausted; no overlay allocations",
    "limits": "No original game process, campaign run or visible UI"}

def u32(value): return value & 0xFFFFFFFF
def s32(value): return value if value < 0x80000000 else value-0x100000000

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe", type=Path, required=True)
    p.add_argument("--out", type=Path)
    p.add_argument("--execute-original-code", action="store_true")
    args = p.parse_args()
    raw = args.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != TARGET_HASH: p.error("Unknown executable hash")
    if not args.execute_original_code:
        print(json.dumps(PLAN, indent=2)); return
    if args.out is None: p.error("Execution requires --out")
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP
    pe = pefile.PE(data=raw)
    assert pe.OPTIONAL_HEADER.ImageBase == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    image = pe.get_memory_mapped_image()
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095
    cases = []
    def put(uc, addr, value, n=4): uc.mem_write(addr, (value&((1<<(8*n))-1)).to_bytes(n,"little"))
    def get(uc, addr, n=4): return int.from_bytes(uc.mem_read(addr,n),"little")
    def new(f=None):
        f = f or {}
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(0x400000,image_size); uc.mem_write(0x400000,image)
        for addr,size in ((UNIT,0x10000),(STACK,0x1000),(STOP,0x1000)): uc.mem_map(addr,size)
        uid = f.get("unit_id",15); owner=f.get("owner",3); hp=f.get("hp",25600)
        for address in (UNIT,OTHER):
            put(uc,address+8,hp); put(uc,address+0xC,SPRITE)
            put(uc,address+0x64,uid,2); put(uc,address+0x4C,owner,1)
            put(uc,address+0x4D,3,1); put(uc,address+0xDC,f.get("status",1))
        put(uc,UNIT+0x60,f.get("shields",0)); put(uc,UNIT+0x112,f.get("matrix",0),2)
        put(uc,UNIT+0x114,f.get("matrix_timer",0),1); put(uc,UNIT+0x126,f.get("spores",0),1)
        put(uc,UNIT+0x90,0xFF,1)
        put(uc,SPRITE+0x18,IMAGE); put(uc,SPRITE+0x1C,IMAGE); put(uc,SPRITE+0x20,IMAGE)
        put(uc,SPRITE+0xE,8 if f.get("selected") else 0,1)
        put(uc,IMAGE+0xA,11,1); put(uc,IMAGE+0xC,4,2)
        put(uc,0x59B210,LO); put(uc,LO+8,0x100)
        put(uc,0x544694,0); put(uc,0x516D38,0)
        put(uc,0x520458+uid,f.get("shield_enabled",int("shields" in f)),1)
        put(uc,0x520540+uid*2,f.get("max_shields",100),2)
        put(uc,0x520708+uid*4,f.get("max_hp",25600))
        put(uc,0x521660+uid*4,f.get("properties",0))
        put(uc,0x521BC0+uid,0,1); put(uc,0x521D90+uid,f.get("armor",0),1)
        put(uc,0x521CA8+uid,f.get("size",3),1)
        put(uc,0x534590+owner*46,f.get("armor_upgrade",0),1)
        put(uc,0x534590+owner*46+15,f.get("shield_upgrade",0),1)
        put(uc,0x5365DE+owner*15+52,f.get("chitin",0),1)
        put(uc,0x536720,f.get("expansion",1),1)
        weapon=f.get("weapon",0); put(uc,0x525748+weapon,f.get("type",3),1)
        put(uc,0x5432FC,4 if f.get("cheat") else 0)
        put(uc,0x6726D0+f.get("attacking_player",owner)*36,f.get("player_type",2),1)
        put(uc,0x6AC530+uid*4,200); put(uc,0x6AC8C0+uid*4,300)
        return uc
    def call(uc,start,ecx=UNIT,edx=0,argv=(),stop_before=None):
        visited=[]; boundary=None; calls=[]
        def hook(machine,addr,width,_):
            nonlocal boundary
            if addr in (STOP,stop_before): boundary=addr; machine.emu_stop(); return
            if not any(lo<=addr and addr+width<=hi for lo,hi in ALLOW):
                raise RuntimeError(f"Outside reviewed native code: {addr:#x}")
            visited.append(addr)
            if addr in (0x41E150,0x4203D0,0x4384C0,0x422FE0,0x415370,0x415260,0x414080):
                sp=machine.reg_read(UC_X86_REG_ESP)
                calls.append({"callee":hex(addr),"ecx":hex(machine.reg_read(UC_X86_REG_ECX)),
                    "stack_arguments":[get(machine,sp+4+i*4) for i in range(4)]})
        h=uc.hook_add(UC_HOOK_CODE,hook)
        sp=STACK+0x800
        uc.mem_write(sp,struct.pack("<"+"I"*(1+len(argv)),STOP,*(u32(x) for x in argv)))
        uc.reg_write(UC_X86_REG_ESP,sp); uc.reg_write(UC_X86_REG_ECX,ecx); uc.reg_write(UC_X86_REG_EDX,edx)
        uc.emu_start(start,STOP+1,count=20000); uc.hook_del(h)
        if boundary is None: raise RuntimeError("Instruction bound reached before reviewed boundary")
        return {"return_eax":uc.reg_read(UC_X86_REG_EAX),"boundary_ecx":hex(uc.reg_read(UC_X86_REG_ECX)),"complete_return":boundary==STOP,
            "stop_va":hex(boundary),"instruction_count":len(visited),"calls":calls,
            "visited_functions":[hex(lo) for lo,hi in ALLOW if any(lo<=a<hi for a in visited)]}
    def record(label,inputs,actual,expected,execution):
        assert actual==expected,(label,inputs,actual,expected)
        cases.append({"case":label,"inputs":inputs,"actual":actual,"expected":expected,"passed":True,"execution":execution})
    def state(uc):
        return {"hp":get(uc,UNIT+8),"shields":get(uc,UNIT+0x60),"matrix":get(uc,UNIT+0x112,2),
            "matrix_timer":get(uc,UNIT+0x114,1),"air_strength":get(uc,UNIT+0x138,2),
            "ground_strength":get(uc,UNIT+0x13A,2),"image_flags":get(uc,IMAGE+0xC,2)}
    def expected(f):
        hp=f.get("hp",25600); shields=f.get("shields",0); matrix=f.get("matrix",0)
        timer=f.get("matrix_timer",0); status=f.get("status",1); uid=f.get("unit_id",15)
        selected=f.get("selected",False); enabled=f.get("shield_enabled",int("shields" in f))
        if hp==0 or status&0x4000000:
            return dict(hp=hp,shields=shields,matrix=matrix,matrix_timer=timer,air_strength=0,ground_strength=0,image_flags=4)
        cheat=f.get("cheat") and f.get("player_type",2)!=2
        damage=0 if cheat else u32(f.get("damage",2560))
        if status&0x40000000: damage=u32(damage*2)
        damage=u32(damage//f.get("divisor",1)+(f.get("spores",0)<<8))
        if s32(damage)<128: damage=128
        absorbed=min(damage,matrix); damage-=absorbed; matrix-=absorbed
        if absorbed and matrix==0: timer=0
        shield_damage=0; dtype=f.get("type",3)
        if enabled and s32(shields)>=256:
            if dtype!=4:
                shield_armor=f.get("shield_upgrade",0)*256
                damage=128 if shield_armor>=damage else damage-shield_armor
            shield_damage=min(damage,shields); damage-=shield_damage
        if dtype!=4:
            bonus=f.get("armor_upgrade",0)
            if f.get("expansion",1) and uid in (39,48) and (f.get("properties",0)&0x40 or f.get("chitin",0)): bonus+=2
            armor=(f.get("armor",0)+(bonus&255))*256
            damage-=min(damage,armor)
        damage=u32(damage*FACTORS[dtype][f.get("size",3)])>>8
        if shield_damage==0 and s32(damage)<128: damage=128
        hp-=0 if cheat else damage; shields-=shield_damage
        life=(hp+255)//256+(shields//256 if enabled else 0)
        maximum=(f.get("max_hp",25600)>>8)+(f.get("max_shields",100) if enabled else 0)
        scores=[0,0] if status&0x40000000 and life<maximum else [(life*x//maximum)&65535 for x in (200,300)]
        return dict(hp=hp,shields=shields,matrix=matrix,matrix_timer=timer,air_strength=scores[0],ground_strength=scores[1],image_flags=5 if selected else 4)
    fixtures=[{"label":f"type_{dtype}_size_{size}","type":dtype,"size":size,"damage":2561}
        for dtype in range(5) for size in range(4)]
    fixtures += [{"label":f"armor_{armor}_damage_{damage}","armor":armor,"damage":damage}
        for armor in (0,1,10) for damage in (0,1,127,128,129,255,256,257,511,512,2560)]
    fixtures += [{"label":f"shields_{shields}_type_{dtype}_upgrade_{level}","shields":shields,"type":dtype,"shield_upgrade":level,"damage":512}
        for shields in (0,1,127,255,256,257,511,512,513,25600) for dtype in (0,1,3,4) for level in (0,1,3)]
    fixtures += [{"label":f"matrix_{matrix}_shield_{shields}_type_{dtype}","matrix":matrix,"matrix_timer":10,"shields":shields,"type":dtype,"damage":512,"status":17}
        for matrix in (1,127,128,255,256,511,512,513,25600) for shields in (0,255,256,512,25600) for dtype in (1,3,4)]
    fixtures += [{"label":f"hallucinated_target_divisor_{divisor}_spores_{spores}","damage":257,"status":0x40000001,"divisor":divisor,"spores":spores}
        for divisor in (1,2,3,9) for spores in (0,1,9)]
    fixtures += [{"label":f"ultralisk_pipeline_{uid}_{chitin}_{level}","unit_id":uid,"chitin":chitin,"armor_upgrade":level,"damage":2048}
        for uid in (39,48) for chitin in (0,1,2) for level in (0,1,255)]
    fixtures += [{"label":f"cheat_type_{ptype}_spores_{spores}_shield_{shield}","cheat":True,"player_type":ptype,"spores":spores,"shields":shield}
        for ptype in (0,1,2,3) for spores in (0,2) for shield in (0,25600)]
    fixtures += [{"label":"dead_gate","hp":0,"matrix":512,"shields":512},
        {"label":"invulnerable_gate","status":0x4000001,"matrix":512,"shields":512},
        {"label":"selected_real_image_mark","selected":True},
        {"label":"matrix_real_impact_allocation_failure","matrix":25600,"matrix_timer":10},
        {"label":"disabled_shields","shields":25600,"shield_enabled":0},
        {"label":"ignored_armor_also_ignores_shield_upgrade","type":4,"armor":250,"armor_upgrade":255,"shield_upgrade":255,"shields":513},
        {"label":"armor_before_explosive_size","type":1,"size":1,"armor":1,"damage":768},
        {"label":"acid_after_divisor","damage":9,"divisor":9,"spores":1}]
    for f in fixtures:
        uc=new(f); execution=call(uc,0x41DFC0,argv=(f.get("damage",2560),f.get("weapon",0),f.get("divisor",1),f.get("direction",0),0,f.get("attacking_player",3)))
        record(f["label"],f,state(uc),expected(f),execution)
    for dtype in range(5):
        for size in range(4):
            for damage in (0,1,127,128,257,65535,0x80000001,0xFFFFFFFF):
                uc=new(); ex=call(uc,0x43EB20,ecx=damage,edx=dtype,argv=(size,))
                record(f"scale_{dtype}_{size}_{damage}",dict(type=dtype,size=size,damage=damage),ex["return_eax"],u32(damage*FACTORS[dtype][size])>>8,ex)
    for uid in (15,39,48):
        for expansion in (0,1):
            for chitin in (0,1,2,255):
                for hero in (0,0x40):
                    f=dict(unit_id=uid,expansion=expansion,chitin=chitin,properties=hero,armor_upgrade=255)
                    uc=new(f); ex=call(uc,0x43AB10)
                    bonus=2 if uid in (39,48) and expansion and (chitin or hero) else 0
                    record(f"armor_bonus_{uid}_{expansion}_{chitin}_{hero}",f,ex["return_eax"],255+bonus,ex)
    for direction in range(256):
        uc=new(); ex=call(uc,0x422FE0,argv=(direction,))
        frame=next(x["stack_arguments"][0] for x in ex["calls"] if x["callee"]=="0x415370")
        record(f"shield_direction_{direction}",{"direction":direction},frame,(u32(direction-124)>>3)&31,ex)
    for attacker in (0,OTHER):
        for shield in (255,256,512):
            for dtype in (0,3):
                f=dict(shields=shield,type=dtype,matrix=512,matrix_timer=10)
                uc=new(f); ex=call(uc,0x41E2F0,argv=(0,0,attacker))
                actual={k:get(uc,UNIT+off,n) for k,off,n in (("hp",8,4),("shields",0x60,4),("matrix",0x112,2),("timer",0x114,1))}
                record(f"hallucinated_hit_{attacker}_{shield}_{dtype}",dict(f,attacker=attacker),actual,dict(hp=25600,shields=shield,matrix=512,timer=10),ex)
    for subunit in (False,True):
        for existing_target in (False,True):
            f={"weapon":34}; uc=new(f)
            put(uc,OTHER+0x4C,4,1);put(uc,UNIT+0x134,AI); put(uc,0x6726D0+3*36,2,1)
            if existing_target: put(uc,UNIT+0x7C,OTHER)
            if subunit:
                put(uc,UNIT+0x70,UNIT+0x400);put(uc,UNIT+0x400+0x64,16,2);put(uc,0x521660+16*4,0x10)
                put(uc,UNIT+0x400+0x90,0xFF,1)
            # The native AI preassignment makes the two target pointers equal;
            # an existing same attacker also returns from the real selector.
            ex=call(uc,0x41DFC0,argv=(2560,34,1,0,OTHER,3))
            expected_sub=4 if subunit and not existing_target else 0xFF
            actual=[get(uc,UNIT+8),get(uc,UNIT+0x90,1),get(uc,UNIT+0x7C)]
            expect=[23040,4,OTHER]
            if subunit: actual += [get(uc,UNIT+0x400+0x90,1),get(uc,UNIT+0x400+0x7C)]; expect += [expected_sub,OTHER if not existing_target else 0]
            record(f"enemy_attribution_complete_{subunit}_{existing_target}",dict(subunit=subunit,existing_target=existing_target,weapon=34,player_type=2),actual,expect,ex)
    for cooldown in (0,1):
        uc=new();put(uc,OTHER+0x4C,4,1);put(uc,OTHER+0xDC,0x301)
        put(uc,UNIT+0x134,AI);put(uc,0x508D50,3);put(uc,0x67CC90,0)
        put(uc,0x66727C,cooldown);put(uc,0x6D237C,37,1)
        put(uc,0x5264B0,0,2);put(uc,0x5264B2,0,2)
        ex=call(uc,0x41DFC0,argv=(2560,0,1,0,OTHER,3))
        record(f"enemy_attribution_hidden_attacker_notification_{cooldown}",
            dict(attacker_status=0x301,detected_by_mask=0,local_player=3,cooldown=cooldown,target_in_camera=True,ai_pointer=True),
            [get(uc,UNIT+8),get(uc,UNIT+0x90,1),get(uc,UNIT+0x8A,1),get(uc,UNIT+0x8B,1)],
            [23040,4,20,37],ex)
    for weapon in (0,34):
        uc=new({"weapon":weapon});put(uc,OTHER+0x4C,4,1);put(uc,OTHER+0xDC,0x301)
        put(uc,UNIT+0x7C,OTHER);put(uc,0x508D50,0);put(uc,0x67CC90,0)
        put(uc,0x5C99B0,0);put(uc,0x5D6E48,0);put(uc,0x5D6E5C,0)
        ex=call(uc,0x41DFC0,argv=(2560,weapon,1,0,OTHER,3))
        record(f"human_attribution_complete_empty_neighbors_{weapon}",
            dict(weapon=weapon,ai_pointer=False,existing_target=OTHER,attacker_status=0x301,spatial_count=0,local_player=0),
            [get(uc,UNIT+8),get(uc,UNIT+0x90,1),get(uc,UNIT+0x7C),get(uc,0x5D6E48),get(uc,0x5D6E5C)],
            [23040,4,OTHER,0,0],ex)
    for damage in (0,1,127,128,129,2560):
        for invincible in (False,True):
            f=dict(status=0x4000001 if invincible else 1)
            uc=new(f);ex=call(uc,0x41E150,argv=(damage,0,3,0))
            record(f"direct_hp_damage_{damage}_{invincible}",dict(f,damage=damage),get(uc,UNIT+8),25600-damage,ex)
    for flags in (0,2):
        for divisor in (1,2,3,9):
            for level in (0,1,3):
                uc=new();bullet=UNIT+0x800
                put(uc,bullet+0x4C,3,1);put(uc,bullet+0x60,0,1);put(uc,bullet+0x62,flags,1)
                put(uc,bullet+0x64,OTHER);put(uc,bullet+0x21,48,1)
                put(uc,0x5258E0,0,1);put(uc,0x5256C0,1,1)
                put(uc,0x525C80,6,2);put(uc,0x525D88,2,2);put(uc,0x534590+3*46+1,level,1)
                ex=call(uc,0x41E290,argv=(bullet,divisor))
                record(f"bullet_full_{flags}_{divisor}_{level}",dict(hit_flags=flags,divisor=divisor,base_damage=6,bonus=2,upgrade_level=level,source_owner=3),
                    get(uc,UNIT+8),25600-(0 if flags&2 else max(128,(6+2*level)*256//divisor)),ex)
    for level in (0,1,3,200):
        for bonus in (2,500):
            f=dict(hp=50000000,max_hp=50000000);uc=new(f)
            put(uc,OTHER+0x5C,UNIT);put(uc,0x5211D8+15,0,1)
            put(uc,0x5256C0,1,1);put(uc,0x525C80,6,2);put(uc,0x525D88,bonus,2)
            put(uc,0x534590+3*46+1,level,1)
            ex=call(uc,0x41FC70,ecx=OTHER)
            record(f"melee_full_{level}_{bonus}",dict(base_damage=6,bonus=bonus,upgrade_level=level,target_hp=50000000),
                get(uc,UNIT+8),50000000-(6+(level*bonus&65535))*256,ex)
            # Projectile computation uses a 32-bit product instead of melee's AX product.
            uc=new(f);bullet=UNIT+0x800;put(uc,bullet+0x60,0,1);put(uc,bullet+0x4C,3,1)
            put(uc,0x5258E0,0,1);put(uc,0x5256C0,1,1);put(uc,0x525C80,6,2);put(uc,0x525D88,bonus,2)
            put(uc,0x534590+3*46+1,level,1)
            ex=call(uc,0x409010,ecx=bullet,argv=(UNIT,))
            record(f"projectile_amount_{level}_{bonus}",dict(base_damage=6,bonus=bonus,upgrade_level=level),ex["return_eax"],(6+level*bonus)*256,ex)
    for maximum in (0,100,749,750,751,1200):
        for shieldmax in (0,100):
            f=dict(hp=1000000,max_hp=maximum*256,max_shields=shieldmax,shields=0,shield_enabled=int(shieldmax!=0))
            uc=new(f);bullet=UNIT+0x800;put(uc,bullet+0x60,0,1);put(uc,0x5258E0,5,1)
            ex=call(uc,0x409010,ecx=bullet,argv=(UNIT,))
            life=(maximum or (1000000+255)//256)+shieldmax
            record(f"yamato_amount_{maximum}_{shieldmax}",dict(f,weapon_behavior=5),ex["return_eax"],max(500,life*2//3)*256,ex)
    for weapon in (0,34):
        uc=new({"weapon":weapon});put(uc,OTHER+0x4C,4,1);put(uc,UNIT+0x134,AI)
        ex=call(uc,0x41DFC0,argv=(2560,weapon,1,0,OTHER,3),stop_before=0x4AEBC0)
        notify=next(x["stack_arguments"][1] for x in ex["calls"] if x["callee"]=="0x4203d0")
        record(f"notify_and_attribution_prefix_{weapon}",dict(weapon=weapon,stop_before="0x4aebc0"),[get(uc,UNIT+8),get(uc,UNIT+0x90,1),notify],[25600,4,int(weapon!=34)],ex)
    for capacity,building,hallucination in ((0,0,0),(8,0,0),(8,1,0),(8,0,0x40000000)):
        f=dict(status=1|hallucination,properties=building);uc=new(f);put(uc,0x523FE0+15,capacity,1)
        ex=call(uc,0x41E150,argv=(25600,0,3,0),stop_before=0x42A4D0)
        record(f"lethal_remove_prefix_{capacity}_{building}_{hallucination}",dict(f,capacity=capacity,damage=25600),[get(uc,UNIT+8),"0x4381d0" in ex["visited_functions"]],[0,bool(capacity and not building and not hallucination)],ex)
    for active in (False,True):
        for building in (0,1):
            uc=new(dict(properties=building));put(uc,0x523FE0+15,8,1)
            cargo=0x5DA3A0;put(uc,UNIT+0xB0,0x801,2)
            put(uc,cargo+0xA5,1,1);put(uc,cargo+0xC,SPRITE);put(uc,cargo+0x4D,3 if active else 0,1)
            ex=call(uc,0x41E150,argv=(25600,0,3,0),stop_before=0x42A4D0)
            cargo_first=active and not building
            record(f"cargo_death_boundary_{active}_{building}",dict(cargo_generation=1,handle=0x801,cargo_order=3 if active else 0,properties=building),
                [get(uc,UNIT+8),ex["boundary_ecx"]],[25600 if cargo_first else 0,hex(cargo if cargo_first else UNIT)],ex)
    for count in (0,1,254,255):
        for uid in (15,73):
            uc=new({"unit_id":uid});put(uc,UNIT+0x8F,count,1);put(uc,OTHER+0x8F,10,1)
            put(uc,OTHER+0x64,15,2);put(uc,UNIT+0xC0,OTHER)
            ex=call(uc,0x41FC40)
            record(f"kill_count_{uid}_{count}",dict(unit_id=uid,kill_count=count,parent_count=10),[get(uc,UNIT+0x8F,1),get(uc,OTHER+0x8F,1)],[min(count+1,255),11 if uid==73 else 10],ex)
    for mask in (0,8,16,32,56):
        for owner in (3,4,8):
            for status,properties in ((1,0),(0x40000001,0),(1,0x10)):
                uc=new(dict(status=status,properties=properties));put(uc,0x523C40+15,mask,1);put(uc,0x524290+15*2,123,2)
                addrs=[0x529154+3*4,0x5292A4+3*4,0x5293C4+3*4,0x531644+(15*12+3)*4,
                    0x529184+owner*4,0x5292D4+owner*4,0x5293F4+owner*4,0x52EB84+(15*12+owner)*4,
                    0x5291E4+owner*4,0x529334+owner*4]
                for address in addrs: put(uc,address,0)
                put(uc,0x6726D0+owner*36,2,1);put(uc,0x5349E0+owner,0,1)
                ex=call(uc,0x414080,edx=owner)
                valid=not(status&0x40000000 or properties&0x10); credit=valid and owner!=3 and owner<8
                unitcredit=bool(mask&8);buildingcredit=bool(mask&16) and not unitcredit
                expect=[int(valid and bool(mask&8)),int(valid and not(mask&8) and bool(mask&16)),int(valid and bool(mask&32)),int(valid),
                    int(credit and unitcredit),int(credit and buildingcredit),int(credit and bool(mask&32)),int(credit),
                    123 if credit and unitcredit else 0,123 if credit and buildingcredit else 0]
                record(f"death_scores_{mask}_{owner}_{status}_{properties}",dict(group_flags=mask,attacking_player=owner,status=status,properties=properties),[get(uc,a) for a in addrs],expect,ex)
    result={"target_sha256":TARGET_HASH,"target_version":"1.17.0.1","probe_script_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "extractor_dependency_sha256":hashlib.sha256(Path(__file__).with_name("inspect_native_damage_117.py").read_bytes()).hexdigest(),
        "engine":{"name":"Unicorn","version":unicorn.__version__,"host_arch":platform.machine(),"guest":"x86-32","python":platform.python_version()},
        "original_code_executed":True,"no_game_process_started":True,"replacement_helper_stubs":[],
        "allowlisted_code_ranges":[[hex(lo),hex(hi)] for lo,hi in ALLOW],"scope":PLAN,"cases":cases,"passed":len(cases),"failed":0}
    args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({"passed":len(cases),"failed":0,"complete_returns":sum(x["execution"]["complete_return"] for x in cases)}))

if __name__=="__main__": main()
