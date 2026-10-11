#!/usr/bin/env python3
"""Run isolated reviewed native military predicates, never a Windows process.

Default mode prints the plan. Execution requires --execute-original-code. Native
region/cargo/combat helpers remain original instructions, with explicit fixtures
and code bounds. Orders and tech availability stop before the named entry.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import pefile
from probe_native_unit_ai_117 import ALLOW as UNIT_ALLOW, TARGET_HASH
from inspect_native_dat_requirements_117 import WITNESSES as DAT_REQUIREMENT_CODE

UNIT,OTHER,AI,OTHER_AI,QUEUE=0x800000,0x800200,0x800800,0x800A00,0x802000
SPRITE,OTHER_SPRITE,REGIONS,NEIGHBORS=0x806000,0x806200,0x810000,0x803000
STACK,STOP,TERRAIN=0x900000,0xA00000,0xB00000
ALLOW=UNIT_ALLOW+[(0x417AA0,0x417AC8),(0x428AF0,0x428B18),(0x428D60,0x428D83),
    (0x429D10,0x429D17),(0x481A30,0x481A5C),(0x481AD0,0x481AF7),
    (0x481B10,0x481B98),(0x481BA0,0x481BB0),(0x481C90,0x481CA4),
    (0x4AB330,0x4AB41A),(0x4AB420,0x4AB447),(0x4AFAF0,0x4AFB1D),
    (0x4AFEC0,0x4AFF1A),(0x4AFF20,0x4B0024),(0x4B09C0,0x4B0AE5),
    (0x4B0E10,0x4B0EB9),(0x4B1100,0x4B11AB),(0x4B2110,0x4B21F0)]
ALLOW += [DAT_REQUIREMENT_CODE[name] for name in (
    "general_dat_evaluator", "technology_use_wrapper", "technology_use_offset_initializer",
    "error_set", "technology_available", "technology_researched", "producer_disabled")]
PLAN={"target_sha256":TARGET_HASH,"functions":["guard return-home", "regional slowest unit",
    "transport pickup wait", "member/follower cohesion", "stopped unit rejoin", "AI stim gates"],
    "limits":"Private x86-32 execution; no game process or campaign; order/tech calls stop at entry",
    "stubs":"None","instruction_limit":10000}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe",type=Path,required=True)
    parser.add_argument("--out",type=Path)
    parser.add_argument("--execute-original-code",action="store_true")
    args=parser.parse_args()
    raw=args.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=TARGET_HASH:parser.error("Unknown executable hash")
    if not args.execute_original_code:print(json.dumps(PLAN,indent=2));return
    if args.out is None:parser.error("--out required for execution evidence")
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP
    pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
    image=pe.get_memory_mapped_image();image_size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095
    cases=[]
    def put(uc,addr,value,n=4):uc.mem_write(addr,(value&((1<<(n*8))-1)).to_bytes(n,"little"))
    def get(uc,addr,n=4):return int.from_bytes(uc.mem_read(addr,n),"little")
    def new():
        uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(0x400000,image_size);uc.mem_write(0x400000,image)
        for addr,n in ((UNIT,0x20000),(STACK,4096),(STOP,4096),(TERRAIN,0x60000)):uc.mem_map(addr,n)
        put(uc,0x69B584,TERRAIN);put(uc,0x6B1A74,REGIONS);put(uc,TERRAIN,4,2)
        return uc
    def unit(uc,addr=UNIT,uid=15,x=0,sprite=SPRITE):
        put(uc,addr+0x64,uid,2);put(uc,addr+0xC,sprite);put(uc,addr+8,100*256)
        put(uc,addr+0xDC,1);put(uc,addr+0x4D,158,1)
        put(uc,sprite+0x14,x,2);put(uc,sprite+0xC,1,1)
        put(uc,addr+0x2C,x*256);put(uc,addr+0x10,x,2)
        put(uc,0x520708+uid*4,100*256);put(uc,0x520458+uid,0,1)
        put(uc,0x521660+uid*4,0);put(uc,0x523C40+uid,0,1)
        put(uc,0x5211D8+uid,1,1);put(uc,0x5213A8+uid,1,1)
        for off in (0,2,4,6):put(uc,0x522C38+uid*8+off,0,2)
        put(uc,0x5254B8+4,64);put(uc,0x5252B0+4,0)
    def region(uc,rid,state=8,center=0,group=1):
        ptr=REGIONS+rid*0x34
        put(uc,ptr,rid,2);put(uc,ptr+5,state,1)
        put(uc,TERRAIN+rid*0x40+0x449FE,group,2)
        put(uc,TERRAIN+rid*0x40+0x44A0C,center*256)
        put(uc,TERRAIN+rid*0x40+0x44A10,0)
        put(uc,TERRAIN+rid*0x40+0x44A00,60,2)
        return ptr
    def at_region(uc,x,rid):put(uc,TERRAIN+0xC+(x//32)*2,rid,2)
    def military(uc,addr=UNIT,node=AI,rid=1):
        put(uc,addr+0x134,node);put(uc,node+8,4,1);put(uc,node+0xC,addr)
        put(uc,node+0x10,REGIONS+rid*0x34)
    def call(uc,start,ecx=UNIT,edx=0,args=(),boundary=None):
        visited=[];stopped=None
        def hook(machine,addr,n,_):
            nonlocal stopped
            if addr==STOP or addr==boundary:stopped=addr;machine.emu_stop();return
            if not any(lo<=addr and addr+n<=hi for lo,hi in ALLOW):raise RuntimeError(f"Outside reviewed code: {addr:#x}")
            visited.append(addr)
        h=uc.hook_add(UC_HOOK_CODE,hook);sp=STACK+0x800
        uc.mem_write(sp,struct.pack("<"+"I"*(len(args)+1),STOP,*args))
        uc.reg_write(UC_X86_REG_ESP,sp);uc.reg_write(UC_X86_REG_ECX,ecx);uc.reg_write(UC_X86_REG_EDX,edx)
        uc.emu_start(start,STOP+1,count=10000);uc.hook_del(h)
        assert stopped is not None,"Instruction bound"
        final_sp=uc.reg_read(UC_X86_REG_ESP)
        return {"return_eax":uc.reg_read(UC_X86_REG_EAX),"stop_va":hex(stopped),"complete_return":stopped==STOP,
            "instruction_count":len(visited),"visited_functions":[hex(lo) for lo,hi in ALLOW if any(lo<=a<hi for a in visited)],
            "ecx_at_stop":uc.reg_read(UC_X86_REG_ECX),"edx_at_stop":uc.reg_read(UC_X86_REG_EDX),
            "stack_arguments_at_boundary":None if stopped==STOP else [get(uc,final_sp+4+i*4) for i in range(3)]}
    def record(label,inputs,actual,expected,execution):
        assert actual==expected,(label,actual,expected)
        cases.append({"case":label,"inputs":inputs,"actual":actual,"expected":expected,"passed":True,"execution":execution})
    guard_cases=[{"label":"no_ai","ai_type":None},{"label":"worker_ai","ai_type":2},
        {"label":"military_ai","ai_type":4},{"label":"different_regions"},
        {"label":"same_region","same_region":True},{"label":"auto_target","auto_target":True},
        {"label":"home_192","home":192},{"label":"home_193","home":193},
        {"label":"home_193_fractional_position","home":193,"exact_x_raw":1},
        {"label":"terran_mechanical_damaged","race":2,"mechanical":True,"hp":99*256},
        {"label":"terran_mechanical_fractional_damage","race":2,"mechanical":True,"hp":100*256-1},
        {"label":"terran_mechanical_full","race":2,"mechanical":True},
        {"label":"zerg_mechanical_damaged","race":1,"mechanical":True,"hp":99*256},
        {"label":"protoss_mechanical_damaged","race":4,"mechanical":True,"hp":99*256},
        {"label":"patrol_queued","queue_order":152},{"label":"move_queued","queue_order":6},
        {"label":"in_range_target","target_x":50},{"label":"outside_range_target","target_x":100}]
    for fixture in guard_cases:
        uc=new();unit(uc);region(uc,1);region(uc,2)
        home=fixture.get("home",320);ai_type=fixture.get("ai_type",1)
        if ai_type is not None:
            put(uc,UNIT+0x134,AI);put(uc,AI+8,ai_type,1);put(uc,AI+0x12,home,2)
        put(uc,UNIT+8,fixture.get("hp",25600));put(uc,UNIT+0x2C,fixture.get("exact_x_raw",0));put(uc,0x523C40+15,fixture.get("race",0),1)
        if fixture.get("mechanical"):put(uc,0x521660+15*4,0x40000000)
        if fixture.get("auto_target"):put(uc,UNIT+0x7C,OTHER)
        if "queue_order" in fixture:put(uc,UNIT+0x74,QUEUE);put(uc,QUEUE+8,fixture["queue_order"],1)
        at_region(uc,0,1);at_region(uc,home,1 if fixture.get("same_region") else 2)
        if "target_x" in fixture:
            unit(uc,OTHER,16,fixture["target_x"],OTHER_SPRITE);put(uc,UNIT+0x5C,OTHER)
        blocked=(ai_type!=1 or fixture.get("same_region") or fixture.get("auto_target") or (home*256-fixture.get("exact_x_raw",0))//256<=192 or
            fixture.get("race")==2 and fixture.get("mechanical") and fixture.get("hp",25600)!=25600 or
            fixture.get("queue_order")==152 or fixture.get("target_x",1000)<=65)
        execution=call(uc,0x4B2110,edx=1)
        record("guard_home_"+fixture["label"],dict(fixture,dont_issue=1),execution["return_eax"],int(not blocked),execution)
    uc=new();unit(uc);region(uc,1);region(uc,2);at_region(uc,0,1);at_region(uc,320,2)
    put(uc,UNIT+0x134,AI);put(uc,AI+8,1,1);put(uc,AI+0x12,320,2)
    execution=call(uc,0x4B2110,boundary=0x42A340)
    record("guard_home_issue_boundary",{"home":[320,0],"dont_issue":0},
        [execution["stop_va"],execution["stack_arguments_at_boundary"]],["0x42a340",[163,320,0]],execution)
    eligibility=[("no_ai",None,UNIT,0),("guard_ai",1,UNIT,0),("wrong_parent",4,OTHER,0),
                 ("building",4,UNIT,1),("military",4,UNIT,0)]
    for label,ai_type,parent,properties in eligibility:
        uc=new();unit(uc);r=region(uc,1);put(uc,r+0x24,OTHER)
        if ai_type is not None:
            military(uc);put(uc,AI+8,ai_type,1);put(uc,AI+0xC,parent)
        put(uc,0x521660+15*4,properties)
        execution=call(uc,0x4AFAF0)
        record("slowest_"+label,{"ai_type":ai_type,"parent":parent,"properties":properties},execution["return_eax"],OTHER if label=="military" else 0,execution)
    for order in (6,96):
        for loaded in (0,1):
            for self_node in (0,1):
                uc=new();unit(uc);unit(uc,OTHER,16,sprite=OTHER_SPRITE);r=region(uc,1);military(uc);military(uc,OTHER,OTHER_AI)
                put(uc,r+0x30,AI if self_node else OTHER_AI)
                put(uc,UNIT+0x4D,order,1);put(uc,OTHER+0x4D,order,1)
                put(uc,UNIT+0xDC,1|(0x40 if loaded else 0));put(uc,OTHER+0xDC,1|(0x40 if loaded else 0))
                execution=call(uc,0x4AFEC0)
                record(f"pickup_wait_{order}_{loaded}_{self_node}",{"other_order":order,"loaded":loaded,"self_node":self_node},execution["return_eax"],int(order==96 and not loaded and not self_node),execution)
    for label,x,center,threshold,state,slowest,loaded,groups in [
        ("at_128",128,1000,60,8,OTHER,0,True),("at_129_far_center",129,1000,60,8,OTHER,0,True),
        ("center_equal_384",129,513,60,8,OTHER,0,True),("center_outside_384",129,514,60,8,OTHER,0,True),
        ("terrain_59",300,1000,59,8,OTHER,0,True),("terrain_60",300,1000,60,8,OTHER,0,True),
        ("state_two",300,1000,60,2,OTHER,0,True),("slowest_absent",300,1000,60,8,0,0,True),
        ("self_slowest",300,1000,60,8,UNIT,0,True),("slowest_loaded",300,1000,60,8,OTHER,1,True),
        ("different_region_group",300,1000,60,8,OTHER,0,False)]:
        uc=new();unit(uc,x=x);unit(uc,OTHER,16,0,OTHER_SPRITE);r=region(uc,1,state,center);military(uc)
        put(uc,r+0x24,slowest);put(uc,OTHER+0xDC,1|(0x40 if loaded else 0))
        at_region(uc,0,1);at_region(uc,x,1 if groups else 2);region(uc,2,group=2)
        put(uc,TERRAIN+0x40+0x44A00,threshold,2)
        execution=call(uc,0x4B09C0)
        expected=int(not (x>128 and abs(center-x)>384 and threshold>=60 and state!=2 and slowest not in (0,UNIT) and not loaded and groups))
        record("follower_"+label,{"distance_to_slowest":x,"center_x":center,"terrain_threshold":threshold,"region_state":state,"slowest":slowest,"slowest_loaded":loaded,"same_region_group":groups},execution["return_eax"],expected,execution)
    for x in (256,257,384,385):
        for center in (0,1000):
            uc=new();unit(uc);unit(uc,OTHER,16,x,OTHER_SPRITE);r=region(uc,1,center=center);military(uc);military(uc,OTHER,OTHER_AI)
            put(uc,r+0x24,UNIT);put(uc,r+0x30,OTHER_AI);at_region(uc,0,1);at_region(uc,x,1)
            execution=call(uc,0x4AFF20)
            expected=int(256<x<=384 and abs(center-x)>384)
            record(f"group_wait_distance_{x}_center_{center}",{"distance":x,"region_center_x":center},execution["return_eax"],expected,execution)
    # Adjacent regions execute real group-ID and neighbor-list checks, without BFS.
    uc=new();region(uc,1);region(uc,2);put(uc,TERRAIN+0x40+0x44A08,NEIGHBORS)
    put(uc,TERRAIN+0x40+0x44A03,1,1);put(uc,NEIGHBORS,2,2)
    execution=call(uc,0x4AB330,ecx=0,edx=1,args=(2,))
    record("adjacent_regions_reachable",{"source_region":1,"target_region":2,"neighbor_list":[2]},execution["return_eax"],1,execution)
    for uid in (15,34):
        for current_region in (1,2):
            uc=new();unit(uc,uid=uid);r=region(uc,2,center=320);military(uc,rid=2);at_region(uc,0,current_region)
            execution=call(uc,0x4B0E10,boundary=0x42A340)
            expected=STOP if current_region==2 else 0x42A340
            actual=[execution["stop_va"],execution["stack_arguments_at_boundary"]]
            record(f"rejoin_unit_{uid}_current_{current_region}",{"unit_id":uid,"current_region":current_region,"assigned_region":2},actual,
                [hex(expected),None if current_region==2 else [177 if uid==34 else 158,320,0]],execution)
    for stim,upgrade,ensnare in ((s,u,e) for s in (0,1) for u in (0,1) for e in (0,1)):
        uc=new();unit(uc,uid=0);unit(uc,OTHER,16,50,OTHER_SPRITE);put(uc,UNIT+0x5C,OTHER)
        put(uc,UNIT+0x115,stim,1);put(uc,UNIT+0x116,ensnare,1);put(uc,UNIT+0xDC,1|(0x10000000 if upgrade else 0))
        modifier=stim+upgrade-ensnare
        execution=call(uc,0x4B1100,boundary=0x407F90)
        expected=STOP if modifier>0 else 0x407F90
        record(f"stim_modifier_{stim}_{upgrade}_{ensnare}",{"stim_timer":stim,"speed_upgrade_status":upgrade,"ensnare_timer":ensnare},execution["stop_va"],hex(expected),execution)
    for uid,hp,target_x,enemy_disabled in ((15,100*256,50,False),(0,49*256,50,False),
                                         (32,50*256,100,False),(0,50*256,50,True)):
        uc=new();unit(uc,uid=uid);unit(uc,OTHER,16,target_x,OTHER_SPRITE)
        put(uc,UNIT+8,hp);put(uc,UNIT+0x5C,OTHER)
        if enemy_disabled:put(uc,OTHER+0xDC,0x401)
        execution=call(uc,0x4B1100)
        record(f"stim_reject_{uid}_{hp}_{target_x}_{enemy_disabled}",{"unit_id":uid,"hp_raw":hp,"target_x":target_x,"enemy_disabled":enemy_disabled},execution["stop_va"],hex(STOP),execution)
    for label,uid,hp,enemy_hp,available,researched in (
        ("marine_eligible",0,50*256,100*256,1,1),
        ("firebat_eligible",32,50*256,100*256,1,1),
        ("fraction_hp_rounds_half",0,49*256+1,100*256,1,1),
        ("enemy_below_100",0,50*256,99*256,1,1),
        ("enemy_fraction_rounds_100",0,50*256,99*256+1,1,1),
        ("technology_unavailable",0,50*256,100*256,0,1),
        ("technology_unresearched",0,50*256,100*256,1,0),
        ("hero_not_ai_stim_class",20,50*256,100*256,1,1)):
        uc=new();unit(uc,uid=uid);unit(uc,OTHER,16,50,OTHER_SPRITE)
        put(uc,UNIT+8,hp);put(uc,OTHER+8,enemy_hp);put(uc,UNIT+0x5C,OTHER)
        uc.mem_write(0x51F918,struct.pack("<44H",*([65535]*44)))
        put(uc,0x534104,available,1);put(uc,0x534224,researched,1);put(uc,0x5432FC,0)
        execution=call(uc,0x4B1100,boundary=0x438AF0)
        eligible=uid in (0,32) and (hp+255)//256>=50 and (enemy_hp+255)//256>=100 and available and researched
        record("stim_original_tech_"+label,{"unit_id":uid,"hp_raw":hp,"enemy_hp_raw":enemy_hp,"technology_available":available,"technology_researched":researched,"native_tech_offsets_initialized":True},
               execution["stop_va"],"0x438af0" if eligible else hex(STOP),execution)
    result={"target_sha256":TARGET_HASH,"target_version":"1.17.0.1","original_code_executed":True,
        "no_game_process_started":True,"replacement_helper_stubs":[],"scope":PLAN,
        "probe_script_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "dependency_hashes":{name:hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                             for name in ("probe_native_unit_ai_117.py","inspect_native_dat_requirements_117.py")},
        "engine":{"name":"Unicorn","version":unicorn.__version__,"host_arch":platform.machine(),"guest":"x86-32","python":platform.python_version()},
        "allowlisted_code_ranges":[[hex(lo),hex(hi)] for lo,hi in ALLOW],"cases":cases,"passed":len(cases),"failed":0}
    args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({"passed":len(cases),"failed":0,"complete_returns":sum(x["execution"]["complete_return"] for x in cases)}))

if __name__=="__main__":main()
