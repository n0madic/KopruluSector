#!/usr/bin/env python3
"""Recover nearest available military selection and actual guard AI transfer.

Static by default; explicit bounded probes use original allowlisted callees,
private pools and hash-gated orders.dat, without helper stubs or game/API entry.
"""
import argparse, hashlib, json, platform, struct
from pathlib import Path
import capstone, pefile
import inspect_native_guard_lifecycle_117 as guards
import inspect_native_primary_orders_117 as primary
import inspect_native_workers_117 as workers
import inspect_native_combat_ai_117 as combat
import inspect_ai_opcodes_117 as opcodes

TARGET_HASH=guards.TARGET_HASH
PLAYER_BASE,STRIDE=guards.PLAYER_BASE,guards.STRIDE
WITNESSES={
 "nearest_available_military":(0x4AF750,0x4AF80A),"remove_unit_ai":(0x48DBC0,0x48DBEB),
 "remove_ai_region_references_and_military":(0x4B0770,0x4B0926),
 "remove_military_record":(0x4A9A30,0x4A9A8E),"map_region_count":(0x481B00,0x481B09),
 "cached_ground_strength":(0x4A9A20,0x4A9A28),"type_air_strength":(0x4A97E0,0x4A97E8),
 "type_ground_strength":(0x4A97F0,0x4A97F8),"clear_or_replace_free_medic":(0x483220,0x483268),
}


def dependencies():
    mods=(guards,primary,workers,combat,opcodes,guards.default,guards.selectors,
          guards.default.dispatch,guards.default.production,guards.default.dispatch.training,
          guards.default.dispatch.research,guards.default.dispatch.training.unit_reqs,
          guards.default.dispatch.research.requirements)
    return {Path(m.__file__).name:hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in mods}


