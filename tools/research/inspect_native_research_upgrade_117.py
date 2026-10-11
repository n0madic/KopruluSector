#!/usr/bin/env python3
"""Inspect research/upgrade startup, refunds, progress bits and timer boundaries.

Static by default. Explicit probes execute reviewed original functions/prefixes
in private Unicorn memory. No helper stubs or full-game/API execution.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import capstone
import pefile
import inspect_native_training_117 as training
import inspect_native_dat_requirements_117 as requirements

TARGET_HASH=training.TARGET_HASH
PLAYER_BASE,STRIDE=training.PLAYER_BASE,training.STRIDE
WITNESSES={
 "start_research":(0x43AF40,0x43AFA9),"start_upgrade":(0x43AFB0,0x43B055),
 "sprite_animation_dispatch":(0x417220,0x417254),
 "research_full_refund":(0x43AB80,0x43ABE0),"upgrade_full_refund":(0x43ABE0,0x43AC37),
 "research_partial_refund":(0x43ADF0,0x43AE5E),"upgrade_partial_refund":(0x43AE60,0x43AE9D),
 "upgrade_full_refund_cost":(0x43B210,0x43B26E),"upgrade_partial_refund_cost":(0x43B2D0,0x43B346),
 "research_clear_progress":(0x4A0500,0x4A052B),"upgrade_clear_progress":(0x4A0530,0x4A0557),
 "research_set_progress":(0x4A05C0,0x4A05E9),"upgrade_set_progress":(0x4A05F0,0x4A0615),
 "research_state_write":(0x49A300,0x49A33F),"upgrade_level_write":(0x49A370,0x49A3B3),
 "research_order_handler":(0x43AC40,0x43ACDA),"upgrade_order_handler":(0x43ACE0,0x43ADEF),
 "research_consumer":(0x485CA0,0x485D21),"upgrade_consumer":(0x485D30,0x485DB1),
}


def script_hashes():
    return {Path(m.__file__).name:hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in (training,requirements)}


def probes(data,pe):
    if platform.machine().lower() not in ("x86_64","amd64"):raise RuntimeError("Use reviewed x86_64 Rosetta runtime")
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP
    unit,ai,sprite,stack,stop=0x800000,0x800400,0x800800,0x900000,0xA00000
    base,image=pe.OPTIONAL_HEADER.ImageBase,pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095
    allowed=list(WITNESSES.values())+list(training.WITNESSES.values())+list(training.unit_reqs.WITNESSES.values())+list(requirements.WITNESSES.values());cases=[]
    def w32(u,a,v):u.mem_write(a,struct.pack("<I",v&0xFFFFFFFF))
    def w16(u,a,v):u.mem_write(a,struct.pack("<H",v&65535))
    def w8(u,a,v):u.mem_write(a,bytes([v&255]))
    def r32(u,a):return struct.unpack("<I",u.mem_read(a,4))[0]
    def r16(u,a):return struct.unpack("<H",u.mem_read(a,2))[0]
    def r8(u,a):return u.mem_read(a,1)[0]
    def level_addr(player,ident):return (0x534590+player*46+ident) if ident<46 else (0x5365DE+player*15+ident)
    def researched_addr(player,ident):return (0x534224+player*24+ident) if ident<24 else (0x536408+player*20+ident)
    def bit_addr(kind,player,ident):return (0x536510+player*6 if kind=="research" else 0x5366C0+player*8)+(ident>>3)
    def new(kind="research",ident=0,level=0,cost=(50,25),stock=(300,100),timer_base=100,timer_factor=20):
        u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,size);u.mem_write(base,image);u.mem_map(unit,0x10000);u.mem_map(stack,0x2000);u.mem_map(stop,0x1000)
        u.mem_write(PLAYER_BASE,bytes(STRIDE*8));u.mem_write(0x52C0C4,struct.pack('<'+'I'*(228*12),*([1]*(228*12))))
        u.mem_write(0x536510,bytes(12*6));u.mem_write(0x5366C0,bytes(12*8));w32(u,0x5432FC,0);w32(u,0x508D50,0);w32(u,0x67CC90,0);w8(u,0x536720,1)
        w16(u,unit+0x64,112 if kind=="research" else 122);w8(u,unit+0x4C,7);w8(u,unit+0x4D,1);w32(u,unit+0xDC,3);w32(u,unit+0xC,sprite);w32(u,sprite+0x1C,0)
        w8(u,unit+0xC8,44);w8(u,unit+0xC9,61);w8(u,unit+0xCD,0);w32(u,unit+0x134,ai);w8(u,ai+8,3)
        u.mem_write(unit+0x98,struct.pack("<5H",*([228]*5)))
        w32(u,0x5263D0+28,stock[0]);w32(u,0x526400+28,stock[1])
        w16(u,0x51F760+ident*2,cost[0]);w16(u,0x51F7B8+ident*2,cost[1]);w16(u,0x51F810+ident*2,timer_base)
        for a,v in ((0x5248D0,cost[0]),(0x524950,3),(0x5249D0,cost[1]),(0x524A50,7),(0x524AD0,timer_base),(0x524B50,timer_factor)):w16(u,a+ident*2,v)
        w8(u,level_addr(7,ident),level);w8(u,researched_addr(7,ident),0)
        w8(u,(0x534104+7*24+ident) if ident<24 else (0x536318+7*20+ident),1)
        w8(u,(0x534368+7*46+ident) if ident<46 else (0x53652A+7*15+ident),3)
        u.mem_write(0x51F8C0,struct.pack("<44H",*([65535]*44)));u.mem_write(0x524BD0,struct.pack("<61H",*([65535]*61)))
        p=PLAYER_BASE+7*STRIDE
        for j,v in enumerate((*cost,0)):w32(u,p+j*4,v);w32(u,p+0xC+j*4,1000)
        w32(u,p+0x18,90|((6 if kind=="research" else 5)<<8)|(ident<<16));w32(u,p+0x1C,0x800C00);w8(u,p+0x210,1)
        return u
    def call(u,start,ecx=unit,edx=0,args=(),checkpoint=None):
        reached,count,trace=False,0,[]
        def hook(m,a,n,_):
            nonlocal reached,count
            if a==stop or a==checkpoint:reached=True;m.emu_stop();return
            if not any(lo<=a and a+n<=hi for lo,hi in allowed):raise RuntimeError(f"Unreviewed original instruction {a:#x}")
            count+=1
            if a in (0x43AF40,0x43AFB0,0x408650,0x417220,0x482BC0,0x482C70,0x4A05C0,0x4A05F0):trace.append(hex(a))
        h=u.hook_add(UC_HOOK_CODE,hook);sp=stack+0x1800;u.mem_write(sp,struct.pack("<"+"I"*(len(args)+1),stop,*args))
        u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
        u.emu_start(start,stop+1,count=100000);u.hook_del(h)
        if not reached:raise RuntimeError("No return/checkpoint within 100000 instructions")
        return u.reg_read(UC_X86_REG_EAX),count,trace
    def observe(kind,inputs,ex,got,expected,scope="complete reviewed function for fixture path"):
        if got!=expected:raise AssertionError((kind,inputs,got,expected))
        cases.append({"kind":kind,"fixture_inputs":inputs,"observed":got,"expected_from_static_contract":expected,"matched":True,"original_instruction_count":ex[1],"native_call_trace":ex[2],"execution_scope":scope,"helper_stubs":[]})
    def state(u,kind,ident):return {"stocks":[r32(u,0x5263D0+28),r32(u,0x526400+28)],"timer":r16(u,unit+0xC6),"technology":r8(u,unit+0xC8),"upgrade":r8(u,unit+0xC9),"next_level":r8(u,unit+0xCD),"progress_byte":r8(u,bit_addr(kind,7,ident))}
    for kind,ids in (("research",(0,7,8,23,24,43)),("upgrade",(0,7,8,45,46,60))):
        for player in (0,7):
            for ident in ids:
                for initial in (0x55,0xAA):
                    for set_bit in (False,True):
                        u=new(kind,ident);a=bit_addr(kind,player,ident);w8(u,a,initial)
                        addr=(0x4A05C0 if set_bit else 0x4A0500) if kind=="research" else (0x4A05F0 if set_bit else 0x4A0530)
                        ex=call(u,addr,ident,player);mask=1<<(ident&7)
                        observe("progress bit set/clear preserves other bits",{"kind":kind,"player":player,"id":ident,"initial":initial,"set":set_bit},ex,r8(u,a),(initial|mask) if set_bit else (initial&~mask))
    for kind,ids in (("research",(0,7,23,24,43)),("upgrade",(0,7,45,46,60))):
        for ident in ids:
            for level in ((0,) if kind=="research" else (0,2,255)):
                u=new(kind,ident,level,cost=(10,5),stock=(10000,10000),timer_base=65535,timer_factor=3)
                ex=call(u,0x43AF40 if kind=="research" else 0x43AFB0,args=[ident])
                price=[10,5] if kind=="research" else [10+level*3,5+level*7]
                observe("actual research/upgrade start payment timer and bit",{"kind":kind,"id":ident,"current_level":level,"timer_base":65535,"timer_factor":3,"sprite_images":[]},ex,
                    {"return":ex[0],**state(u,kind,ident)}, {"return":1,"stocks":[10000-price[0],10000-price[1]],"timer":65535 if kind=="research" else (65535+level*3)&65535,"technology":ident if kind=="research" else 44,"upgrade":ident if kind=="upgrade" else 61,"next_level":(level+1)&255 if kind=="upgrade" else 0,"progress_byte":1<<(ident&7)})
        for argument in ((44,255,256) if kind=="research" else (61,255,256)):
            u=new(kind);ex=call(u,0x43AF40 if kind=="research" else 0x43AFB0,args=[argument]);accepted=(argument&255)==0
            observe("start parses low stack byte before capacity check",{"kind":kind,"argument":argument},ex,{"return":ex[0],"stocks":state(u,kind,0)["stocks"]},{"return":int(accepted),"stocks":[250,75] if accepted else [300,100]})
        for omitted in ("incomplete","disabled","already known"):
            u=new(kind)
            if omitted=="incomplete":w32(u,unit+0xDC,0)
            if omitted=="disabled":w8(u,unit+0x117,1)
            if omitted=="already known":w8(u,researched_addr(7,0),1) if kind=="research" else w8(u,level_addr(7,0),3)
            ex=call(u,0x43AF40 if kind=="research" else 0x43AFB0,args=[0]);observe("start helper omits caller legality",{"kind":kind,"condition":omitted,"sprite_images":[]},ex,ex[0],1)
        for stock in ((49,25),(50,24),(0xFFFFFFFF,25)):
            u=new(kind,stock=stock);ex=call(u,0x43AF40 if kind=="research" else 0x43AFB0,args=[0])
            observe("start rejects current stock shortage before unit state changes",{"kind":kind,"stock":stock,"viewer":0,"player":7},ex,
                {"return":ex[0],**state(u,kind,0)}, {"return":0,"stocks":list(stock),"timer":0,"technology":44,"upgrade":61,"next_level":0,"progress_byte":0})
    for kind,ids in (("research",(0,23,24,43)),("upgrade",(0,45,46,60))):
        for player in (0,7):
            for ident in ids:
                for expansion in (False,True):
                    u=new(kind,ident);w8(u,0x536720,int(expansion));a=researched_addr(player,ident) if kind=="research" else level_addr(player,ident);w8(u,a,9)
                    ex=call(u,0x49A300 if kind=="research" else 0x49A370,player,ident,[257])
                    writable=ident<(24 if kind=="research" else 46) or expansion
                    observe("state writer expansion gate and low value byte",{"kind":kind,"player":player,"id":ident,"expansion":expansion,"value_argument":257},ex,r8(u,a),1 if writable else 9)
    for kind in ("research","upgrade"):
        for full in (False,True):
            for ident,cost,stock in [(0,(51,27),(300,100)),(24 if kind=="research" else 46,(65535,65535),(0xFFFFFFF0,0xFFFFFFF0))]:
                u=new(kind,ident,level=2,cost=cost,stock=stock);w16(u,unit+0xC6,120);w8(u,unit+(0xC8 if kind=="research" else 0xC9),ident);w8(u,unit+0xCD,3);w8(u,bit_addr(kind,7,ident),255)
                w32(u,0x521660+r16(u,unit+0x64)*4,1)
                addr=(0x43AB80 if full else 0x43ADF0) if kind=="research" else (0x43ABE0 if full else 0x43AE60)
                ex=call(u,addr)
                price=cost if kind=="research" else ((cost[0]+6)&65535,(cost[1]+14)&65535)
                refund=price if full else tuple(v*3//4 for v in price)
                observe("full or three-quarter refund actual clears",{"kind":kind,"full":full,"id":ident,"cost":cost,"current_upgrade_level":2,"stock":stock,"sprite_images":[]},ex,state(u,kind,ident),
                    {"stocks":[(v+c)&0xFFFFFFFF for v,c in zip(stock,refund)],"timer":0,"technology":44,"upgrade":61,"next_level":3 if kind=="research" else 0,"progress_byte":255&~(1<<(ident&7))})
        u=new(kind);w16(u,unit+0xC6,123);w8(u,unit+0xCD,2);w8(u,bit_addr(kind,7,0),255)
        ex=call(u,0x43AB80 if kind=="research" else 0x43ABE0)
        observe("sentinel cancellation does nothing",{"kind":kind},ex,state(u,kind,0),{"stocks":[300,100],"timer":123,"technology":44,"upgrade":61,"next_level":2,"progress_byte":255})
    for full in (False,True):
        u=new("upgrade");w8(u,unit+0xC9,0);w16(u,unit+0xC6,123);w8(u,unit+0xCD,1);w8(u,bit_addr("upgrade",7,0),1);w32(u,0x521660+122*4,0)
        ex=call(u,0x43ABE0 if full else 0x43AE60)
        observe("only full upgrade refund requires DAT building property",{"full":full,"unit_DAT_building":False},ex,state(u,"upgrade",0),
            {"stocks":[300,100] if full else [337,118],"timer":123 if full else 0,"technology":44,"upgrade":0 if full else 61,"next_level":1 if full else 0,"progress_byte":1 if full else 0})
        u=new("upgrade");w32(u,0x521660+122*4,1);first=call(u,0x43AFB0,args=[0]);w8(u,level_addr(7,0),2)
        ex=call(u,0x43ABE0 if full else 0x43AE60);ex=(ex[0],ex[1]+first[1],first[2]+ex[2])
        observe("upgrade refund recomputes current level instead of saved paid price",{"full":full,"start_level":0,"fixture_level_changed_before_refund":2},ex,state(u,"upgrade",0),
            {"stocks":[306,114] if full else [292,104],"timer":0,"technology":44,"upgrade":61,"next_level":0,"progress_byte":0},"two complete helpers with explicit fixture state change between calls")
    for kind in ("research","upgrade"):
        for timer,known,cheat in ((2,False,False),(1,False,False),(0,False,False),(65535,False,False),(10,True,False),(10,False,True)):
            u=new(kind);w16(u,unit+0xC6,timer);w8(u,unit+(0xC8 if kind=="research" else 0xC9),0);w8(u,unit+0xCD,1);w32(u,0x5432FC,2 if cheat else 0)
            if known:w8(u,researched_addr(7,0) if kind=="research" else level_addr(7,0),1)
            completion=timer==0 or known or cheat;cp=(0x43AC7D if kind=="research" else 0x43AD36) if completion else None
            ex=call(u,0x43AC40 if kind=="research" else 0x43ACE0,checkpoint=cp)
            observe("timer checks old zero and known/cheat completion",{"kind":kind,"old_timer":timer,"already_known":known,"cheat_2":cheat},ex,
                {"timer":r16(u,unit+0xC6)}, {"timer":(timer-1)&65535},"prefix stops at completion branch before sound/state/UI/order helpers" if completion else "complete waiting order handler; no completion branch")
    for kind in ("research","upgrade"):
        u=new(kind);ex=call(u,0x485CA0 if kind=="research" else 0x485D30,7,unit,checkpoint=0x485D0A if kind=="research" else 0x485D9A);p=PLAYER_BASE+7*STRIDE
        observe("AI consumer successful original recipe and actual start prefix",{"kind":kind,"id":0,"producer_type":112 if kind=="research" else 122,"original_recipe":True,"sprite_images":[]},ex,
            {**state(u,kind,0),"heap_count":r8(u,p+0x210),"demand":[r32(u,p+j*4) for j in range(3)]},
            {"stocks":[250,75],"timer":100,"technology":0 if kind=="research" else 44,"upgrade":0 if kind=="upgrade" else 61,"next_level":1 if kind=="upgrade" else 0,"progress_byte":1,"heap_count":1,"demand":[50,25,0]},"consumer prefix after actual start, before primary-order issue and successful request pop")
        for failure in ("stock","available budget","disabled","wrong producer"):
            u=new(kind);p=PLAYER_BASE+7*STRIDE
            if failure=="stock":w32(u,0x5263D0+28,49)
            if failure=="available budget":w32(u,p+0xC,49)
            if failure=="disabled":w8(u,unit+0x117,1)
            if failure=="wrong producer":w16(u,unit+0x64,0)
            ex=call(u,0x485CA0 if kind=="research" else 0x485D30,7,unit)
            observe("AI research/upgrade consumer retains on selected failures",{"kind":kind,"failure":failure},ex,
                {"heap_count":r8(u,p+0x210),"demand":[r32(u,p+j*4) for j in range(3)],"stocks":state(u,kind,0)["stocks"],"timer":r16(u,unit+0xC6)},
                {"heap_count":1,"demand":[50,25,0],"stocks":[49,100] if failure=="stock" else [300,100],"timer":0})
    return {"format_version":1,"target_sha256":TARGET_HASH,"probe_script_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),"dependency_script_sha256":script_hashes(),
      "runtime_versions":{"python":platform.python_version(),"unicorn":unicorn.__version__,"capstone":capstone.__version__,"pefile":pefile.__version__,"architecture":platform.machine(),"system":platform.system()},
      "original_code_executed":True,"complete_game_executed":False,"helper_stubs":[],"allowlisted_preferred_va_ranges":[[hex(a),hex(b)] for a,b in allowed],"all_matched":True,"case_count":len(cases),"cases":cases}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--exe",type=Path,required=True);p.add_argument("--out",type=Path,required=True);p.add_argument("--execute-isolated-original-code",action="store_true");p.add_argument("--probe-out",type=Path);a=p.parse_args()
    if a.execute_isolated_original_code and a.probe_out is None:p.error("--probe-out required")
    data=a.exe.read_bytes()
    if hashlib.sha256(data).hexdigest()!=TARGET_HASH:p.error("Wrong EXE hash")
    pe=pefile.PE(data=data);c=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    report={"format_version":1,"target_sha256":TARGET_HASH,"inspector_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),"dependency_script_sha256":script_hashes(),"original_code_executed":False,"complete_game_executed":False,"witnesses":{}}
    for name,(start,end) in WITNESSES.items():
        raw=pe.get_data(start-0x400000,end-start);report["witnesses"][name]={"start_preferred_va":hex(start),"end_exclusive_preferred_va":hex(end),"bytes_sha256":hashlib.sha256(raw).hexdigest(),"instructions":[{"address":hex(i.address),"bytes":i.bytes.hex(),"mnemonic":i.mnemonic,"operands":i.op_str} for i in c.disasm(raw,start)]}
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(report,indent=2)+"\n")
    if a.execute_isolated_original_code:
        result=probes(data,pe);a.probe_out.parent.mkdir(parents=True,exist_ok=True);a.probe_out.write_text(json.dumps(result,indent=2)+"\n");print(f"{result['case_count']} native research/upgrade cases passed; no stubs")
    else:print("Wrote static research/upgrade witnesses; original code not executed")


if __name__=="__main__":main()
