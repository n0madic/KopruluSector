#!/usr/bin/env python3
"""Decode exact native 1.17 unit production requirements and probe original gate.

Static by default. Explicit isolated execution uses no helper stubs; it never
enters the full game, WinMain, Windows APIs or host file/network services.
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
REQ_TABLE,REQ_OFFSETS,ERROR=0x50DA88,0x523A78,0x6BE654
WITNESSES={
    "unit_requirements_gate_and_interpreter":(0x4B32A0,0x4B37DA),
    "unit_requirement_offsets_initialize":(0x4B3230,0x4B329E),
    "producer_disabled_predicate":(0x407B50,0x407B80),
    "producer_busy_predicate":(0x407B10,0x407B43),
    "circular_queue_relative_slot":(0x41B300,0x41B323),
    "protoss_constructing_target":(0x42F240,0x42F268),
    "addon_constructing_target":(0x435990,0x4359BE),
    "carrier_type_predicate":(0x41B380,0x41B397),
    "reaver_type_predicate":(0x41B3D0,0x41B3E7),
    "queued_fighters_count":(0x41CC50,0x41CC7C),
    "fighter_capacity":(0x41CBA0,0x41CC14),
    "technology_researched":(0x49A420,0x49A43F),
    "upgrade_level":(0x49A470,0x49A496),
}
NAMES={0xFF01:"OR continuation",0xFF02:"current producer type",0xFF03:"has unit, all+completed count",0xFF04:"attached addon type",0xFF05:"status0x2 set",0xFF07:"not busy",0xFF08:"not constructing target",0xFF09:"technology byte0x2c",0xFF0A:"upgrade byte0x3d",0xFF0B:"not SCV order0x21",0xFF0C:"no addon pointer",0xFF0E:"fighter capacity",0xFF10:"no linked missile pointer",0xFF11:"status0x10 clear",0xFF22:"disabled",0xFF23:"blank",0xFF24:"expansion byte nonzero",0xFF25:"technology researched",0xFF26:"status0x10 set",0xFFFF:"record terminator"}
OPERANDS={0xFF02:1,0xFF03:1,0xFF04:1,0xFF25:1}


def parse_unit_table(pe):
    words=struct.unpack("<3000H",pe.get_data(REQ_TABLE-0x400000,6000))
    if words[0]!=0:raise ValueError("Unexpected table header")
    pos,entries=1,[]
    while pos<len(words):
        uid=words[pos];pos+=1
        if uid==0xFFFF:break
        if uid>=228:raise ValueError("Unexpected recipe ID")
        start=pos;tokens=[]
        while words[pos]!=0xFFFF:
            pc=pos;value=words[pos];pos+=1
            operands=list(words[pos:pos+OPERANDS.get(value,0)]);pos+=len(operands)
            tokens.append({"word_offset":pc,"preferred_va":hex(REQ_TABLE+pc*2),"token":value,"name":NAMES.get(value,"completed unit count"),"operands":operands})
        entries.append({"unit_id":uid,"word_offset":start,"words":list(words[start:pos]),"decoded_tokens":tokens});pos+=1
    if pos>=len(words):raise ValueError("Missing table sentinel")
    return entries,pos


def probes(data,pe,entries):
    if platform.machine().lower() not in ("x86_64","amd64"):raise RuntimeError("Use reviewed x86_64 Rosetta runtime")
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP
    unit,target,stack,stop=0x800000,0x800400,0x900000,0xA00000
    base,image=pe.OPTIONAL_HEADER.ImageBase,pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095
    allowed=list(WITNESSES.values());cases=[]
    def w32(u,a,v):u.mem_write(a,struct.pack("<I",v&0xFFFFFFFF))
    def w16(u,a,v):u.mem_write(a,struct.pack("<H",v&0xFFFF))
    def w8(u,a,v):u.mem_write(a,bytes([v&255]))
    def r32(u,a):return struct.unpack("<I",u.mem_read(a,4))[0]
    def new(producer=7,request=106,program=None):
        u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(base,size);u.mem_write(base,image)
        u.mem_map(unit,0x10000);u.mem_map(stack,0x2000);u.mem_map(stop,0x1000)
        u.mem_write(REQ_OFFSETS,struct.pack("<228H",*([65535]*228)))
        u.mem_write(0x52655C,bytes([1])*(228*12));u.mem_write(0x52C0C4,struct.pack("<I",1)*(228*12))
        u.mem_write(0x529604,struct.pack("<I",1)*(228*12))
        u.mem_write(0x534224,bytes([1])*(24*12));u.mem_write(0x536420,bytes([1])*(20*12))
        u.mem_write(0x534590,bytes([1])*(46*12));u.mem_write(0x53660C,bytes([1])*(15*12))
        w16(u,unit+0x64,producer);w32(u,unit+0xDC,3);w8(u,unit+0x4D,0x9C)
        w8(u,unit+0xC8,44);w8(u,unit+0xC9,61);w32(u,0x5432FC,0);w8(u,0x536720,1)
        u.mem_write(unit+0x98,struct.pack("<5H",*([228]*5)));w32(u,0x521660+producer*4,0)
        if program is not None:
            w16(u,REQ_OFFSETS+request*2,1)
            u.mem_write(REQ_TABLE+2,struct.pack("<"+"H"*(len(program)+1),*program,65535))
        return u
    def call(u,request=106,player=0,start=0x4B32A0):
        reached,count=False,0
        def hook(m,a,n,_):
            nonlocal reached,count
            if a==stop:reached=True;m.emu_stop();return
            if not any(lo<=a and a+n<=hi for lo,hi in allowed):raise RuntimeError(f"Unreviewed native instruction {a:#x}")
            count+=1
        h=u.hook_add(UC_HOOK_CODE,hook);sp=stack+0x1800
        u.mem_write(sp,struct.pack("<II",stop,unit));u.reg_write(UC_X86_REG_ESP,sp)
        u.reg_write(UC_X86_REG_ECX,request);u.reg_write(UC_X86_REG_EDX,player)
        u.emu_start(start,stop+1,count=50000);u.hook_del(h)
        if not reached:raise RuntimeError("No native return within50000 instructions")
        return {"return_u32":u.reg_read(UC_X86_REG_EAX),"error_u32":r32(u,ERROR)},count
    def observe(kind,inputs,execution,expected):
        got,count=execution
        if got!=expected:raise AssertionError((kind,inputs,got,expected))
        cases.append({"kind":kind,"fixture_inputs":inputs,"observed":got,"expected_from_static_contract":expected,"matched":True,"original_instructions":count,"helper_stubs":[]})
    def result(value,error=0):return {"return_u32":value&0xFFFFFFFF,"error_u32":error}

    u=new();ex=call(u,start=0x4B3230);offsets=list(struct.unpack("<228H",u.mem_read(REQ_OFFSETS,456)))
    expected=[0]*228
    for e in entries:expected[e["unit_id"]]=e["word_offset"]
    if offsets!=expected:raise AssertionError("Original lazy offset initialization differs from packed-table decode")
    cases.append({"kind":"original packed table lazy initialization","fixture_inputs":{"initial_offsets":65535},"observed":{"offsets_228":offsets},"expected_from_static_contract":{"offsets_228":expected},"matched":True,"original_instructions":ex[1],"helper_stubs":[]})
    for kind,patch,expected in [
        ("out of range ID",lambda u:None,result(0,16)),
        ("wrong producer owner",lambda u:w8(u,unit+0x4C,1),result(0,1)),
        ("producer incomplete",lambda u:w32(u,unit+0xDC,2),result(0,20)),
        ("producer status disabled",lambda u:w32(u,unit+0xDC,0x403),result(0,10)),
        ("producer timer117",lambda u:w8(u,unit+0x117,1),result(0,10)),
        ("producer timer119",lambda u:w8(u,unit+0x119,1),result(0,10)),
        ("producer timer124",lambda u:w8(u,unit+0x124,1),result(0,10)),
        ("unit unavailable",lambda u:w8(u,0x52655C+106,0),result(0,2)),
        ("fifth queue word105",lambda u:w16(u,unit+0xA0,105),result(0,3)),
        ("fifth queue word106 allowed",lambda u:w16(u,unit+0xA0,106),result(1)),
        ("hallucination status",lambda u:w32(u,unit+0xDC,0x40000003),result(0,22)),
        ("missing offset zero",lambda u:w16(u,REQ_OFFSETS+106*2,0),result(0,23)),
    ]:
        u=new(program=[]);patch(u);observe("unit gate "+kind,{},call(u,228 if kind=="out of range ID" else 106),expected)

    # Execute every shipped unit recipe using the unchanged original program.
    for e in entries:
        producers=[t["operands"][0] for t in e["decoded_tokens"] if t["token"]==0xFF02]
        if not producers:raise ValueError("Recipe without explicit producer")
        producer=producers[0];u=new(producer,e["unit_id"])
        for t in e["decoded_tokens"]:
            if t["token"]==0xFF04:
                w32(u,unit+0xC0,target);w16(u,target+0x64,t["operands"][0])
            if t["token"]==0xFF0E:w8(u,unit+0xC8,0);w8(u,unit+0xC9,0)
        observe("shipped original unit recipe",{"requested_unit":e["unit_id"],"producer_type":producer,"all_completed_counts":1,"all_allunit_counts":1,"all_researched":1,"all_upgrade_levels":1},
                call(u,e["unit_id"]),result(1))

    tests=[
      ("producer match",[0xFF02,7],lambda u:None,result(1)),
      ("producer wrong existing parent",[0xFF02,7],lambda u:w16(u,unit+0x64,64),result(-1,25)),
      ("producer wrong missing parent",[0xFF02,7],lambda u:(w16(u,unit+0x64,64),w32(u,0x52C0C4+7*48,0)),result(-1,8)),
      ("OR producer second match",[0xFF02,64,0xFF01,0xFF02,7],lambda u:None,result(1)),
      ("completed prerequisite missing",[111],lambda u:w32(u,0x52C0C4+111*48,0),result(-1,8)),
      ("completed prerequisite cheat",[111],lambda u:(w32(u,0x52C0C4+111*48,0),w32(u,0x5432FC,0x2000)),result(1)),
      ("has unit completed0 all1",[0xFF03,111],lambda u:w32(u,0x52C0C4+111*48,0),result(1)),
      ("has unit both0",[0xFF03,111],lambda u:(w32(u,0x52C0C4+111*48,0),w32(u,0x529604+111*48,0)),result(-1,8)),
      ("OR prerequisite second exists",[111,0xFF01,112],lambda u:w32(u,0x52C0C4+111*48,0),result(1)),
      ("AND second missing",[111,112],lambda u:w32(u,0x52C0C4+112*48,0),result(-1,8)),
      ("addon absent",[0xFF04,120],lambda u:None,result(-1,4)),
      ("addon matching",[0xFF04,120],lambda u:(w32(u,unit+0xC0,target),w16(u,target+0x64,120)),result(1)),
      ("addon wrong",[0xFF04,120],lambda u:(w32(u,unit+0xC0,target),w16(u,target+0x64,119)),result(-1,4)),
      ("addon cheated",[0xFF04,120],lambda u:w32(u,0x5432FC,0x2000),result(1)),
      ("grounded status present",[0xFF05],lambda u:None,result(1)),
      ("grounded status absent",[0xFF05],lambda u:w32(u,unit+0xDC,1),result(0,7)),
      ("not busy empty",[0xFF07],lambda u:None,result(1)),
      ("not busy queued",[0xFF07],lambda u:w16(u,unit+0x98,0),result(0,5)),
      ("not busy research byte",[0xFF07],lambda u:w8(u,unit+0xC8,0),result(0,5)),
      ("not busy upgrade byte",[0xFF07],lambda u:w8(u,unit+0xC9,0),result(0,5)),
      ("not constructing addon",[0xFF08],lambda u:None,result(1)),
      ("constructing addon",[0xFF08],lambda u:(w8(u,unit+0xA6,0x25),w32(u,unit+0xEC,target)),result(0,5)),
      ("not teching",[0xFF09],lambda u:None,result(1)),
      ("teching",[0xFF09],lambda u:w8(u,unit+0xC8,0),result(0,5)),
      ("not upgrading",[0xFF0A],lambda u:None,result(1)),
      ("upgrading",[0xFF0A],lambda u:w8(u,unit+0xC9,0),result(0,5)),
      ("SCV constructing",[0xFF0B],lambda u:w8(u,unit+0x4D,0x21),result(0,5)),
      ("nonSCV constructing order",[0xFF0B],lambda u:(w16(u,unit+0x64,64),w8(u,unit+0x4D,0x21)),result(1)),
      ("no addon",[0xFF0C],lambda u:None,result(1)),
      ("has addon",[0xFF0C],lambda u:w32(u,unit+0xC0,target),result(0,4)),
      ("no missile",[0xFF10],lambda u:None,result(1)),
      ("has missile",[0xFF10],lambda u:w32(u,unit+0xD0,target),result(0,3)),
      ("not burrowed",[0xFF11],lambda u:None,result(1)),
      ("is burrowed rejected",[0xFF11],lambda u:w32(u,unit+0xDC,0x13),result(0,5)),
      ("burrow required absent",[0xFF26],lambda u:None,result(-1,5)),
      ("burrow required present",[0xFF26],lambda u:w32(u,unit+0xDC,0x13),result(1)),
      ("disabled token",[0xFF22],lambda u:None,result(-1,21)),
      ("blank token",[0xFF23],lambda u:None,result(0,23)),
      ("expansion true",[0xFF24],lambda u:None,result(1)),
      ("expansion false",[0xFF24],lambda u:w8(u,0x536720,0),result(0,26)),
      ("tech researched",[0xFF25,35],lambda u:None,result(1)),
      ("tech missing",[0xFF25,35],lambda u:w8(u,0x536408+35,0),result(-1,9)),
      ("tech cheated",[0xFF25,35],lambda u:(w8(u,0x536408+35,0),w32(u,0x5432FC,0x1000)),result(1)),
      ("immediate failure not rescued by following OR",[0xFF05,0xFF01,7],lambda u:w32(u,unit+0xDC,1),result(0,7)),
      ("has unit count u32 overflow",[0xFF03,111],lambda u:(w32(u,0x52C0C4+111*48,0xFFFFFFFF),w32(u,0x529604+111*48,1)),result(-1,8)),
    ]
    for name,program,patch,expected in tests:
        u=new(program=program);patch(u);observe("requirement token "+name,{"program_words":program},call(u),expected)
    for producer,hero,upgrade,in_hangar,out_hangar,queued,capacity in [(72,False,0,0,0,0,4),(72,False,1,3,4,0,8),(72,True,0,7,0,0,8),(83,False,0,4,0,0,5),(83,False,1,9,0,0,10),(83,True,0,9,0,0,10)]:
        for at_limit in (False,True):
            u=new(producer,106,[0xFF0E]);w32(u,0x521660+producer*4,0x40 if hero else 0)
            levelid=43 if producer==72 else 36;w8(u,0x534590+levelid,upgrade)
            amount=capacity if at_limit else capacity-1
            w8(u,unit+0xC8,amount);w8(u,unit+0xC9,0)
            observe("hangar capacity boundary",{"producer":producer,"hero":hero,"upgrade_level":upgrade,"fighters":amount,"capacity":capacity},call(u),result(0,3) if at_limit else result(1))
    for producer,queued_type,existing,expected in [(72,73,3,result(0,3)),(72,85,3,result(0,3)),(72,228,3,result(1)),(83,85,4,result(0,3)),(83,73,4,result(0,3)),(83,228,4,result(1))]:
        u=new(producer,106,[0xFF0E]);w8(u,0x534590+(43 if producer==72 else 36),0)
        w8(u,unit+0xC8,existing);w8(u,unit+0xC9,0);w16(u,unit+0x98,queued_type)
        observe("hangar queued fighter types share counter",{"producer":producer,"queued_type":queued_type,"existing_fighters":existing},call(u),expected)
    for head in range(5):
        for queue_id in (105,106,228):
            u=new(program=[]);w8(u,unit+0xA4,head);w16(u,unit+0x98+2*((head+4)%5),queue_id)
            observe("fifth relative queue slot gate",{"head":head,"relative4_id":queue_id},call(u),result(0,3) if queue_id<=105 else result(1))
    return {"format_version":1,"target_sha256":hashlib.sha256(data).hexdigest(),"probe_script_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
       "original_code_executed":True,"complete_game_executed":False,"helper_stubs":[],"runtime_versions":{"python":platform.python_version(),"unicorn":unicorn.__version__,"capstone":capstone.__version__,"pefile":pefile.__version__,"architecture":platform.machine(),"system":platform.system()},
       "allowlisted_preferred_va_ranges":[[hex(a),hex(b)] for a,b in allowed],"all_matched":True,"case_count":len(cases),"cases":cases}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--exe",type=Path,required=True);p.add_argument("--out",type=Path,required=True)
    p.add_argument("--execute-isolated-original-code",action="store_true");p.add_argument("--probe-out",type=Path);a=p.parse_args()
    if a.execute_isolated_original_code and a.probe_out is None:p.error("--probe-out required")
    data=a.exe.read_bytes()
    if hashlib.sha256(data).hexdigest()!=TARGET_HASH:p.error("EXE hash differs from reviewed1.17 artifact")
    pe=pefile.PE(data=data);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);entries,end=parse_unit_table(pe)
    targets=struct.unpack("<19I",pe.get_data(0x4B37DC-0x400000,76));indices=list(pe.get_data(0x4B3828-0x400000,37))
    out={"format_version":1,"target_sha256":TARGET_HASH,"inspector_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),"original_code_executed":False,"complete_game_executed":False,
        "unit_requirement_table":{"preferred_va":hex(REQ_TABLE),"end_exclusive_preferred_va":hex(REQ_TABLE+end*2),"raw_bytes_sha256":hashlib.sha256(pe.get_data(REQ_TABLE-0x400000,end*2)).hexdigest(),"recipe_count":len(entries),"recipes":entries},
        "unit_gate_switch":[{"token":0xFF02+j,"name":NAMES.get(0xFF02+j,"default completed-unit-count branch"),"branch_index":i,"preferred_va":hex(targets[i]),"dedicated":i!=18} for j,i in enumerate(indices)],"witnesses":{}}
    for name,(start,finish) in WITNESSES.items():
        raw=pe.get_data(start-0x400000,finish-start);out["witnesses"][name]={"start_preferred_va":hex(start),"end_exclusive_preferred_va":hex(finish),"bytes_sha256":hashlib.sha256(raw).hexdigest(),
            "instructions":[{"address":hex(i.address),"bytes":i.bytes.hex(),"mnemonic":i.mnemonic,"operands":i.op_str} for i in md.disasm(raw,start)]}
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(out,indent=2)+"\n")
    if a.execute_isolated_original_code:
        r=probes(data,pe,entries);a.probe_out.parent.mkdir(parents=True,exist_ok=True);a.probe_out.write_text(json.dumps(r,indent=2)+"\n");print(f"{r['case_count']} original requirement cases passed; no stubs")
    else:print(f"Decoded{len(entries)} original unit recipes; original code not executed")


if __name__=="__main__":main()