def probes(data,pe,orders):
    if platform.machine().lower() not in ('x86_64','amd64'):raise RuntimeError('Use reviewed x86_64 Rosetta runtime')
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP,UC_X86_REG_EIP
    unit,sprites,ais,regions,guard,military_pool,order_pool=0x800000,0x805000,0x806000,0x810000,0x830000,0x840000,0x850000
    mapbase,stack,stop=0xB00000,0x900000,0xA00000
    image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095;cases=[]
    allowed=list(WITNESSES.values())+list(primary.BLOCKS.values())+list(guards.WITNESSES.values())+list(guards.default.WITNESSES.values())+list(guards.selectors.WITNESSES.values())
    for mod in (guards.default.production,guards.default.dispatch,guards.default.dispatch.training,guards.default.dispatch.training.unit_reqs,guards.default.dispatch.research,guards.default.dispatch.research.requirements):allowed+=list(mod.WITNESSES.values())
    allowed += [workers.BLOCKS[x] for x in ('clear_resource_references','worker_or_building_ai_remove')]
    allowed += [combat.BLOCKS['approximate_distance'],opcodes.WITNESSES['unit_hostility']]

    def w32(u,a,v):u.mem_write(a,struct.pack('<I',v&0xffffffff))
    def w16(u,a,v):u.mem_write(a,struct.pack('<H',v&65535))
    def w8(u,a,v):u.mem_write(a,bytes([v&255]))
    def r32(u,a):return struct.unpack('<I',u.mem_read(a,4))[0]
    def r16(u,a):return struct.unpack('<H',u.mem_read(a,2))[0]
    def r8(u,a):return u.mem_read(a,1)[0]
    def new(records=(),order_count=4):
        u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,size);u.mem_write(0x400000,image)
        u.mem_map(unit,0x80000);u.mem_map(mapbase,0x60000);u.mem_map(stack,0x2000);u.mem_map(stop,0x1000)
        u.mem_write(PLAYER_BASE,bytes(STRIDE*8));u.mem_write(0x6726d0,bytes(0x120));u.mem_write(0x534914,bytes([1])*144)
        w8(u,0x6726d0+7*36,1);w32(u,0x69b584,mapbase);w16(u,mapbase,5)
        for p in range(8):w32(u,0x6b1a74+p*4,regions+p*0x1000)
        for y in range(8):
            for x in range(8):w16(u,mapbase+0xc+2*(y*256+x),3)
        w32(u,0x665b08+28,unit if records else 0);w32(u,0x6be610+56,guard+0x1000);w32(u,0x6be614+56,guard)
        w8(u,guard+8,1);w16(u,guard+0x10,37)
        for f,v in ((0x12,64),(0x14,96),(0x16,64),(0x18,96)):w16(u,guard+f,v)
        w32(u,0x5349d8,100);w32(u,0x508d50,0);w32(u,0x67cc90,0);w32(u,0x5432fc,0)
        w32(u,0x6ab4f8,order_pool+0x1000);w16(u,order_pool+0x1000,0)
        for row in orders:
            w8(u,0x51b200+row['id'],row['interruptible']);w16(u,0x51b980+row['id']*2,row['highlight']);w8(u,0x51b080+row['id'],row['subunit_inheritance'])
        w32(u,0x5aef00,order_pool if order_count else 0);w32(u,0x5aef04,order_pool+20*(order_count-1) if order_count else 0);w32(u,0x5aef08,0)
        for j in range(order_count):w32(u,order_pool+j*20,order_pool+(j-1)*20 if j else 0);w32(u,order_pool+j*20+4,order_pool+(j+1)*20 if j+1<order_count else 0)
        for j,f in enumerate(records):
            a=unit+j*0x200;s=sprites+j*0x100;node=ais+j*0x20;region=regions+7*0x1000+0x34*(j+1)
            w16(u,a+0x64,f.get('id',37));w8(u,a+0x4c,7);w8(u,a+0x4d,f.get('order',1));w32(u,a+0xc,s if f.get('sprite',True) else 0)
            w32(u,a+0x6c,unit+(j+1)*0x200 if j+1<len(records) else 0);w32(u,a+0xdc,f.get('status',1));w16(u,a+0x13a,f.get('strength',10))
            w32(u,a+0x5c,f.get('target',0));w32(u,a+0x7c,f.get('auto_target',0));w16(u,s+0x14,f.get('x',64));w16(u,s+0x16,f.get('y',96))
            for foff in (0x18,0x1c,0x20):w32(u,s+foff,0x860000+j*0x100)
            w32(u,a+0x134,node if f.get('ai',True) else 0);w8(u,node+8,f.get('ai_type',4));w32(u,node+0xc,a);w32(u,node+0x10,region)
            w8(u,region+5,f.get('region_state',0));w16(u,region,f.get('region_id',j+1));w8(u,region+4,7)
            w16(u,region+0xa,f.get('ground_count',1));w16(u,region+0xc,f.get('needed',0));w16(u,region+0x14,f.get('all_ground',100))
            w32(u,region+0x2c,military_pool);w32(u,region+0x30,node);w32(u,military_pool+0x4e20,0)
            w32(u,0x521660+f.get('id',37)*4,0);w8(u,0x520f20+f.get('id',37),3)
        return u
    def call(u,start,ecx=7,edx=37,args=(64,96)):
        reached,count,trace=False,0,[];entries={a for a,b in allowed}
        def hook(m,a,n,_):
            nonlocal reached,count
            if a==stop:reached=True;m.emu_stop();return
            if not any(lo<=a and a+n<=hi for lo,hi in allowed):raise RuntimeError(f'Unreviewed original instruction {a:#x}')
            count+=1
            if a in entries:trace.append(hex(a))
        h=u.hook_add(UC_HOOK_CODE,hook);u.mem_write(stack+0x1800,struct.pack('<'+'I'*(len(args)+1),stop,*args));u.reg_write(UC_X86_REG_ESP,stack+0x1800);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
        try:u.emu_start(start,stop+1,count=100000)
        except unicorn.UcError as error:raise RuntimeError(f'Guest error {error} at {u.reg_read(UC_X86_REG_EIP):#x}; calls{trace[-12:]}') from error
        finally:u.hook_del(h)
        if not reached:raise RuntimeError('No complete return within100000 instructions')
        return u.reg_read(UC_X86_REG_EAX),count,trace
    def observe(label,inputs,ex,got,want):
        if got!=want:raise AssertionError((label,inputs,got,want))
        cases.append({'label':label,'fixture_inputs':inputs,'observed':got,'expected_from_static_contract':want,'matched':True,'original_instruction_count':ex[1],'native_call_trace':ex[2],'execution_scope':'complete reviewed functions and actual callees for fixture path','helper_stubs':[]})
    for state in range(11):
        for all_ground,needed,strength in ((100,89,10),(100,90,10),(100,91,10),(1,0,2)):
            row={'region_state':state,'all_ground':all_ground,'needed':needed,'strength':strength};u=new([row]);ex=call(u,0x4af750)
            observe('region state and signed spare ground strength strict threshold',row,ex,ex[0],unit if state not in (1,7,8,9) and all_ground-strength>needed else 0)
    for changes in ({'sprite':False},{'order':0},{'id':47},{'ai':False},{'ai_type':1},{'status':0},{'target':unit+0x600,'auto_target':unit+0x800}):
        u=new([changes]);ex=call(u,0x4af750);expected=unit if 'status' in changes or 'target' in changes else 0
        observe('selector requires live sprite exact type and militaryAI but not completion or targets',changes,ex,ex[0],expected)
    for positions,expected_index in (([(74,96),(65,96)],1), ([(65,96),(63,96)],0), ([(64,120),(80,96)],1), ([(63,96),(65,96)],0),([(-1,-1),(65,96)],1)):
        rows=[{'x':x,'y':y} for x,y in positions];u=new(rows);ex=call(u,0x4af750)
        observe('nearest approximate center distance and first candidate wins ties',{'positions':positions,'request':[64,96]},ex,ex[0],unit+expected_index*0x200)
    for ty in (0,1,4):
        for status,count in ((1,1),(1,0),(5,0)):
            u=new([{'ai_type':ty,'status':status,'ground_count':count}]);region=regions+7*0x1000+0x34
            if ty==1:w8(u,ais+9,99)
            ex=call(u,0x48dbc0,unit,0,())
            observe('actualAI removal clears unit pointer and recycles military or detaches guard',{'ai_type':ty,'status':status,'ground_count':count},ex,
                {'unitAI':r32(u,unit+0x134),'military_free':r32(u,military_pool+0x4e20),'region_head':r32(u,region+0x30),'ground_count':r16(u,region+0xa),'record_parent':r32(u,ais+0xc),'guard_deaths':r8(u,ais+9)},
                {'unitAI':0,'military_free':ais if ty==4 else 0,'region_head':0 if ty==4 else ais,'ground_count':(count-1)&65535 if ty==4 and not status&4 else count,'record_parent':0 if ty==1 else unit,'guard_deaths':100 if ty==1 else 0})
    for air_need,ground_need,air_strength,ground_strength in ((100,100,20,30),(10,10,20,30),(20,30,20,30)):
        u=new([{}]);enemy=regions+3*0x34
        w8(u,0x6726d0,1);w8(u,0x534914+7,0);w16(u,enemy+0xe,air_need);w16(u,enemy+0xc,ground_need)
        w32(u,0x6ac530+37*4,air_strength);w32(u,0x6ac8c0+37*4,ground_strength)
        for p in (0,7):
            for j in range(1,5):
                for f in (0x1c,0x20,0x24,0x28):w32(u,regions+p*0x1000+j*0x34+f,unit)
        ex=call(u,0x48dbc0,unit,0,())
        observe('AI removal clears all computer-region references and saturates hostile type-strength need',{'enemy_air_ground_need':[air_need,ground_need],'type_strength':[air_strength,ground_strength]},ex,
            {'needs':[r16(u,enemy+0xe),r16(u,enemy+0xc)],'targets':[[r32(u,regions+p*0x1000+j*0x34+f) for f in (0x1c,0x20,0x24,0x28)] for p in (0,7) for j in range(1,5)]},
            {'needs':[max(0,air_need-air_strength),max(0,ground_need-ground_strength)],'targets':[[0]*4]*8})
    for requested in (37,47):
        for order_count in (0,4):
            for status in (1,5):
                u=new([{'id':requested,'status':status} ],order_count);region=regions+7*0x1000+0x34;w16(u,guard+0x10,requested)
                w32(u,guard+0x1c,77);w32(u,0x5349d8,1000)
                ex=call(u,0x4b2320,7,0,())
                observe('complete guard adoption performs real military release and actualprimaryissue',{'id':requested,'order_pool_count':order_count,'status':status,'previous_update':77,'elapsed':1000},ex,
                    {'unitAI':r32(u,unit+0x134),'guard_parent':r32(u,guard+0xc),'home':[r16(u,guard+0x12),r16(u,guard+0x14)],'timestamp':r32(u,guard+0x1c),'military_free':r32(u,military_pool+0x4e20),'region_head':r32(u,region+0x30),'ground_count':r16(u,region+0xa),'primary':r8(u,unit+0x4d),'spending_count':r8(u,PLAYER_BASE+7*STRIDE+0x210),'allocated_orders':r32(u,0x5aef08)},
                    {'unitAI':guard,'guard_parent':unit,'home':[64,96],'timestamp':77,'military_free':ais,'region_head':0,'ground_count':1 if status&4 else 0,'primary':0x9c if order_count else 1,'spending_count':0,'allocated_orders':0})
    for strength in (0,65535):
        u=new([{'strength':strength,'all_ground':65535,'needed':0}]);ex=call(u,0x4af750)
        observe('cached uint16 ground strength boundary still uses strict spare comparison',{'cached_strength':strength,'region_all_ground':65535,'region_needed':0},ex,ex[0],unit if strength==0 else 0)
    u=new([{'target':unit+0x200,'auto_target':unit+0x400}]);region=regions+7*0x1000+0x34
    ex=call(u,0x4b2320,7,0,())
    observe('adoption of engaged military clearsprimarytarget but retainsAI autotarget and cached region strength',{'primary_target':unit+0x200,'auto_target':unit+0x400,'region_cached_all_ground':100},ex,
        {'parent':r32(u,guard+0xc),'primary_target':r32(u,unit+0x5c),'auto_target':r32(u,unit+0x7c),'region_cached_all_ground':r16(u,region+0x14),'primary':r8(u,unit+0x4d)},
        {'parent':unit,'primary_target':0,'auto_target':unit+0x400,'region_cached_all_ground':100,'primary':0x9c})
    return {'format_version':1,'target_sha256':TARGET_HASH,'orders_dat_sha256':primary.ORDERS_HASH,'probe_script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'dependency_script_sha256':dependencies(),'runtime_versions':{'python':platform.python_version(),'unicorn':unicorn.__version__,'capstone':capstone.__version__,'pefile':pefile.__version__,'architecture':platform.machine(),'system':platform.system()},'original_code_executed':True,'complete_game_executed':False,'helper_stubs':[],'allowlisted_preferred_va_ranges':[[hex(a),hex(b)] for a,b in allowed],'all_matched':True,'case_count':len(cases),'cases':cases}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--orders-dat',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--execute-isolated-original-code',action='store_true');p.add_argument('--probe-out',type=Path);a=p.parse_args()
    if a.execute_isolated_original_code and a.probe_out is None:p.error('--probe-out required')
    data=a.exe.read_bytes()
    if hashlib.sha256(data).hexdigest()!=TARGET_HASH:p.error('Wrong EXE hash')
    orders=primary.stock_orders(a.orders_dat.read_bytes());pe=pefile.PE(data=data);c=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    report={'format_version':1,'target_sha256':TARGET_HASH,'orders_dat_sha256':primary.ORDERS_HASH,'inspector_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'dependency_script_sha256':dependencies(),'original_code_executed':False,'complete_game_executed':False,'witnesses':{},'remaining_dependencies':['4b0770 was_killed!=0 cross-region transition branches andstate8 roster branch','worker/building removal fulltown paths separately bounded inworkers report','allhigherAI pool/list writers and state maintenance']}
    for name,(start,end) in WITNESSES.items():
        raw=pe.get_data(start-0x400000,end-start);report['witnesses'][name]={'start_preferred_va':hex(start),'end_exclusive_preferred_va':hex(end),'bytes_sha256':hashlib.sha256(raw).hexdigest(),'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str} for i in c.disasm(raw,start)]}
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(report,indent=2)+'\n')
    if a.execute_isolated_original_code:
        result=probes(data,pe,orders);a.probe_out.parent.mkdir(parents=True,exist_ok=True);a.probe_out.write_text(json.dumps(result,indent=2)+'\n');print(f"{result['case_count']} native guard-transfer cases passed; no stubs")
    else:print('Wrote static guard-transfer witnesses; original code not executed')


if __name__=='__main__':main()
