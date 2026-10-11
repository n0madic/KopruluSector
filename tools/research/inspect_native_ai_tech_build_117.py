#!/usr/bin/env python3
"""Read original prerequisite recipes and authenticate constituent AI instructions."""
import argparse
import ast
import struct
import hashlib
import json
from pathlib import Path
import capstone
import pefile
import inspect_native_requirements_117 as requirements
TARGET_HASH=requirements.TARGET_HASH
BLOCKS={'town_completed_or_queue_count':(0x4872f0,0x4874ec)}
PROGRAM=bytes([56,3,35,34,0,6,1,109,0,80,9,1,109,0,6,1,111,0,80,9,1,111,0,6,1,112,0,80,9,1,112,0,76,1,32,0,78,1,32,0,36])
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--schema',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH;pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
 entries,end=requirements.parse_unit_table(pe);recipe=[x for x in entries if x['unit_id']in(109,111,112,32)]
 def decode_schema(path):
  schema_raw=path.read_bytes();assert hashlib.sha256(schema_raw).hexdigest()=='b30ea0c1c2c4350d4b8de6c910eaf647d6c526a8a2efb64ba966e5c659a5125c'
  tree=ast.parse(schema_raw.decode());aiclass=next(n for n in tree.body if isinstance(n,ast.ClassDef)and n.name=='AIBIN')
  labels=next(ast.literal_eval(n.value)for n in aiclass.body if isinstance(n,ast.Assign)and any(isinstance(t,ast.Name)and t.id=='short_labels'for t in n.targets))
  init=next(n for n in aiclass.body if isinstance(n,ast.FunctionDef)and n.name=='__init__')
  pars=next(n.value for n in init.body if isinstance(n,ast.Assign)and any(isinstance(t,ast.Attribute)and t.attr=='parameters'for t in n.targets))
  params=[[]if isinstance(n,ast.Constant)else[e.attr for e in n.elts]for n in pars.elts];assert len(params)==len(labels)
  return labels,params,hashlib.sha256(schema_raw).hexdigest()
 labels,params,schema_sha=decode_schema(a.schema)
 def decode_interval(data,begin,end):
  rows=[];pc=begin
  while pc<end:
   start=pc;op=data[pc];pc+=1
   if op==0 and pc==end:break
   assert op<len(labels);values=[]
   for typ in params[op]:
    size=1 if typ in('ai_byte','ai_compare')else 4 if typ=='ai_dword'else 2
    if typ=='ai_string':size=data.index(0,pc,end)-pc+1;value=None
    else:value=int.from_bytes(data[pc:pc+size],'little')
    assert pc+size<=end;values.append(value);pc+=size
   rows.append(dict(pc=start,opcode=op,label=labels[op],values=values,next=pc,hex=data[start:pc].hex()))
  assert pc==end
  return rows
 stock_rows=[];bin_hashes={}
 for filename,stride in(('aiscript.bin',16),('bwscript.bin',8)):
  data=(a.data/'scripts'/filename).read_bytes();bin_hashes[filename]=hashlib.sha256(data).hexdigest();table=struct.unpack_from('<I',data)[0];entries=[];pc=table
  while data[pc:pc+4]!=bytes(4):
   ident=data[pc:pc+4].decode('latin1');start=struct.unpack_from('<I',data,pc+4)[0]
   if start:entries.append((start,ident))
   pc+=stride
  starts=sorted(set(x[0]for x in entries))
  for begin,end in zip(starts,starts[1:]+[table]):
   ids=[ident for start,ident in entries if start==begin]
   for row in decode_interval(data,begin,end):row.update(filename=filename,entry_ids=ids,entry_offset=begin);stock_rows.append(row)
 macro=[]
 for row in decode_interval(PROGRAM,0,len(PROGRAM)):
  matches=[x for x in stock_rows if x['hex']==row['hex']and x['opcode']==row['opcode']and x['values']==row['values']]
  row.update(original_matching_instruction_count=len(matches),original_samples=matches[:5],absence_is_no_matching_operands_not_unsupported_opcode=not bool(matches));macro.append(row)
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi)in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(i.size for i in ins)==len(code)
  witnesses[name]=dict(start_preferred_va=hex(lo),end_exclusive_preferred_va=hex(hi),bytes_sha256=hashlib.sha256(code).hexdigest(),instructions=[dict(address=hex(i.address),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str)for i in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',inspector_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),dependency_sha256={Path(requirements.__file__).name:hashlib.sha256(Path(requirements.__file__).read_bytes()).hexdigest()},original_code_executed=False,original_prerequisite_recipes=recipe,declared_macro_hex=PROGRAM.hex(),macro_is_synthetic_composition=True,macro_instructions=macro,pinned_schema_sha256=schema_sha,original_AI_BIN_sha256=bin_hashes,town_count_switch=dict(base_unit=131,last_unit=143,destinations=[hex(x)for x in struct.unpack("<5I",pe.get_data(0x874ec,20))],indices=list(pe.get_data(0x87500,13))),witnesses=witnesses)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(witnesses=len(witnesses),instructions=sum(len(w['instructions'])for w in witnesses.values()),original_recipe=recipe)))
if __name__=='__main__':main()
