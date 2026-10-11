#!/usr/bin/env python3
"""Inspect original training queue, supply/resource tests and AI request service.

Static by default. Explicit probes execute bounded original code in private
Unicorn memory, with no helper stubs or full-game/Windows/API execution.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import capstone
import pefile
import inspect_native_requirements_117 as unit_reqs

TARGET_HASH=unit_reqs.TARGET_HASH
PLAYER_BASE,STRIDE=0x67D068,0x4E8
WITNESSES={
 "normal_training_consumer":(0x487DE0,0x488025),"town_worker_training_consumer":(0x487D30,0x487DD7),
 "enqueue_training_unit":(0x41B4B0,0x41B542),"enqueue_with_ai_assignment":(0x41B550,0x41B5BC),
 "training_ai_assignment":(0x4844A0,0x4844BB),"secondary_order_change":(0x42A1E0,0x42A20C),
 "unit_race_match":(0x4860A0,0x4860F3),"resource_gate_ai_unit":(0x4826D0,0x48271F),
 "resource_gate_ai_technology":(0x482720,0x48276D),"resource_gate_ai_upgrade":(0x482770,0x4827BD),
 "prepare_supply_check":(0x4084D0,0x4085DC),"cached_resource_stock_compare":(0x4085E0,0x408649),
 "actual_stock_debit":(0x408650,0x408670),"unit_technology_upgrade_check":(0x408670,0x408759),
 "cached_cost_getters":(0x408760,0x408788),"supply_race_id":(0x414550,0x414574),
 "supply_available_to_cap":(0x414180,0x4142D6),"supply_total":(0x4142E0,0x414319),
 "supply_used":(0x414320,0x414352),"resource_feedback":(0x4461F0,0x446278),
 "spending_top_getters":(0x483530,0x48356C),"spending_unit_limit":(0x4835E0,0x483647),
 "campaign_predicate":(0x4831E0,0x4831F1),"spending_cost":(0x481FF0,0x48203C),
 "unit_technology_upgrade_cost":(0x408790,0x40885E),"upgrade_price":(0x43B270,0x43B2C9),
 "spending_remove_cost":(0x482260,0x482304),"spending_pop":(0x482310,0x4823C8),
 "spending_success_pop":(0x482BC0,0x482BD6),"spending_failure_pop":(0x482C70,0x482C82),
 "error_get":(0x4B38A0,0x4B38A6),"error_set":(0x4B38B0,0x4B38B7),
}
SUPPLY_TABLES=[(0x529424,0x529454,0x529484),(0x5294B4,0x5294E4,0x529514),(0x529544,0x529574,0x5295A4)]


def probes(data,pe):
    if platform.machine().lower() not in ("x86_64","amd64"):raise RuntimeError("Use reviewed x86_64 Rosetta runtime")
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP
    unit,ai,town,stack,stop=0x800000,0x800400,0x800800,0x900000,0xA00000
    base,image=pe.OPTIONAL_HEADER.ImageBase,pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095
    allowed=list(WITNESSES.values())+list(unit_reqs.WITNESSES.values());cases=[]
    def w32(u,a,v):u.mem_write(a,struct.pack("<I",v&0xFFFFFFFF))
    def w16(u,a,v):u.mem_write(a,struct.pack("<H",v&65535))
    def w8(u,a,v):u.mem_write(a,bytes([v&255]))
    def r32(u,a):return struct.unpack("<I",u.mem_read(a,4))[0]
    def r16(u,a):return struct.unpack("<H",u.mem_read(a,2))[0]
    def r8(u,a):return u.mem_read(a,1)[0]
    def new(player=7,ty=0,cost=(50,25,2),group=2,properties=0):
        u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,size);u.mem_write(base,image);u.mem_map(unit,0x10000);u.mem_map(stack,0x2000);u.mem_map(stop,0x1000)
        u.mem_write(PLAYER_BASE,bytes(STRIDE*8));u.mem_write(0x529604,bytes(228*12*4));u.mem_write(0x52C0C4,bytes(228*12*4))
        u.mem_write(0x52655C,bytes([1])*(228*12));u.mem_write(0x523A78,struct.pack("<228H",*([65535]*228)))
        w32(u,0x508D50,0);w32(u,0x67CC90,0);w32(u,0x5432FC,0)
        w16(u,unit+0x64,111);w8(u,unit+0x4C,player);w8(u,unit+0x4D,0x9C);w32(u,unit+0xDC,3)
        w8(u,unit+0xC8,44);w8(u,unit+0xC9,61);w32(u,unit+0x134,ai);w8(u,ai+8,3)
        u.mem_write(unit+0x98,struct.pack("<5H",*([228]*5)))
        w16(u,0x523520+ty*2,cost[0]);w16(u,0x5236E8+ty*2,cost[1]);w8(u,0x523E10+ty,cost[2]);w32(u,0x521660+ty*4,properties)
        w8(u,0x523C40+ty,group);w8(u,0x523C40+111,2)
        w32(u,0x5263D0+player*4,300);w32(u,0x526400+player*4,100)
        for maximum,used,total in SUPPLY_TABLES:
            w32(u,maximum+player*4,400);w32(u,used+player*4,0);w32(u,total+player*4,20)
        p=PLAYER_BASE+player*STRIDE
        for j,v in enumerate(cost):w32(u,p+j*4,v);w32(u,p+0xC+j*4,1000)
        w32(u,p+0x18,90|(1<<8)|(ty<<16));w32(u,p+0x1C,town);w8(u,p+0x210,1)
        return u
    def call(u,start,ecx=7,edx=0,args=(),checkpoint=None):
        reached,count,trace=False,0,[]
        def hook(m,a,n,_):
            nonlocal reached,count
            if a==stop or a==checkpoint:reached=True;m.emu_stop();return
            if not any(lo<=a and a+n<=hi for lo,hi in allowed):raise RuntimeError(f"Unreviewed original instruction {a:#x}")
            count+=1
            if a in (0x41B4B0,0x41B550,0x482BC0,0x482C70,0x42A1E0,0x4461F0):trace.append(hex(a))
        h=u.hook_add(UC_HOOK_CODE,hook);sp=stack+0x1800;u.mem_write(sp,struct.pack("<"+"I"*(len(args)+1),stop,*args))
        u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
        u.emu_start(start,stop+1,count=100000);u.hook_del(h)
        if not reached:raise RuntimeError("No return/checkpoint within100000 instructions")
        return u.reg_read(UC_X86_REG_EAX),count,trace
    def observe(kind,inputs,ex,got,expected,scope="complete reviewed function for fixture path"):
        if got!=expected:raise AssertionError((kind,inputs,got,expected))
        cases.append({"kind":kind,"fixture_inputs":inputs,"observed":got,"expected_from_static_contract":expected,"matched":True,"original_instructions":ex[1],"native_call_trace":ex[2],"execution_scope":scope,"helper_stubs":[]})
    def cache(u,player=7):return [r32(u,a+player*4) for a in (0x53DAD0,0x53DB00,0x53DB30)]
    def supply_setup(u,race,maximum,used,total,player=7):
        a,b,c=SUPPLY_TABLES[race];w32(u,a+player*4,maximum);w32(u,b+player*4,used);w32(u,c+player*4,total)
    def queue(u):return list(struct.unpack("<5H",u.mem_read(unit+0x98,10)))
    for wanted_group in range(8):
        for producer_group in range(8):
            u=new(group=wanted_group);w8(u,0x523C40+111,producer_group)
            ex=call(u,0x4860A0,unit,0)
            tested_bit=2 if wanted_group&2 else (1 if wanted_group&1 else 4)
            accepted=bool(producer_group&tested_bit)
            observe("requested race precedence including no-group fallback",{"requested_group":wanted_group,"producer_group":producer_group},ex,
                {"return":ex[0],"error":r32(u,0x6BE654)}, {"return":int(accepted),"error":0 if accepted else 25})
    for race,group in enumerate((1,2,4)):
        for maximum,used,total,expected in [(400,0,20,20),(400,10,20,10),(12,10,20,2),(400,20,20,0),(12,15,20,0xFFFFFFFD),(12,5,8,3)]:
            u=new(group=group);supply_setup(u,race,maximum,used,total)
            ex=call(u,0x414180,0,7)
            observe("remaining supply min(total,maximum)-used",{"race_group":group,"maximum":maximum,"used":used,"total":total},ex,ex[0],expected)
    for cheat in (False,True):
        u=new(group=2);supply_setup(u,1,12,10,20);w32(u,0x5432FC,0x20000 if cheat else 0)
        ex=call(u,0x414180,0,7);observe("supply cheat bypasses maximum only",{"cheat":cheat},ex,ex[0],10 if cheat else 2)
    for stock_m,stock_g,used,total,supply,props,expected in [
        (50,25,18,20,2,0,1),(49,25,18,20,2,0,0),(50,24,18,20,2,0,0),
        (50,25,19,20,2,0,0),(50,25,20,20,0,0,1),(50,25,20,20,2,0x1000000,1),
        (50,25,17,20,2,0x400,0),(50,25,16,20,2,0x400,1),
        (0xFFFFFFFF,25,0,20,2,0,0),
    ]:
        u=new(cost=(50,25,supply),properties=props);w32(u,0x5263D0+28,stock_m);w32(u,0x526400+28,stock_g);supply_setup(u,1,400,used,total)
        ex=call(u,0x4086B0,7,0,[1,1]);observe("unit stock and supply check actual feedback nonviewer path",{"minerals":stock_m,"gas":stock_g,"used":used,"total":total,"supply":supply,"properties":props,"viewer":0,"player":7},ex,
            {"return":ex[0],"cached":cache(u)}, {"return":expected,"cached":[50,25,supply*(2 if props&0x400 else 1)]})
    for budget in [(50,25,2),(49,25,2),(50,24,2),(50,25,1)]:
        u=new();p=PLAYER_BASE+7*STRIDE
        for j,v in enumerate(budget):w32(u,p+0xC+j*4,v)
        ex=call(u,0x4826D0);observe("AI reserved budget is additional unsigned gate",{"available_budget":budget},ex,ex[0],int(all(v>=c for v,c in zip(budget,(50,25,2)))))
    for kind in ("technology","upgrade"):
        for stock_m,stock_g,budget in [(50,25,(50,25,0)),(49,25,(50,25,0)),(50,24,(50,25,0)),(50,25,(49,25,0)),(50,25,(50,24,0))]:
            u=new();p=PLAYER_BASE+7*STRIDE;ident=5
            w16(u,0x51F760+ident*2,50);w16(u,0x51F7B8+ident*2,25)
            w8(u,0x534590+7*46+ident,2)
            for a,v in ((0x5248D0,10),(0x524950,20),(0x5249D0,5),(0x524A50,10)):w16(u,a+ident*2,v)
            w32(u,0x5263D0+28,stock_m);w32(u,0x526400+28,stock_g)
            for j,v in enumerate(budget):w32(u,p+0xC+j*4,v)
            ex=call(u,0x482720 if kind=="technology" else 0x482770,7,ident)
            accepted=stock_m>=50 and stock_g>=25 and budget[0]>=50 and budget[1]>=25
            observe("technology or current-level upgrade resource and budget gate",{"kind":kind,"current_upgrade_level":2,"minerals":stock_m,"gas":stock_g,"budget":budget},ex,
                {"return":ex[0],"cache":cache(u)}, {"return":int(accepted),"cache":[50,25,0]})
    u=new();w8(u,0x534590+7*46+5,2)
    for a,v in ((0x5248D0,65535),(0x524950,3),(0x5249D0,65535),(0x524A50,7)):w16(u,a+10,v)
    ex=call(u,0x482770,7,5);observe("upgrade cached price truncates AX",{"level":2,"mineral_base_factor":[65535,3],"gas_base_factor":[65535,7]},ex,
        {"return":ex[0],"cache":cache(u)}, {"return":1,"cache":[5,13,0]})
    for hydra_supply,lurker_supply,used,total,expected in [(2,4,18,20,1),(2,4,19,20,0),(4,2,0,20,1)]:
        u=new(ty=103,cost=(50,25,lurker_supply),group=1);w8(u,0x523E10+38,hydra_supply);supply_setup(u,0,400,used,total)
        ex=call(u,0x4086B0,7,103,[1,1]);observe("Lurker incremental supply subtraction",{"hydra_supply":hydra_supply,"lurker_supply":lurker_supply,"used":used,"total":total},ex,
                    {"return":ex[0],"cached_supply":cache(u)[2]}, {"return":expected,"cached_supply":(lurker_supply-hydra_supply)&0xFFFFFFFF})
    for head in range(5):
        for occupied in range(6):
            u=new();w8(u,unit+0xA4,head);q=[228]*5
            for j in range(occupied):q[(head+j)%5]=2
            u.mem_write(unit+0x98,struct.pack("<5H",*q));w32(u,0x53DB30+28,777)
            ex=call(u,0x41B550,unit,0,[0,2,town]);expected=q[:]
            accepted=occupied<5;slot=(head+occupied)%5
            if accepted:expected[slot]=0
            observe("queue insertion and AI tag circular first empty",{"head":head,"occupied":occupied,"tag":2,"context":town},ex,
                {"return":ex[0],"queue":queue(u),"stocks":[r32(u,0x5263D0+28),r32(u,0x526400+28)],"tags":list(u.mem_read(ai+9,5)),"contexts":[r32(u,ai+0x18+4*j) for j in range(5)],"cached_supply":cache(u)[2]},
                {"return":int(accepted),"queue":expected,"stocks":[250,75] if accepted else [300,100],"tags":[2 if accepted and j==slot else 0 for j in range(5)],"contexts":[town if accepted and j==slot else 0 for j in range(5)],"cached_supply":2 if occupied==0 else 777})
    for properties in (0,1):
        u=new(properties=properties);ex=call(u,0x41B4B0,unit,0,[0]);observe("building-property queue insertion does not debit",{"properties":properties},ex,
            {"return":ex[0],"stocks":[r32(u,0x5263D0+28),r32(u,0x526400+28)]},{"return":1,"stocks":[300,100] if properties&1 else [250,75]})
    for supply in (127,128,255):
        u=new(cost=(50,25,supply),properties=0x400);supply_setup(u,1,1000,0,1000)
        ex=call(u,0x4086B0,7,0,[1,0]);observe("live supply check doubles dword without byte wrap",{"raw_supply":supply,"properties":0x400},ex,
            {"return":ex[0],"cached_supply":cache(u)[2]}, {"return":1,"cached_supply":supply*2})
    for first in (False,True):
        u=new();supply_setup(u,1,400,20,20)
        if not first:w16(u,unit+0x98,2)
        ex=call(u,0x41B4B0,unit,0,[0]);observe("appended unit skips supply gate",{"insert_at_head":first,"used_equals_total":True},ex,
            {"return":ex[0],"queue":queue(u)}, {"return":int(not first),"queue":[228]*5 if first else [2,0,228,228,228]})
    for consume_available in (False,True):
        for current,budget in [((100,50,10),(100,50,10)),((10,5,1),(10,5,1))]:
            u=new();p=PLAYER_BASE+7*STRIDE
            for j,v in enumerate(current):w32(u,p+j*4,v)
            for j,v in enumerate(budget):w32(u,p+0xC+j*4,v)
            ex=call(u,0x482BC0 if consume_available else 0x482C70)
            expected=[max(0,v-c) for v,c in zip(current,(50,25,2))]
            expectedbudget=[(v-c)&0xFFFFFFFF for v,c in zip(budget,(50,25,2))] if consume_available else list(budget)
            observe("spending pop demand saturates available wraps",{"success_pop":consume_available,"current_demand":current,"available_budget":budget},ex,
                {"demand":[r32(u,p+j*4) for j in range(3)],"available":[r32(u,p+0xC+j*4) for j in range(3)],"heap_count":r8(u,p+0x210)},
                {"demand":expected,"available":expectedbudget,"heap_count":0})
    for ty in (44,62,63,68,103):
        u=new(ty=ty);p=PLAYER_BASE+7*STRIDE;ex=call(u,0x482BC0)
        observe("special ID pop skips all cost removal",{"id":ty},ex,{"demand":[r32(u,p+j*4) for j in range(3)],"available":[r32(u,p+0xC+j*4) for j in range(3)],"heap_count":r8(u,p+0x210)},
                {"demand":[50,25,2],"available":[1000]*3,"heap_count":0})
    for kind,initial_secondary in [(1,0),(2,0),(1,0x26)]:
        u=new();p=PLAYER_BASE+7*STRIDE;w8(u,p+0x19,kind);w8(u,unit+0xA6,initial_secondary)
        w32(u,unit+0xE8,123);w32(u,unit+0xEC,0x800C00);w8(u,unit+0xE2,5)
        ex=call(u,0x487DE0,7,unit,[1,1])
        got={"return":ex[0],"queue":queue(u),"stocks":[r32(u,0x5263D0+28),r32(u,0x526400+28)],"demand":[r32(u,p+j*4) for j in range(3)],"available":[r32(u,p+0xC+j*4) for j in range(3)],"heap_count":r8(u,p+0x210),"ai_tag":r8(u,ai+9),"ai_context":r32(u,ai+0x18),"secondary":r8(u,unit+0xA6),"secondary_state":[r32(u,unit+0xE8),r32(u,unit+0xEC),r8(u,unit+0xE2)]}
        expected={"return":1,"queue":[0,228,228,228,228],"stocks":[250,75],"demand":[0,0,0],"available":[950,975,998],"heap_count":0,"ai_tag":kind,"ai_context":town,"secondary":0x26,"secondary_state":[123,0x800C00,5] if initial_secondary==0x26 else [0,0,0]}
        observe("ordinary training consumer full success path",{"kind":kind,"initial_secondary":initial_secondary,"unit_id":0,"producer":111,"no_retry":1},ex,got,expected)
    for failure in ("race","unavailable","missing prerequisite","unit limit","resources campaign","resources melee"):
        u=new();p=PLAYER_BASE+7*STRIDE
        if failure=="race":w8(u,0x523C40,1)
        if failure=="unavailable":w8(u,0x52655C+7*228,0)
        if failure=="missing prerequisite":w16(u,unit+0x64,113)
        if failure=="unit limit":w8(u,p+0x3F0,255)
        if failure.startswith("resources"):
            w32(u,0x5263D0+28,49);w16(u,p+0x218,0x20 if failure.endswith("campaign") else 0)
        ex=call(u,0x487DE0,7,unit,[1,1]);pop=failure in ("unavailable","unit limit","resources melee")
        observe("training consumer actual failure and retry policy",{"failure":failure,"no_retry":1},ex,
             {"return":ex[0],"heap_count":r8(u,p+0x210),"stocks":[r32(u,0x5263D0+28),r32(u,0x526400+28)],"queue":queue(u),"demand":[r32(u,p+j*4) for j in range(3)],"available":[r32(u,p+0xC+j*4) for j in range(3)]},
             {"return":0,"heap_count":0 if pop else 1,"stocks":[49,100] if failure.startswith("resources") else [300,100],"queue":[228]*5,"demand":[0,0,0] if pop else [50,25,2],"available":[1000]*3})
    for failure in (None,"town mismatch","race","unavailable","resources","unit limit"):
        u=new(ty=7);p=PLAYER_BASE+7*STRIDE;w16(u,unit+0x64,106);w8(u,0x523C40+106,2)
        w32(u,ai+0x14,town);w8(u,p+0x19,4);u.mem_write(ai+9,bytes([99]*5))
        if failure=="town mismatch":w32(u,ai+0x14,0)
        if failure=="race":w8(u,0x523C40+7,1)
        if failure=="unavailable":w8(u,0x52655C+7*228+7,0)
        if failure=="resources":w32(u,0x5263D0+28,49)
        if failure=="unit limit":w8(u,p+0x3F0+7,255)
        ex=call(u,0x487D30,7,unit,[ai]);success=failure is None;pop=success or failure=="unit limit"
        observe("town worker training actual queue payment and failure paths",{"requested_unit":7,"producer":106,"failure":failure},ex,
            {"heap_count":r8(u,p+0x210),"queue":queue(u),"stocks":[r32(u,0x5263D0+28),r32(u,0x526400+28)],"secondary":r8(u,unit+0xA6),"tags":list(u.mem_read(ai+9,5)),"demand":[r32(u,p+j*4) for j in range(3)],"available":[r32(u,p+0xC+j*4) for j in range(3)]},
            {"heap_count":0 if pop else 1,"queue":[7,228,228,228,228] if success else [228]*5,"stocks":[250,75] if success else ([49,100] if failure=="resources" else [300,100]),"secondary":0x26 if success else 0,"tags":[99]*5,"demand":[0]*3 if pop else [50,25,2],"available":[950,975,998] if success else [1000]*3})
    return {"format_version":1,"target_sha256":hashlib.sha256(data).hexdigest(),"probe_script_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),"dependency_script_sha256":{Path(unit_reqs.__file__).name:hashlib.sha256(Path(unit_reqs.__file__).read_bytes()).hexdigest()},
       "original_code_executed":True,"complete_game_executed":False,"helper_stubs":[],"runtime_versions":{"python":platform.python_version(),"unicorn":unicorn.__version__,"capstone":capstone.__version__,"pefile":pefile.__version__,"architecture":platform.machine(),"system":platform.system()},"allowlisted_preferred_va_ranges":[[hex(a),hex(b)] for a,b in allowed],"all_matched":True,"case_count":len(cases),"cases":cases}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--exe",type=Path,required=True);p.add_argument("--out",type=Path,required=True);p.add_argument("--execute-isolated-original-code",action="store_true");p.add_argument("--probe-out",type=Path);a=p.parse_args()
    if a.execute_isolated_original_code and a.probe_out is None:p.error("--probe-out required")
    data=a.exe.read_bytes()
    if hashlib.sha256(data).hexdigest()!=TARGET_HASH:p.error("Wrong EXE hash")
    pe=pefile.PE(data=data);c=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    out={"format_version":1,"target_sha256":TARGET_HASH,"inspector_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),"dependency_script_sha256":{Path(unit_reqs.__file__).name:hashlib.sha256(Path(unit_reqs.__file__).read_bytes()).hexdigest()},"original_code_executed":False,"complete_game_executed":False,"witnesses":{}}
    for name,(start,end) in WITNESSES.items():
        raw=pe.get_data(start-0x400000,end-start);out["witnesses"][name]={"start_preferred_va":hex(start),"end_exclusive_preferred_va":hex(end),"bytes_sha256":hashlib.sha256(raw).hexdigest(),"instructions":[{"address":hex(i.address),"bytes":i.bytes.hex(),"mnemonic":i.mnemonic,"operands":i.op_str} for i in c.disasm(raw,start)]}
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(out,indent=2)+"\n")
    if a.execute_isolated_original_code:
        r=probes(data,pe);a.probe_out.parent.mkdir(parents=True,exist_ok=True);a.probe_out.write_text(json.dumps(r,indent=2)+"\n");print(f"{r['case_count']} native training/resource cases passed; no stubs")
    else:print("Wrote static training witnesses; original code not executed")


if __name__=="__main__":main()
