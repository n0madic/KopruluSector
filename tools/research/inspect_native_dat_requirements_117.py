#!/usr/bin/env python3
"""Recover original general DAT requirements/tables/wrappers, statically by default.

Explicit isolated probes execute only reviewed original x86 instructions, with
private data fixtures and no helper stubs, Windows APIs or full-game execution.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import capstone
import pefile

TARGET_HASH="94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
ERROR=0x6BE654
TABLES={
 "upgrades":(0x501958,0x501CA0,0x524BD0,61,0x408280,0x408140),
 "research":(0x501CA0,0x501DE0,0x51F8C0,44,0x407F20,0x407DC0),
 "technology_use":(0x501DE0,0x502090,0x51F918,44,0x4080D0,0x407F90),
 "orders":(0x502090,0x502D00,0x51BB00,189,0x407D50,0x407BB0),
}
WITNESSES={
 "general_dat_evaluator":(0x407570,0x407A77),
 "orders_wrapper":(0x407BB0,0x407D49),"orders_offset_initializer":(0x407D50,0x407DB8),
 "research_wrapper":(0x407DC0,0x407F1D),"research_offset_initializer":(0x407F20,0x407F88),
 "technology_use_wrapper":(0x407F90,0x4080C4),"technology_use_offset_initializer":(0x4080D0,0x408138),
 "upgrade_wrapper":(0x408140,0x408277),"upgrade_offset_initializer":(0x408280,0x4082F6),
 "error_set":(0x4B38B0,0x4B38B7),"technology_available":(0x49A400,0x49A41F),
 "technology_researched":(0x49A420,0x49A43F),"upgrade_level":(0x49A470,0x49A496),
 "upgrade_max_level":(0x49A4A0,0x49A4C6),"research_in_progress_bit":(0x4A0560,0x4A058C),
 "upgrade_in_progress_bit":(0x4A0590,0x4A05B7),"producer_disabled":(0x407B50,0x407B80),
 "ghost_class_predicate":(0x407B80,0x407BA6),"queue_relative_word":(0x41B300,0x41B323),
 "protoss_constructing":(0x42F240,0x42F268),"addon_constructing":(0x435990,0x4359BE),
 "transport_predicate":(0x4381F0,0x43822D),"can_move_predicate":(0x427E60,0x427E8B),
 "right_click_action":(0x422D90,0x422DCF),"rally_building_type_predicate":(0x429250,0x4292C1),
 "can_attack_predicate":(0x41D9C0,0x41DA45),
}
NAMES={0xFF01:"OR",0xFF02:"current producer",0xFF03:"all+completed unit count",0xFF05:"landed",0xFF06:"lifted",0xFF07:"not busy",0xFF08:"not constructing target",0xFF09:"not researching",0xFF0A:"not upgrading",0xFF0D:"no linked Nydus",0xFF0F:"context technology or Lurker exception",0xFF11:"not burrowed unless has AI",0xFF12:"not landed status",0xFF13:"landed status",0xFF14:"can move",0xFF15:"can attack",0xFF16:"worker properties",0xFF17:"flying building properties",0xFF18:"transport",0xFF19:"powerup properties",0xFF1A:"subunit properties",0xFF1B:"mine byte nonzero",0xFF1C:"hero properties",0xFF1D:"right-click action nonzero",0xFF1E:"allow hallucinations",0xFF1F:"upgrade section selection",0xFF22:"disabled",0xFF23:"blank",0xFF24:"expansion",0xFF26:"burrowed"}
OPERANDS={0xFF02:1,0xFF03:1}


def parse_tables(pe):
    tables={}
    for name,(start,bound,offsets,capacity,initializer,wrapper) in TABLES.items():
        raw=pe.get_data(start-0x400000,bound-start);words=struct.unpack("<"+"H"*(len(raw)//2),raw)
        if words[0]!=0:raise ValueError("Unexpected table header")
        pos,records,ended=1,[],False
        while pos<len(words):
            rid=words[pos];pos+=1
            if rid==65535:ended=True;break
            first=pos;tokens=[]
            while pos<len(words) and words[pos]!=65535:
                address=pos;token=words[pos];pos+=1;args=list(words[pos:pos+OPERANDS.get(token,0)]);pos+=len(args)
                tokens.append({"word_offset":address,"token":token,"name":NAMES.get(token,"completed unit count or unsupported token fallback"),"operands":args})
            records.append({"record_id":rid,"word_offset":first,"words":list(words[first:pos]),"decoded_tokens":tokens,"real_id":rid<capacity});pos+=1
        if not ended:raise ValueError("Missing table sentinel")
        recipes=[]
        for j,r in enumerate(records):
            if not r["real_id"]:continue
            last=j+1
            while last<len(records) and not records[last]["real_id"]:last+=1
            recipes.append({"id":r["record_id"],"word_offset":r["word_offset"],"sections":records[j:last]})
        tables[name]={"preferred_va":hex(start),"end_exclusive_preferred_va":hex(start+2*pos),"offsets_preferred_va":hex(offsets),"capacity":capacity,"initializer_preferred_va":hex(initializer),"wrapper_preferred_va":hex(wrapper),
            "raw_bytes_sha256":hashlib.sha256(pe.get_data(start-0x400000,2*pos)).hexdigest(),"records":records,"recipe_count":len(recipes),"recipes":recipes}
    return tables


def probes(data,pe,tables):
    if platform.machine().lower() not in ("x86_64","amd64"):raise RuntimeError("Use reviewed x86_64 Rosetta runtime")
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP
    unit,target,program,stack,stop=0x800000,0x800400,0x801000,0x900000,0xA00000
    base,image=pe.OPTIONAL_HEADER.ImageBase,pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095
    allowed=list(WITNESSES.values());cases=[]
    def w32(u,a,v):u.mem_write(a,struct.pack("<I",v&0xFFFFFFFF))
    def w16(u,a,v):u.mem_write(a,struct.pack("<H",v&0xFFFF))
    def w8(u,a,v):u.mem_write(a,bytes([v&255]))
    def r32(u,a):return struct.unpack("<I",u.mem_read(a,4))[0]
    def new(producer=7,words=None):
        u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,size);u.mem_write(base,image);u.mem_map(unit,0x10000);u.mem_map(stack,0x2000);u.mem_map(stop,0x1000)
        for _,(_,_,off,capacity,_,_) in TABLES.items():u.mem_write(off,struct.pack("<"+"H"*capacity,*([65535]*capacity)))
        u.mem_write(0x52C0C4,struct.pack("<I",1)*(228*12));u.mem_write(0x529604,struct.pack("<I",1)*(228*12))
        u.mem_write(0x534104,bytes([1])*(24*12));u.mem_write(0x536330,bytes([1])*(20*12))
        u.mem_write(0x534224,bytes([1])*(24*12));u.mem_write(0x536420,bytes([1])*(20*12))
        u.mem_write(0x534590,bytes(46*12));u.mem_write(0x53660C,bytes(15*12))
        u.mem_write(0x534368,bytes([3])*(46*12));u.mem_write(0x536558,bytes([3])*(15*12))
        u.mem_write(0x536510,bytes(6*12));u.mem_write(0x5366C0,bytes(8*12))
        w32(u,ERROR,0);w32(u,0x5432FC,0);w8(u,0x536720,1)
        w16(u,unit+0x64,producer);w32(u,unit+0xDC,3);w8(u,unit+0x4D,0x9C);w8(u,unit+0xC8,44);w8(u,unit+0xC9,61)
        u.mem_write(unit+0x98,struct.pack("<5H",*([228]*5)));w32(u,0x521660+producer*4,0)
        w8(u,0x521E78+producer,1);w8(u,0x523FE0+producer,8);w8(u,0x5211D8+producer,0);w8(u,0x5213A8+producer,0)
        if words is not None:u.mem_write(program,struct.pack("<"+"H"*(len(words)+1),*words,65535))
        return u
    def call(u,start=0x407570,context=0,player=0,args=None):
        args=[unit,program,0] if args is None else args;reached,count=False,0
        def hook(m,a,n,_):
            nonlocal reached,count
            if a==stop:reached=True;m.emu_stop();return
            if not any(lo<=a and a+n<=hi for lo,hi in allowed):raise RuntimeError(f"Unreviewed native instruction {a:#x}")
            count+=1
        h=u.hook_add(UC_HOOK_CODE,hook);sp=stack+0x1800;u.mem_write(sp,struct.pack("<"+"I"*(len(args)+1),stop,*args))
        u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,context);u.reg_write(UC_X86_REG_EDX,player)
        u.emu_start(start,stop+1,count=50000);u.hook_del(h)
        if not reached:raise RuntimeError("No original return within50000 instructions")
        return {"return_u32":u.reg_read(UC_X86_REG_EAX),"error_u32":r32(u,ERROR)},count
    def observe(kind,inputs,ex,expected):
        got,count=ex
        if got!=expected:raise AssertionError((kind,inputs,got,expected))
        cases.append({"kind":kind,"fixture_inputs":inputs,"observed":got,"expected_from_static_contract":expected,"matched":True,"original_instructions":count,"helper_stubs":[]})
    def result(v=1,error=0):return {"return_u32":v&0xFFFFFFFF,"error_u32":error}
    def upgrade_level(u,ty,level):w8(u,(0x534590+ty) if ty<46 else (0x5365DE+ty),level)
    for name,table in tables.items():
        u=new();ex=call(u,int(table["initializer_preferred_va"],16),args=[])
        off=int(table["offsets_preferred_va"],16);capacity=table["capacity"]
        got=list(struct.unpack("<"+"H"*capacity,u.mem_read(off,capacity*2)));expected=[0]*capacity
        for r in table["records"]:
            if r["real_id"]:expected[r["record_id"]]=r["word_offset"]
        if got!=expected:raise AssertionError((name,"offset initializer mismatch"))
        cases.append({"kind":"original "+name+" offset initializer","fixture_inputs":{"initial_offsets":65535},"observed":{"offsets":got},"expected_from_static_contract":{"offsets":expected},"matched":True,"original_instructions":ex[1],"helper_stubs":[]})
        for recipe in table["recipes"]:
            first=recipe["sections"][0];tokens=first["decoded_tokens"]
            parents=[t["operands"][0] for t in tokens if t["token"]==0xFF02];producer=parents[0] if parents else 7
            for level in ((0,1,2) if name=="upgrades" else (0,)):
                u=new(producer);properties=0
                values=[t["token"] for t in tokens]
                if 0xFF16 in values:properties|=8
                if 0xFF17 in values:properties|=0x20
                if 0xFF19 in values:properties|=0x800
                if 0xFF1A in values:properties|=0x10
                w32(u,0x521660+producer*4,properties)
                if any(x in values for x in (0xFF06,0xFF12,0xFF14)):w32(u,unit+0xDC,1)
                if 0xFF26 in values:w32(u,unit+0xDC,0x11)
                if 0xFF1B in values:w8(u,unit+0xC0,1)
                if producer==42:upgrade_level(u,24,1)
                if name=="research":w8(u,(0x534224+recipe["id"]) if recipe["id"]<24 else (0x536408+recipe["id"]),0)
                if name=="upgrades":upgrade_level(u,recipe["id"],level)
                expected=result(0,23) if 0xFF23 in values else result()
                observe("shipped original "+name+" recipe wrapper",{"id":recipe["id"],"producer":producer,"current_upgrade_level":level,"permissive_counts_and_capabilities":True},
                    call(u,int(table["wrapper_preferred_va"],16),recipe["id"],args=[unit]),expected)

    tests=[
      ("current producer matches",[0xFF02,7],lambda u:None,result()),
      ("current producer mismatch",[0xFF02,64],lambda u:None,result(0,25)),
      ("current producer OR next matches",[0xFF02,64,0xFF01,0xFF02,7],lambda u:None,result()),
      ("ordinary completed missing",[111],lambda u:w32(u,0x52C0C4+111*48,0),result(-1,8)),
      ("ordinary completed cheat does not bypass",[111],lambda u:(w32(u,0x52C0C4+111*48,0),w32(u,0x5432FC,0x2000)),result(-1,8)),
      ("has-unit incomplete",[0xFF03,111],lambda u:w32(u,0x52C0C4+111*48,0),result()),
      ("landed success",[0xFF05],lambda u:None,result()),
      ("landed failure",[0xFF05],lambda u:w32(u,unit+0xDC,1),result(0,7)),
      ("lifted failure",[0xFF06],lambda u:None,result(0,6)),
      ("lifted success",[0xFF06],lambda u:w32(u,unit+0xDC,1),result()),
      ("busy empty",[0xFF07],lambda u:None,result()),
      ("busy queue",[0xFF07],lambda u:w16(u,unit+0x98,0),result(0,5)),
      ("not addon building",[0xFF08],lambda u:None,result()),
      ("addon building",[0xFF08],lambda u:(w8(u,unit+0xA6,0x25),w32(u,unit+0xEC,target)),result(0,5)),
      ("not researching",[0xFF09],lambda u:None,result()),
      ("researching",[0xFF09],lambda u:w8(u,unit+0xC8,0),result(0,5)),
      ("not upgrading",[0xFF0A],lambda u:None,result()),
      ("upgrading",[0xFF0A],lambda u:w8(u,unit+0xC9,0),result(0,5)),
      ("Nydus no link",[0xFF0D],lambda u:None,result()),
      ("Nydus linked",[0xFF0D],lambda u:w32(u,unit+0xD0,target),result(0,8)),
      ("context technology missing",[0xFF0F],lambda u:w8(u,0x534224,0),result(-1,8)),
      ("context technology cheat",[0xFF0F],lambda u:(w8(u,0x534224,0),w32(u,0x5432FC,0x1000)),result()),
      ("burrowed lacks AI",[0xFF11],lambda u:w32(u,unit+0xDC,0x13),result(0,5)),
      ("burrowed has AI exception",[0xFF11],lambda u:(w32(u,unit+0xDC,0x13),w32(u,unit+0x134,target)),result()),
      ("not burrowed success",[0xFF11],lambda u:None,result()),
      ("burrowed required fail",[0xFF26],lambda u:None,result(0,5)),
      ("burrowed required pass",[0xFF26],lambda u:w32(u,unit+0xDC,0x13),result()),
      ("not landed failure",[0xFF12],lambda u:None,result(0,19)),
      ("not landed success",[0xFF12],lambda u:w32(u,unit+0xDC,1),result()),
      ("landed only failure",[0xFF13],lambda u:w32(u,unit+0xDC,1),result(0,19)),
      ("landed only success",[0xFF13],lambda u:None,result()),
      ("move blocked grounded",[0xFF14],lambda u:None,result(0,19)),
      ("move supported action",[0xFF14],lambda u:w32(u,unit+0xDC,1),result()),
      ("attack ground weapon",[0xFF15],lambda u:None,result()),
      ("attack no weapons",[0xFF15],lambda u:(w8(u,0x5211D8+7,130),w8(u,0x5213A8+7,130)),result(0,19)),
      ("worker property absent",[0xFF16],lambda u:None,result(0,19)),
      ("worker property present",[0xFF16],lambda u:w32(u,0x521660+7*4,8),result()),
      ("flying building property",[0xFF17],lambda u:w32(u,0x521660+7*4,32),result()),
      ("transport zero capacity",[0xFF18],lambda u:w8(u,0x523FE0+7,0),result(-1,8)),
      ("transport has capacity",[0xFF18],lambda u:None,result()),
      ("powerup property",[0xFF19],lambda u:w32(u,0x521660+7*4,0x800),result()),
      ("subunit property",[0xFF1A],lambda u:w32(u,0x521660+7*4,0x10),result()),
      ("mine byte zero",[0xFF1B],lambda u:None,result(0,8)),
      ("mine byte nonzero",[0xFF1B],lambda u:w8(u,unit+0xC0,1),result()),
      ("hero property missing",[0xFF1C],lambda u:None,result(-1,8)),
      ("hero property present",[0xFF1C],lambda u:w32(u,0x521660+7*4,0x40),result()),
      ("hold position action0",[0xFF1D],lambda u:(w8(u,0x521E78+7,0),w32(u,unit+0xDC,1)),result(0,8)),
      ("hold position action1",[0xFF1D],lambda u:None,result()),
      ("hallucination implicit reject",[],lambda u:w32(u,unit+0xDC,0x40000003),result(0,22)),
      ("hallucination allow token",[0xFF1E],lambda u:w32(u,unit+0xDC,0x40000003),result()),
      ("disabled",[0xFF22],lambda u:None,result(-1,21)),
      ("blank",[0xFF23],lambda u:None,result(0,23)),
      ("expansion false",[0xFF24],lambda u:w8(u,0x536720,0),result(0,26)),
      ("expansion true",[0xFF24],lambda u:None,result()),
    ]
    for name,words,patch,expected in tests:
        u=new(words=words);patch(u);observe("general token "+name,{"words":words,"context":0},call(u),expected)
    u=new(103,[0xFF0F]);w8(u,0x534224+11,0);observe("Lurker context11 research exception",{"producer":103,"context":11,"researched":0},call(u,context=11),result())
    # Current level selects a later marker across intermediate0xffff terminators.
    words=[0xFF1F,111,65535,0xFF20,112,65535,0xFF21,113]
    for level,required in [(0,111),(1,112),(2,113),(3,111)]:
        for exists in (False,True):
            u=new(words=words);upgrade_level(u,0,level)
            for ty in (111,112,113):w32(u,0x52C0C4+ty*48,int(exists and ty==required))
            observe("upgrade level marker selection",{"current_level":level,"required_selected":required,"exists":exists,"words":words},call(u),result() if exists else result(-1,8))
    for name,(_,_,off,capacity,_,wrapper) in TABLES.items():
        for test in ("ID bound","owner mismatch","incomplete","disabled","offset missing"):
            u=new();rid=capacity if test=="ID bound" else 0
            if test=="owner mismatch":w8(u,unit+0x4C,1)
            if test=="incomplete":w32(u,unit+0xDC,0)
            if test=="disabled":w8(u,unit+0x117,1)
            if test=="offset missing":
                w16(u,off,0)
                if name=="research":w8(u,0x534224,0)
            err={"ID bound":{"orders":15,"upgrades":18,"research":17,"technology_use":17}[name],"owner mismatch":1,"incomplete":20,"disabled":10,"offset missing":23}[test]
            observe(name+" wrapper "+test,{"context":rid},call(u,wrapper,rid,args=[unit]),result(0,err))
    for ty in (0,7,8,23,24,35,43):
        for player in (0,7):
            for active in (False,True):
                u=new();mask=1<<(ty&7);w8(u,0x536510+player*6+(ty>>3),mask if active else 0)
                observe("research in-progress bit indexing",{"technology":ty,"player":player,"active":active},call(u,0x4A0560,ty,player,args=[]),result(mask if active else 0))
    for ty in (0,7,8,45,46,60):
        for player in (0,7):
            for active in (False,True):
                u=new();mask=1<<(ty&7);w8(u,0x5366C0+player*8+(ty>>3),mask if active else 0)
                observe("upgrade in-progress bit indexing",{"upgrade":ty,"player":player,"active":active},call(u,0x4A0590,ty,player,args=[]),result(mask if active else 0))
    for name,patch,expected in [
        ("research unavailable",lambda u:(w8(u,0x534224,0),w8(u,0x534104,0)),result(0,2)),
        ("research in progress",lambda u:(w8(u,0x534224,0),w8(u,0x536510,1)),result(0,12)),
        ("research already completed",lambda u:None,result(0,13)),
        ("research cheat causes completed branch",lambda u:(w8(u,0x534224,0),w32(u,0x5432FC,0x1000)),result(0,13)),
        ("technology use unavailable",lambda u:w8(u,0x534104,0),result(0,2)),
        ("technology use hallucinated",lambda u:w32(u,unit+0xDC,0x40000003),result(0,22)),
        ("upgrade maximum reached",lambda u:upgrade_level(u,0,3),result(0,14)),
        ("upgrade in progress",lambda u:w8(u,0x5366C0,1),result(0,12)),
    ]:
        u=new();which="research" if name.startswith("research") else "upgrades" if name.startswith("upgrade") else "technology_use"
        _,_,off,_,_,wrapper=TABLES[which];w16(u,off,1);w16(u,TABLES[which][0]+2,65535);patch(u)
        observe("wrapper "+name,{"context":0,"synthetic_empty_requirements":True},call(u,wrapper,0,args=[unit]),expected)
    for order_id,producer,status,primary,expected in [
        (39,7,0,0x9C,result()),(40,7,0,0x9C,result()),(38,7,0,0x9C,result(0,20)),
        (118,7,0x11,0x9C,result()),(0,7,0x11,0x9C,result(0,11)),(0,103,0x11,0x9C,result()),
        (1,7,3,0x21,result()),(0,7,3,0x21,result(0,8)),
        (0,1,3,0x81,result(0,8)),(0,68,3,0x6A,result(0,8)),(0,63,3,0x6A,result(0,8)),
    ]:
        u=new(producer);w16(u,0x51BB00+order_id*2,1);w16(u,0x502092,65535);w32(u,unit+0xDC,status);w8(u,unit+0x4D,primary)
        observe("order wrapper exception or busy restriction",{"order":order_id,"producer":producer,"status":status,"primary":primary,"synthetic_empty_requirements":True},call(u,0x407BB0,order_id,args=[unit]),expected)
    return {"format_version":1,"target_sha256":hashlib.sha256(data).hexdigest(),"probe_script_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),"original_code_executed":True,"complete_game_executed":False,"helper_stubs":[],
      "runtime_versions":{"python":platform.python_version(),"unicorn":unicorn.__version__,"capstone":capstone.__version__,"pefile":pefile.__version__,"architecture":platform.machine(),"system":platform.system()},"allowlisted_preferred_va_ranges":[[hex(a),hex(b)] for a,b in allowed],"all_matched":True,"case_count":len(cases),"cases":cases}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--exe",type=Path,required=True);p.add_argument("--out",type=Path,required=True);p.add_argument("--execute-isolated-original-code",action="store_true");p.add_argument("--probe-out",type=Path);a=p.parse_args()
    if a.execute_isolated_original_code and a.probe_out is None:p.error("--probe-out required")
    data=a.exe.read_bytes()
    if hashlib.sha256(data).hexdigest()!=TARGET_HASH:p.error("Wrong EXE hash")
    pe=pefile.PE(data=data);tables=parse_tables(pe);c=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    targets=struct.unpack("<37I",pe.get_data(0x407A78-0x400000,148))
    out={"format_version":1,"target_sha256":TARGET_HASH,"inspector_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),"original_code_executed":False,"complete_game_executed":False,"tables":tables,
         "general_switch":[{"token":0xFF02+i,"name":NAMES.get(0xFF02+i,"completed unit count fallback"),"target_preferred_va":hex(v),"dedicated":v!=0x4078FA} for i,v in enumerate(targets)],"witnesses":{}}
    for name,(start,end) in WITNESSES.items():
        raw=pe.get_data(start-0x400000,end-start);out["witnesses"][name]={"start_preferred_va":hex(start),"end_exclusive_preferred_va":hex(end),"bytes_sha256":hashlib.sha256(raw).hexdigest(),"instructions":[{"address":hex(i.address),"bytes":i.bytes.hex(),"mnemonic":i.mnemonic,"operands":i.op_str} for i in c.disasm(raw,start)]}
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(out,indent=2)+"\n")
    if a.execute_isolated_original_code:
        r=probes(data,pe,tables);a.probe_out.parent.mkdir(parents=True,exist_ok=True);a.probe_out.write_text(json.dumps(r,indent=2)+"\n");print(f"{r['case_count']} original general-DAT cases passed; no stubs")
    else:print("Decoded original tables; original code not executed")


if __name__=="__main__":main()
