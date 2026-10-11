#!/usr/bin/env python3
"""Execute exact-hash hidden Train placement/show and unit creation dependencies.

Default mode only prints a plan. Explicit execution runs reviewed original x86
instructions and actual callees; no helper stubs, Windows process, game loop,
imports or guest host-file/network operations. Synthetic profiles and explicit
prefix stops remain separate from a complete normal trained-unit lifecycle.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import subprocess
import sys
import pefile
import inspect_native_training_lifecycle_117 as lifecycle
import inspect_native_unit_creation_117 as local
import probe_native_trigger_unit_counts_117 as spatial
import inspect_native_primary_orders_117 as primary
import inspect_native_unit_ai_117 as unit_helpers
import inspect_native_training_117 as admission
import inspect_native_military_ai_117 as regions
import probe_native_placement_117 as placement
import probe_native_trigger_unit_actions_117 as unit_actions

UNIT,CHILD,TARGET,AI,GUARD,REGION,AREA,TBL,SPRITE,CSPRITE,IMAGE,CIMAGE,POOL,NODE = (
    0x5DA3A0,0x5DA4F0,0x5DA640,0x801000,0x801100,0x802000,0x810000,
    0x806000,0x804000,0x804100,0x804200,0x804300,0x807000,0x805000)
STACK,STOP,TERRAIN,TOWN,FOW,GEOMETRY=0x900000,0xA00000,0xB00000,0x803000,0xC00000,0x80A000
CONNECTIVITY={k:placement.BLOCKS[k] for k in ('point_connectivity_group','region_at_pixel','native_memset','first_filtered_border_query','spatial_rectangle_query')}
DEATH={k:unit_actions.BLOCKS[k] for k in ('death_order','drop_powerup_zero_carry','clear_unit_ai','remove_military_ai_null','remove_town_ai_null','clear_town_resource_refs','clear_guard_ai_null','clear_queen_selection')}
ALLOW=list(local.BLOCKS.values())+list(lifecycle.BLOCKS.values())+list(spatial.BLOCKS.values())+list(primary.BLOCKS.values())+list(unit_helpers.BLOCKS.values())+list(admission.WITNESSES.values())+list(regions.BLOCKS.values())+list(CONNECTIVITY.values())+list(DEATH.values())
PLAN=dict(target_sha256=local.TARGET_HASH, instruction_bound=50000, helper_stubs=[],
    fixture='Synthetic producer/child, private pools, DAT/supply/AI/region records; real original callees',
    scope='Hidden-child Train placement/show/completion, generated vision masks, indexed movement, density and cancellation issuance',
    limits='Synthetic pools, terrain, DAT and fog fixtures; no whole game, Windows API, imports or guest host I/O')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path)
    p.add_argument('--execute-original-code',action='store_true')
    p.add_argument('--units-dat',type=Path,required=True);p.add_argument('--orders-dat',type=Path,required=True)
    p.add_argument('--pyms',type=Path,required=True)
    a=p.parse_args();raw=a.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=local.TARGET_HASH:p.error('Unknown executable hash')
    if not a.execute_original_code:print(json.dumps(PLAN,indent=2));return
    if not a.out:p.error('Execution requires --out')
    if platform.machine().lower() not in ('x86_64','amd64'):p.error('Use reviewed x86_64 runtime')
    datraw=a.units_dat.read_bytes();dat_hash=hashlib.sha256(datraw).hexdigest()
    if dat_hash!='da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba':p.error('Unknown effective units.dat')
    pyms_commit=subprocess.check_output(['git','-C',str(a.pyms),'rev-parse','HEAD'],text=True).strip()
    if pyms_commit!='bfc5d3aad0b5614a5aff72c223f8efa00afddfa4':p.error('Unreviewed PyMS')
    sys.path.insert(0,str(a.pyms));from PyMS.FileFormats.DAT.UnitsDAT import UnitsDAT
    dat=UnitsDAT();dat.load_data(datraw);orders=primary.stock_orders(a.orders_dat.read_bytes())
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP
    pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
    image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095
    cases=[]
    allowed_cache={}
    def put(u,addr,v,n=4):u.mem_write(addr,(v&((1<<(n*8))-1)).to_bytes(n,'little'))
    def get(u,addr,n=4):return int.from_bytes(u.mem_read(addr,n),'little')
    def new(f=None):
        f=f or {};u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,size);u.mem_write(0x400000,image)
        for addr,length in ((0x800000,0x20000),(STACK,0x4000),(STOP,0x1000),(TERRAIN,0xA0000),(FOW,0x60000)):u.mem_map(addr,length)
        for addr,sp,img,uid in ((UNIT,SPRITE,IMAGE,f.get('producer_id',111)),(CHILD,CSPRITE,CIMAGE,f.get('child_id',0)),(TARGET,CSPRITE,CIMAGE,7)):
            put(u,addr+0xC,sp);put(u,addr+0x64,uid,2);put(u,addr+0x4C,3,1)
            put(u,addr+0x4D,3,1);put(u,addr+0xDC,0);put(u,addr+8,1000)
            put(u,addr+0x50,228,2);put(u,addr+0x90,0xFF,1);put(u,addr+0xA6,38,1)
            for offset in (0x18,0x20):put(u,sp+offset,img)
            put(u,sp+0x14,200,2);put(u,sp+0x16,200,2)
            # Empty image list for animation fanout; main image remains valid.
            put(u,sp+0x1C,0);put(u,img+0x16,0,1)
            put(u,0x521660+uid*4,0);put(u,0x520708+uid*4,25600)
            put(u,0x520458+uid,0,1);put(u,0x520540+uid*2,0,2)
            put(u,0x523C40+uid,10,1);put(u,0x523D28+uid,0,1);put(u,0x523E10+uid,2,1)
            put(u,0x520F20+uid,3,1);put(u,0x520D50+uid,3,1);put(u,0x520E38+uid,3,1)
            put(u,0x6AC530+uid*4,200);put(u,0x6AC8C0+uid*4,300)
            put(u,0x522C38+uid*8,8,2);put(u,0x522C3A+uid*8,8,2)
            put(u,0x522C3C+uid*8,8,2);put(u,0x522C3E+uid*8,8,2)
        put(u,UNIT+0xDC,f.get('status',3));put(u,UNIT+0xE2,f.get('state',0),1)
        put(u,UNIT+0xEC,f.get('current_build',0));put(u,UNIT+0xA4,f.get('slot',0),1)
        u.mem_write(UNIT+0x98,struct.pack('<5H',*f.get('queue',[0,228,228,228,228])))
        put(u,CHILD+0xA8,f.get('hp_gain',257),2);put(u,CHILD+0xAC,f.get('timer',10),2)
        put(u,CHILD+0xDC,f.get('child_status',0));put(u,CSPRITE+0xE,32 if f.get('hidden') else 0,1)
        put(u,0x523C40+f.get('producer_id',111),f.get('producer_group',18),1)
        put(u,0x5432FC,2 if f.get('cwal') else 0)
        put(u,0x6726D0+3*36,f.get('player_type',2),1);put(u,0x508D50,0);put(u,0x67CC90,0)
        put(u,0x5294B4+3*4,400);put(u,0x5294E4+3*4,0);put(u,0x529514+3*4,20)
        put(u,0x5263D0+3*4,1000);put(u,0x526400+3*4,1000)
        put(u,0x53DAD0+3*4,50);put(u,0x53DB00+3*4,0)
        put(u,0x543230,4096,2);put(u,0x543234,4096,2)
        put(u,0x673930,1);put(u,0x67393C,12345)
        put(u,0x665AF0,f.get('free_unit',0));put(u,0x665AF4,f.get('free_unit',0));put(u,0x665B38,f.get('unit_count',0))
        put(u,0x665DE0,TBL);put(u,0x6AB4F8,TBL);put(u,TBL,0,2)
        put(u,0x5C51C8,0);put(u,0x5C51CC,0)
        put(u,0x516D38,0);put(u,0x543300,0xABCDEF);put(u,0x543304,0x234567)
        put(u,0x5AEF00,POOL);put(u,0x5AEF04,POOL+15*20);put(u,0x5AEF08,0)
        for i in range(16):put(u,POOL+i*20,POOL+(i-1)*20 if i else 0);put(u,POOL+i*20+4,POOL+(i+1)*20 if i<15 else 0)
        u.mem_write(0x51B200,bytes([1])*189);u.mem_write(0x51B980,bytes([255])*378)
        if f.get('ai'):put(u,UNIT+0x134,AI);put(u,AI+8,3,1)
        for i in range(5):put(u,AI+9+i,1+i,1);put(u,AI+0x18+i*4,0x11110000+i)
        put(u,REGION+0x2C,AREA);put(u,0x6B1A74+3*4,REGION)
        put(u,0x69B584,TERRAIN);put(u,TERRAIN,4,2)
        put(u,TERRAIN+0x44A0C,200*256);put(u,TERRAIN+0x44A10,200*256)
        put(u,0x6BE610+3*8,AREA);put(u,AREA+0x7D00,0)
        return u
    def call(u,start,ecx=UNIT,edx=0,args=(),stop_before=None):
        visited=[];visited_blocks=set();calls=[];boundary=None;boundary_values={}
        entries={lo for lo,hi in ALLOW}
        def hook(m,addr,width,_):
            nonlocal boundary,boundary_values
            if addr in (STOP,stop_before):
                boundary=addr;sp=m.reg_read(UC_X86_REG_ESP)
                boundary_values=dict(ecx=m.reg_read(UC_X86_REG_ECX),edx=m.reg_read(UC_X86_REG_EDX),
                    stack=[get(m,sp+4+i*4) for i in range(6)])
                m.emu_stop();return
            key=(addr,width)
            if key not in allowed_cache:allowed_cache[key]=tuple(lo for lo,hi in ALLOW if lo<=addr and addr+width<=hi)
            if not allowed_cache[key]:raise RuntimeError(f'Outside reviewed original code {addr:#x}')
            visited_blocks.update(allowed_cache[key])
            visited.append(addr)
            if addr in entries:
                sp=m.reg_read(UC_X86_REG_ESP)
                calls.append(dict(callee=hex(addr),ecx=m.reg_read(UC_X86_REG_ECX),edx=m.reg_read(UC_X86_REG_EDX),
                    stack=[get(m,sp+4+i*4) for i in range(6)]))
        h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0x3000
        u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*(x&0xFFFFFFFF for x in args)))
        u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
        try:u.emu_start(start,STOP+1,count=50000)
        except unicorn.UcError as error:
            raise RuntimeError(f'Guest fault after {hex(visited[-1]) if visited else "entry"}; recent function calls {calls[-3:]}') from error
        finally:u.hook_del(h)
        if boundary is None:raise RuntimeError('No return/boundary within instruction bound')
        if boundary==STOP:assert u.reg_read(UC_X86_REG_ESP)==sp+4+len(args)*4
        return dict(complete_return=boundary==STOP,stop_va=hex(boundary),return_eax=u.reg_read(UC_X86_REG_EAX),
            instruction_count=len(visited),boundary_registers_arguments=boundary_values,calls=calls,
            visited_functions=[hex(lo) for lo in sorted(visited_blocks)])
    def record(name,inputs,actual,expected,ex):
        assert actual==expected,(name,inputs,actual,expected)
        cases.append(dict(case=name,inputs=inputs,actual=actual,expected=expected,passed=True,execution=ex))
    def initialize_vision(u,sight,stride=128):
        descriptor=0x508A50+sight*28;grid=TERRAIN+0x60000;records=TERRAIN+0x64000
        inner=get(u,descriptor+8)*get(u,descriptor+12)
        put(u,descriptor+0x10,inner)
        grid_ex=call(u,0x466990,ecx=descriptor,edx=grid);total=grid_ex['return_eax']
        put(u,descriptor+0x14,total-inner);put(u,descriptor+0x18,records)
        u.mem_write(records,bytes(total*20))
        records_ex=call(u,0x466BD0,ecx=descriptor,edx=grid,args=(stride,))
        output=[]
        for i in range(total):
            row=records+i*20;dx,dy=struct.unpack('<bb',u.mem_read(row+16,2));n=get(u,row+18,1)
            dependencies=[(get(u,row+k*4)-records)//20 for k in range(n)]
            assert n<=2 and all(0<=x<i for x in dependencies)
            assert get(u,row+8)==((dx+dy*stride)&0xFFFFFFFF)
            output.append(dict(dx=dx,dy=dy,offset=dx+dy*stride,dependency_indices=dependencies))
        width,height=get(u,descriptor),get(u,descriptor+4)
        marked={(i%width-width//2,i//width-height//2) for i in range(width*height) if get(u,grid+i*6+5,1)}
        assert {(x['dx'],x['dy']) for x in output}==marked and len(marked)==total
        assert all((-x,y) in marked and (x,-y) in marked for x,y in marked)
        return dict(sight=sight,width=width,height=height,inner_count=inner,outer_count=total-inner,
            records=output,grid_execution=grid_ex,records_execution=records_ex)
    def state(u):return dict(secondary=get(u,UNIT+0xA6,1),state=get(u,UNIT+0xE2,1),building=get(u,UNIT+0xEC),
        slot=get(u,UNIT+0xA4,1),queue=list(struct.unpack('<5H',u.mem_read(UNIT+0x98,10))),
        child_hp=get(u,CHILD+8),child_timer=get(u,CHILD+0xAC,2),child_status=get(u,CHILD+0xDC),
        error=get(u,0x5DA398,2),stocks=[get(u,0x5263D0+12),get(u,0x526400+12)])
    def prepare(stock=False,child_id=8,producer_id=114,ai=False,rally='none',flying=True):
        u=new(dict(state=2,current_build=CHILD,timer=0,hidden=True,child_id=child_id,child_status=4 if flying else 0,producer_id=producer_id,queue=[child_id,228,228,228,228],ai=ai,player_type=1 if ai else 2))
        if stock:
            for uid,x in enumerate(dat.entries):
                e=x.unit_extents;box=x.staredit_placement_size
                u.mem_write(0x522C38+uid*8,struct.pack('<4H',e.left,e.up,e.right,e.down))
                u.mem_write(0x522728+uid*4,struct.pack('<2H',box.width,box.height))
                for base,value in ((0x521660,x.special_ability_flags),(0x520708,x.hit_points.whole*256+x.hit_points.fraction)):
                    put(u,base+uid*4,value)
                for base,value in ((0x523C40,x.staredit_group_flags),(0x523D28,x.supply_provided.whole*2+x.supply_provided.half),
                    (0x521AD8,x.sight_range),(0x523FE0,x.space_provided),(0x520D50,x.comp_ai_idle),(0x520E38,x.human_ai_idle),
                    (0x520F20,x.return_to_idle),(0x520458,x.shield_enabled)):
                    put(u,base+uid,value,1)
                put(u,0x520540+uid*2,x.shield_amount,2)
            for row in orders:
                put(u,0x51B200+row['id'],row['interruptible'],1);put(u,0x51B980+row['id']*2,row['highlight'],2)
                put(u,0x51B080+row['id'],row['subunit_inheritance'],1)
            put(u,CHILD+8,dat.entries[child_id].hit_points.whole*256)
            put(u,CHILD+0x60,dat.entries[child_id].shield_amount*256)
            put(u,CSPRITE+0xD,dat.entries[child_id].elevation_level,1)
        for addr in (UNIT,CHILD,TARGET):
            for off in (0x10,0x12,0x1C,0x1E,0x28,0x2A):put(u,addr+off,200,2)
            put(u,addr+0x2C,200*256);put(u,addr+0x30,200*256)
            for off in (0x13C,0x140,0x144,0x148):put(u,addr+off,0xFFFFFFFF)
        put(u,0x5264B4,128,2);put(u,0x5264B6,128,2)
        put(u,0x666454,FOW);put(u,0x666458,FOW+0x10000)
        put(u,0x66696C,FOW+0x20000);put(u,0x6CE650,FOW+0x40000);put(u,0x66644C,FOW+0x50000)
        put(u,0x54323C,FOW+0x10000);put(u,TERRAIN+0x97A1C,GEOMETRY)
        u.mem_write(FOW,struct.pack('<I',0x10000)*16384)
        put(u,CHILD+0x105,0 if flying else 1,1)
        put(u,0x6D319C,2);put(u,0x5C99B0,0);put(u,0x5D6E40,256);put(u,0x5D6E44,160)
        put(u,0x665AE0,UNIT);put(u,0x665AE4,UNIT);put(u,0x665AF8,CHILD);put(u,0x665AFC,CHILD)
        put(u,0x665B08+12,UNIT);put(u,UNIT+0x6C,CHILD);put(u,CHILD+0x68,UNIT)
        put(u,0x6D3140,0)
        put(u,0x5C51D0+6*4,SPRITE);put(u,0x5C55D0+6*4,CSPRITE)
        put(u,SPRITE+4,CSPRITE);put(u,CSPRITE,SPRITE)
        if not stock:
            put(u,0x522728+child_id*4,32,2);put(u,0x52272A+child_id*4,32,2)
            put(u,0x521AD8+child_id,7,1);put(u,0x523FE0+child_id,0,1)
        put(u,0x5264CC+12,8);put(u,0x672668,1);put(u,0x67266C,1)
        put(u,0x51B080+3,0,1)
        put(u,TARGET+0xC,0x804500);put(u,0x804500+0x14,300,2);put(u,0x804500+0x16,300,2)
        put(u,0x804500+0x18,0x804600);put(u,0x804500+0x20,0x804600)
        if ai:
            put(u,AI+9,2,1);put(u,AI+0x18,GUARD);put(u,GUARD+0x16,321,2);put(u,GUARD+0x18,654,2)
            put(u,AI+0x14,TOWN);put(u,TOWN+8,AREA);put(u,TOWN+0x10,AREA);put(u,TOWN+0x18,3,1)
            put(u,AREA+0x5DC0,NODE);put(u,0x67D280+3*0x4E8,0x20,2)
        if rally!='none':
            put(u,UNIT+0xF8,321 if rally!='zero_x' else 0,2);put(u,UNIT+0xFA,654,2)
            put(u,UNIT+0xFC,TARGET if rally in ('follow','other_owner') else UNIT if rally=='self' else 0)
            if rally=='other_owner':put(u,TARGET+0x4C,4,1)
        call(u,0x4195A0,UNIT)
        return u
    u=prepare();ex=call(u,0x439310)
    record('hidden_train_flyer_complete_initial',dict(child_type=8,hidden=True,flying=True,frame_remaining=0),
        [get(u,CHILD+0xDC),get(u,CSPRITE+0xE,1),get(u,UNIT+0xE2,1),get(u,UNIT+0xEC),get(u,0x5C99B0)],
        [5,0,0,0,4],ex)
    u=prepare(stock=True);ex=call(u,0x439310)
    record('hidden_train_Wraith_stock_dat',dict(child_type=8,producer_type=114,stock_units_dat=True,hidden=True,flying=True,frame_remaining=0),
        [get(u,CHILD+0xDC),get(u,CSPRITE+0xE,1),get(u,CHILD+0x4D,1),get(u,UNIT+0xE2,1),get(u,UNIT+0xEC),get(u,0x5C99B0)],
        [5,0,2,0,0,4],ex)
    for sight in range(12):
        for stride in (64,128,256):
            u=new();shape=initialize_vision(u,sight,stride)
            record(f'original_vision_template_sight_{sight}_stride_{stride}',dict(sight=sight,map_width=stride,preallocated_private_buffers=True),
                [shape['width'],shape['height'],shape['inner_count'],len(shape['records']),shape['outer_count']],
                [3+2*sight,3+2*sight,9,[9,21,37,61,97,129,177,221,277,349,413,489][sight],[0,12,28,52,88,120,168,212,268,340,404,480][sight]],
                shape['records_execution'])
            cases[-1]['native_shape']=shape
    u=new();ex=call(u,0x466E40,ecx=0x508A50,edx=12,args=(128,),stop_before=0x4D001C)
    record('vision_template_allocator_SAlloc_prefix',dict(template_count=12,map_width=128,stop_before='0x4d001c'),
        [ex['stop_va'],ex['boundary_registers_arguments']['stack'][:4]],['0x4d001c',[3750,0x4EE64C,0x11E,0]],ex)
    for child_id,producer_id in ((8,114),(9,114),(11,114),(12,114),(58,114),(70,167),(72,167),(60,167),(69,155),(84,155)):
        for ai in (False,True):
            for rally in ('none','move','follow','other_owner','self','zero_x'):
                u=prepare(stock=True,child_id=child_id,producer_id=producer_id,ai=ai,rally=rally)
                ex=call(u,0x439310,stop_before=0x41D200 if child_id==84 else None)
                if child_id==84:
                    record(f'hidden_stock_Observer_permanent_cloak_prefix_ai_{ai}_rally_{rally}',
                        dict(child_type=child_id,producer_type=producer_id,computer=ai,rally=rally,stop_before='0x41d200'),
                        [ex['stop_va'],get(u,CSPRITE+0xE,1),get(u,UNIT+0xE2,1),get(u,UNIT+0xEC),get(u,CHILD+0x134)],
                        ['0x41d200',32,2,CHILD,0],ex)
                    continue
                idle=dat.entries[child_id].comp_ai_idle if ai else dat.entries[child_id].human_ai_idle
                order=49 if rally=='follow' else 6 if rally in ('move','other_owner') else idle
                expected_target=TARGET if rally=='follow' else 0
                expected_ai=GUARD if ai and child_id!=84 else 0
                record(f'hidden_stock_Train_{child_id}_{producer_id}_ai_{ai}_rally_{rally}',
                    dict(child_type=child_id,producer_type=producer_id,stock_units_dat=True,hidden=True,computer=ai,rally=rally,vision_shape_counts=0),
                    [get(u,CHILD+0xDC)&5,get(u,CSPRITE+0xE,1),get(u,CHILD+0x4D,1),get(u,CHILD+0x5C),get(u,CHILD+0x134),
                     get(u,UNIT+0xE2,1),get(u,UNIT+0xEC),get(u,UNIT+0xA4,1),get(u,UNIT+0x98,2),get(u,0x5C99B0),get(u,0x665AF8),get(u,0x665AE0)],
                    [5,0,order,expected_target,expected_ai,0,0,1,228,4,0,UNIT],ex)
    for child_id,producer_id in ((0,111),(7,106),(65,160),(64,154)):
        for ai in (False,True):
            for rally in ('none','move','follow','other_owner','self','zero_x'):
                u=prepare(stock=True,child_id=child_id,producer_id=producer_id,ai=ai,rally=rally,flying=False)
                ex=call(u,0x439310)
                idle=dat.entries[child_id].comp_ai_idle if ai else dat.entries[child_id].human_ai_idle
                order=49 if rally=='follow' else 6 if rally in ('move','other_owner') else idle
                expected_ai=(GUARD if child_id in (0,65) else NODE) if ai else 0
                record(f'hidden_stock_ground_Train_{child_id}_{producer_id}_ai_{ai}_rally_{rally}',
                    dict(child_type=child_id,producer_type=producer_id,stock_units_dat=True,hidden=True,computer=ai,rally=rally,terrain='flat walkable, empty terrain edge arrays',vision_shape_counts=0),
                    [get(u,CHILD+0xDC)&5,get(u,CSPRITE+0xE,1),get(u,CHILD+0x4D,1),get(u,CHILD+0x5C),get(u,CHILD+0x134),
                     get(u,UNIT+0xE2,1),get(u,UNIT+0xEC),get(u,UNIT+0xA4,1),get(u,UNIT+0x98,2),get(u,0x5C99B0)],
                    [1,0,order,TARGET if rally=='follow' else 0,expected_ai,0,0,1,228,4],ex)
    # Generated original templates feed a complete hidden Train; visibility bits
    # begin unrevealed and unexplored. No synthetic replacement shape is used.
    for child_id,producer_id,flying in ((8,114,True),(0,111,False)):
        for mask in (0,1,8,12,255,256):
            u=prepare(stock=True,child_id=child_id,producer_id=producer_id,flying=flying)
            shape=initialize_vision(u,dat.entries[child_id].sight_range)
            u.mem_write(FOW,struct.pack('<I',0x1FFFF)*16384);put(u,0x5264CC+12,mask)
            ex=call(u,0x439310)
            x,y=get(u,CSPRITE+0x14,2),get(u,CSPRITE+0x16,2);cx,cy=x//32,y//32
            count=shape['outer_count'] if flying else len(shape['records'])
            changed=[];expected_changes=[]
            for i in range(16384):
                value=get(u,FOW+i*4)
                if value!=0x1FFFF:changed.append([i,value])
            if 0<mask<=255:
                for node in shape['records'][:count]:
                    tx,ty=cx+node['dx'],cy+node['dy']
                    if 0<=tx<128 and 0<=ty<128:expected_changes.append([ty*128+tx,0x1FFFF&~(mask|mask<<8)])
            record(f'hidden_stock_Train_real_fog_type_{child_id}_mask_{mask}',
                dict(child_type=child_id,producer_type=producer_id,shared_vision_mask=mask,initial_tile_flags=0x1FFFF,
                    native_generated_sight=shape['sight'],native_records_consumed=count,flying=flying),
                [changed,get(u,CSPRITE+0xC,1),get(u,CSPRITE+0xE,1),get(u,UNIT+0xEC),get(u,UNIT+0xA4,1)],
                [sorted(expected_changes),mask if 0<mask<=255 else 0,0,0,1],ex)
            cases[-1]['placed_position']=[x,y]
    for ai in (False,True):
        for rally in ('none','move','follow','other_owner','self','zero_x'):
            u=prepare(stock=True,child_id=84,producer_id=155,ai=ai,rally=rally)
            shape=initialize_vision(u,dat.entries[84].sight_range)
            ex=call(u,0x439310)
            order=49 if rally=='follow' else 6 if rally in ('move','other_owner') else dat.entries[84].comp_ai_idle if ai else dat.entries[84].human_ai_idle
            record(f'hidden_stock_Observer_cloak_complete_ai_{ai}_rally_{rally}',
                dict(child_type=84,producer_type=155,computer=ai,rally=rally,original_sight_records=len(shape['records']),
                    detector_candidates='producer only, no detector property',sound_backend_enabled=False,linked_image_count=0),
                [get(u,CHILD+0xDC)&0x105,get(u,CHILD+0xE4),get(u,CHILD+0x91,1),get(u,CSPRITE+0xE,1),
                 get(u,CHILD+0x4D,1),get(u,CHILD+0x5C),get(u,CHILD+0x134),get(u,UNIT+0xEC),get(u,UNIT+0xA4,1)],
                [0x105,8,30,0,order,TARGET if rally=='follow' else 0,0,0,1],ex)
    for sight in (0,1,7):
        for flying in (False,True):
            for position in ((0,0),(31,31),(32,32),(100,100),(4000,4000),(4095,4095)):
                for barrier in (False,True):
                    u=prepare();shape=initialize_vision(u,sight)
                    cx,cy=position[0]//32,position[1]//32
                    initial=[0x1FFFF|(0x1000000 if barrier and i%128==cx+2 else 0) for i in range(16384)]
                    u.mem_write(FOW,struct.pack('<16384I',*initial))
                    expected=list(initial);cache=[];clear_mask=~(8|8<<8)&0xFFFFFFFF
                    selected=shape['records'][:shape['outer_count']] if flying else shape['records']
                    for i,node in enumerate(selected):
                        tx,ty=cx+node['dx'],cy+node['dy']
                        value=0xFF
                        if 0<=tx<128 and 0<=ty<128:
                            offset=ty*128+tx
                            allowed=flying or i<shape['inner_count'] or any(not(cache[j]&(8|0x7000000)) for j in node['dependency_indices'])
                            if allowed:
                                expected[offset]&=clear_mask;value=expected[offset]
                        cache.append(value)
                    ex=call(u,0x46BBB0,ecx=position[0],edx=position[1],args=(int(flying),8,sight,0))
                    actual=bytes(u.mem_read(FOW,16384*4));wanted=struct.pack('<16384I',*expected)
                    record(f'vision_reveal_edges_and_barrier_sight_{sight}_flying_{flying}_position_{position}_barrier_{barrier}',
                        dict(sight=sight,flying=flying,position=list(position),vision_mask=8,flat_source_elevation=0,
                            blocking_height_column=cx+2 if barrier else None,tile_flags_without_barrier=0x1FFFF,barrier_flag=0x1000000),
                        [hashlib.sha256(actual).hexdigest(),sum(value!=initial[i] for i,value in enumerate(struct.unpack('<16384I',actual)))],
                        [hashlib.sha256(wanted).hexdigest(),sum(value!=initial[i] for i,value in enumerate(expected))],ex)
    for side in range(4):
        for delta in (-12,-9,-8,-1,0,1,8,9,12):
            for span in ((190,210),(208,240),(209,240),(160,192),(160,191)):
                u=prepare(child_id=0,producer_id=111,flying=False)
                record_pointer=GEOMETRY+0x100
                put(u,GEOMETRY+side*4,record_pointer);put(u,GEOMETRY+0x10+side*2,1,2)
                u.mem_write(record_pointer,struct.pack('<3H2B',200+delta,*span,side,0))
                ex=call(u,0x42ED50,ecx=0,edx=200,args=(200,))
                axis_hit=(-8<=delta<=-1) if side in (0,3) else (1<=delta<=8)
                span_hit=span[0]-8<=200<=span[1]+8
                record(f'ground_footprint_nonempty_edge_{side}_{delta}_{span}',
                    dict(type=0,position=[200,200],half_extents=[8,8,8,8],edge_side=side,edge_axis=200+delta,edge_span=list(span)),
                    ex['return_eax'],int(not(axis_hit and span_hit)),ex)
    for positions in (((100,100),(100,100),(100,100)),((100,100),(116,116),(84,84)),((100,100),(300,50),(200,150))):
        for destination in ((20,20),(100,100),(400,400),(116,84)):
            u=new();put(u,0x5C99B0,0);put(u,0x5D6E40,256);put(u,0x5D6E44,160);put(u,0x6D319C,2)
            expected_x=[];expected_y=[]
            for index,(addr,sp,xy) in enumerate(zip((UNIT,CHILD,TARGET),(SPRITE,CSPRITE,0x804500),positions),1):
                put(u,addr+0xC,sp);put(u,sp+0x14,xy[0],2);put(u,sp+0x16,xy[1],2)
                put(u,addr+0x64,0,2)
                ex=call(u,0x4195A0,ecx=addr)
                for entries,center in ((expected_x,xy[0]),(expected_y,xy[1])):
                    for coordinate in (center-8,center+8):
                        offset=next((i for i,row in enumerate(entries) if row[1]>=coordinate),len(entries))
                        entries.insert(offset,[index,coordinate])
            actual_x=[list(struct.unpack('<II',u.mem_read(0x5C99B8+i*8,8))) for i in range(6)]
            actual_y=[list(struct.unpack('<II',u.mem_read(0x5D03F8+i*8,8))) for i in range(6)]
            assert actual_x==expected_x and actual_y==expected_y
            # Real movement of an already indexed unit updates four bounds and
            # all moved neighbor index fields; the sprite is caller-owned here.
            dx,dy=destination[0]-positions[1][0],destination[1]-positions[1][1]
            ex=call(u,0x419820,ecx=CHILD,args=(dx,dy))
            actual_x=[list(struct.unpack('<II',u.mem_read(0x5C99B8+i*8,8))) for i in range(6)]
            actual_y=[list(struct.unpack('<II',u.mem_read(0x5D03F8+i*8,8))) for i in range(6)]
            wanted_x=sorted([[index,center+(dx if index==2 else 0)] for index,center in expected_x],key=lambda row:row[1])
            wanted_y=sorted([[index,center+(dy if index==2 else 0)] for index,center in expected_y],key=lambda row:row[1])
            assert sorted(actual_x)==sorted(wanted_x) and sorted(actual_y)==sorted(wanted_y)
            assert [r[1] for r in actual_x]==sorted(r[1] for r in actual_x)
            assert [r[1] for r in actual_y]==sorted(r[1] for r in actual_y)
            fields=[]
            for index,addr in enumerate((UNIT,CHILD,TARGET),1):
                values=[get(u,addr+offset) for offset in (0x13C,0x140,0x144,0x148)]
                assert all(array[value][0]==index for array,value in zip((actual_x,actual_x,actual_y,actual_y),values))
                fields.append(values)
            record(f'spatial_insertion_then_indexed_move_{positions}_{destination}',
                dict(positions=[list(x) for x in positions],moved_native_pool_index=2,destination=list(destination),native_insertion_before_equal=True),
                [get(u,0x5C99B0),[r[1] for r in actual_x],[r[1] for r in actual_y]],
                [6,[r[1] for r in wanted_x],[r[1] for r in wanted_y]],ex)
            cases[-1]['spatial_indexes']=dict(x=actual_x,y=actual_y,unit_fields=fields)
    for flying in (False,True):
        for walkable in (False,True):
            for allow_search in (False,True):
                u=prepare(child_id=0,producer_id=111,flying=flying)
                point,output,rectangle=0x809000,0x809010,0x809020
                put(u,point,300,2);put(u,point+2,300,2);put(u,output,0xA5A55A5A)
                u.mem_write(rectangle,struct.pack('<4H',299,299,301,301))
                if not walkable:u.mem_write(FOW,bytes(16384*4))
                ex=call(u,0x47DC30,ecx=CHILD,edx=point,args=(output,rectangle,int(allow_search),0))
                success=flying or walkable
                record(f'placement_unobstructed_tiny_bounds_flying_{flying}_walkable_{walkable}_search_{allow_search}',
                    dict(flying=flying,walkable=walkable,terrain_displacement_allowed=allow_search,desired=[300,300],search_bounds=[299,299,301,301]),
                    [ex['return_eax'],get(u,output),get(u,0x5DA398,2)],
                    [int(success),(300|(300<<16)) if success else 0xA5A55A5A,0 if success else 60],ex)
    u=prepare();put(u,CSPRITE+0xE,0,1)
    before=[get(u,0x5C99B0),get(u,0x665AF8),get(u,0x665AE0),get(u,CSPRITE+0xC,1),get(u,CHILD+0x105,1)]
    ex=call(u,0x422F60,ecx=CHILD)
    record('Show_already_visible_is_noop',dict(sprite_hidden=False),
        [get(u,0x5C99B0),get(u,0x665AF8),get(u,0x665AE0),get(u,CSPRITE+0xC,1),get(u,CHILD+0x105,1)],before,ex)
    for child_id,producer_id,flying in ((0,111,False),(8,114,True),(65,160,False),(7,106,False)):
        for slot in range(5):
            u=prepare(stock=True,child_id=child_id,producer_id=producer_id,flying=flying)
            put(u,UNIT+0xA4,slot,1);queue=[228]*5;queue[slot]=child_id
            u.mem_write(UNIT+0x98,struct.pack('<5H',*queue))
            put(u,0x523520+child_id*2,50,2);put(u,0x5236E8+child_id*2,25,2)
            ex=call(u,0x41B810,args=(0,))
            record(f'cancel_incomplete_hidden_Train_child_{child_id}_queue_head_{slot}',
                dict(child_type=child_id,producer_type=producer_id,head=slot,relative_slot=0,completed=False,
                    configured_price=[50,25],child_ai_pointer=0,powerup_pointer=0,subunit_pointer=0),
                [get(u,0x5263D0+12),get(u,0x526400+12),get(u,CHILD+0x4D,1),get(u,CHILD+0x93,1),
                 get(u,UNIT+0xEC),get(u,UNIT+0xE2,1),get(u,UNIT+0xA4,1),list(struct.unpack('<5H',u.mem_read(UNIT+0x98,10))),get(u,CSPRITE+0xE,1)],
                [1050,1025,0,0,CHILD,2,slot,[228]*5,32],ex)
    seed_next=(12345*22695477+1)&0xFFFFFFFF;random_result=(seed_next>>16)&0x7FFF
    for initial_flags in (0,5,0x38):
        for density in (0,254,255):
            u=prepare(stock=True);put(u,CHILD+0xDC,0x20004);put(u,CHILD+0x14C,initial_flags,1)
            put(u,CHILD+0x14D,0xA5,1);put(u,0x67CF60,FOW+0x30000)
            bucket=FOW+0x30000+4*171+4;put(u,bucket,density,1)
            ex=call(u,0x481760,ecx=CHILD)
            randomized=not(initial_flags&0xF0)
            high=((random_result>>8)&0xF0) or 0xF0
            record(f'air_density_add_flags_{initial_flags}_density_{density}',
                dict(status=0x20004,position=[200,200],initial_air_flags=initial_flags,initial_density=density,seed=12345),
                [get(u,CHILD+0x14C,1),get(u,CHILD+0x14D,1),get(u,CHILD+0x14E,1),get(u,CHILD+0x14F,1),get(u,bucket,1),get(u,0x67393C)],
                [((initial_flags|high) if randomized else initial_flags)&0xF8,random_result&255 if randomized else 0xA5,4,4,min(255,density+1),seed_next if randomized else 12345],ex)
    for child_id,producer_id in ((8,114),(9,114),(11,114),(12,114),(58,114),(70,167),(72,167),(60,167),(69,155),(84,155)):
        u=prepare(stock=True,child_id=child_id,producer_id=producer_id)
        shape=initialize_vision(u,dat.entries[child_id].sight_range)
        put(u,CHILD+0xDC,0x20004);put(u,0x67CF60,FOW+0x30000)
        ex=call(u,0x439310)
        x,y=get(u,CSPRITE+0x14,2),get(u,CSPRITE+0x16,2)
        index=(y//48)*171+x//48
        record(f'hidden_stock_flyer_Train_with_density_{child_id}',
            dict(child_type=child_id,producer_type=producer_id,initial_status=0x20004,seed=12345,original_sight_records=len(shape['records'])),
            [get(u,CHILD+0xDC)&0x20005,get(u,CHILD+0x14E,1),get(u,CHILD+0x14F,1),get(u,FOW+0x30000+index,1),get(u,0x67393C),get(u,UNIT+0xEC)],
            [0x20005,x//48,y//48,1,seed_next,0],ex)
    result=dict(target_sha256=local.TARGET_HASH,target_version='1.17.0.1',original_code_executed=True,
        engine=f'Unicorn {unicorn.__version__}',host_python_arch=platform.machine(),probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        extractor_dependencies={Path(m.__file__).name:hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in (local,lifecycle,spatial,primary,unit_helpers,admission,regions,placement,unit_actions)},
        helper_stubs=[],limits=PLAN,cases=cases,passed=len(cases),failed=0,
        retail_data=dict(units_dat_sha256=dat_hash,orders_dat_sha256=primary.ORDERS_HASH,pyms_commit=pyms_commit),
        complete_returns=sum(x['execution']['complete_return'] for x in cases),
        prefixes=sum(not x['execution']['complete_return'] for x in cases))
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('passed','failed','complete_returns')}))

if __name__=='__main__':main()
