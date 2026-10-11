#!/usr/bin/env python3
"""Execute allowlisted exact-hash Train lifecycle functions in private memory.

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
import pefile
import inspect_native_training_lifecycle_117 as local
import inspect_native_primary_orders_117 as primary
import inspect_native_unit_ai_117 as unit_helpers
import inspect_native_training_117 as admission
import inspect_native_military_ai_117 as regions

UNIT,CHILD,TARGET,AI,GUARD,REGION,AREA,TBL,SPRITE,CSPRITE,IMAGE,CIMAGE,POOL,NODE = (
    0x800000,0x800200,0x800400,0x801000,0x801100,0x802000,0x810000,
    0x806000,0x804000,0x804100,0x804200,0x804300,0x807000,0x805000)
STACK,STOP,TERRAIN,TOWN=0x900000,0xA00000,0xB00000,0x803000
ALLOW=list(local.BLOCKS.values())+list(primary.BLOCKS.values())+list(unit_helpers.BLOCKS.values())+list(admission.WITNESSES.values())+list(regions.BLOCKS.values())
PLAN=dict(target_sha256=local.TARGET_HASH, instruction_bound=50000, helper_stubs=[],
    fixture='Synthetic producer/child, private pools, DAT/supply/AI/region records; real original callees',
    scope='Secondary Train gates/retries, timer/HP progress, creation failure, completion fields/queues, AI assignment',
    limits='Explicit animation, allocation or placement prefixes; no whole game, Windows API, imports or guest host I/O')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path)
    p.add_argument('--execute-original-code',action='store_true')
    a=p.parse_args();raw=a.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=local.TARGET_HASH:p.error('Unknown executable hash')
    if not a.execute_original_code:print(json.dumps(PLAN,indent=2));return
    if not a.out:p.error('Execution requires --out')
    if platform.machine().lower() not in ('x86_64','amd64'):p.error('Use reviewed x86_64 runtime')
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP
    pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
    image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095
    cases=[]
    def put(u,addr,v,n=4):u.mem_write(addr,(v&((1<<(n*8))-1)).to_bytes(n,'little'))
    def get(u,addr,n=4):return int.from_bytes(u.mem_read(addr,n),'little')
    def new(f=None):
        f=f or {};u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,size);u.mem_write(0x400000,image)
        for addr,length in ((UNIT,0x20000),(STACK,0x4000),(STOP,0x1000),(TERRAIN,0x60000)):u.mem_map(addr,length)
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
        visited=[];calls=[];boundary=None;boundary_values={}
        entries={lo for lo,hi in ALLOW}
        def hook(m,addr,width,_):
            nonlocal boundary,boundary_values
            if addr in (STOP,stop_before):
                boundary=addr;sp=m.reg_read(UC_X86_REG_ESP)
                boundary_values=dict(ecx=m.reg_read(UC_X86_REG_ECX),edx=m.reg_read(UC_X86_REG_EDX),
                    stack=[get(m,sp+4+i*4) for i in range(6)])
                m.emu_stop();return
            if not any(lo<=addr and addr+width<=hi for lo,hi in ALLOW):raise RuntimeError(f'Outside reviewed original code {addr:#x}')
            visited.append(addr)
            if addr in entries:
                sp=m.reg_read(UC_X86_REG_ESP)
                calls.append(dict(callee=hex(addr),ecx=m.reg_read(UC_X86_REG_ECX),edx=m.reg_read(UC_X86_REG_EDX),
                    stack=[get(m,sp+4+i*4) for i in range(6)]))
        h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0x3000
        u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*(x&0xFFFFFFFF for x in args)))
        u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
        u.emu_start(start,STOP+1,count=50000);u.hook_del(h)
        if boundary is None:raise RuntimeError('No return/boundary within instruction bound')
        return dict(complete_return=boundary==STOP,stop_va=hex(boundary),return_eax=u.reg_read(UC_X86_REG_EAX),
            instruction_count=len(visited),boundary_registers_arguments=boundary_values,calls=calls,
            visited_functions=[hex(lo) for lo,hi in ALLOW if any(lo<=x<hi for x in visited)])
    def record(name,inputs,actual,expected,ex):
        assert actual==expected,(name,inputs,actual,expected)
        cases.append(dict(case=name,inputs=inputs,actual=actual,expected=expected,passed=True,execution=ex))
    def state(u):return dict(secondary=get(u,UNIT+0xA6,1),state=get(u,UNIT+0xE2,1),building=get(u,UNIT+0xEC),
        slot=get(u,UNIT+0xA4,1),queue=list(struct.unpack('<5H',u.mem_read(UNIT+0x98,10))),
        child_hp=get(u,CHILD+8),child_timer=get(u,CHILD+0xAC,2),child_status=get(u,CHILD+0xDC),
        error=get(u,0x5DA398,2),stocks=[get(u,0x5263D0+12),get(u,0x526400+12)])
    # Unsigned 32-bit numerator, 16-bit storage and zero-to-one correction.
    for hp in (0,1,9,10,11,255,256,25600,0xFFFFFFFF):
        for time in (0,1,2,10,65535):
            u=new();put(u,0x520708,hp);put(u,0x5238B0,time,2)
            ex=call(u,0x427D90,CHILD)
            gain=(((hp-hp//10+time-1)&0xFFFFFFFF)//time)&65535 if time else 0
            record(f'initialize_hp_gain_{hp}_{time}',dict(max_hp_fixed=hp,dat_build_time=time),
                get(u,CHILD+0xA8,2),gain or 1,ex)
    for shields in (0,1,10,100,255,256,65535):
        for time in (0,1,2,10,65535):
            for enabled,grounded in ((False,False),(False,True),(True,False),(True,True)):
                u=new();put(u,0x520458,int(enabled),1);put(u,0x520540,shields,2)
                put(u,0x5238B0,time,2);put(u,CHILD+0xDC,2 if grounded else 0)
                put(u,CHILD+0x60,12345);put(u,CHILD+0xAA,54321,2)
                ex=call(u,0x427DE0,CHILD)
                fixed=shields*256;initial=fixed//10
                gain=(((fixed-initial)//time)&65535) if time else 0
                expected=[initial,gain or 1] if enabled and grounded else [12345,54321]
                record(f'initialize_shield_gain_{shields}_{time}_{enabled}_{grounded}',
                    dict(max_shields=shields,dat_build_time=time,shields_enabled=enabled,grounded=grounded),
                    [get(u,CHILD+0x60),get(u,CHILD+0xAA,2)],expected,ex)
    # Raw multiplier; callers that MOVZX AX afterward deliberately truncate.
    for cwal in (False,True):
        for gain in (0,1,127,128,255,256,257,4095,4096,65535):
            u=new(dict(hp_gain=gain,cwal=cwal));ex=call(u,0x41B290,CHILD)
            record(f'hp_gain_{cwal}_{gain}',dict(hp_gain=gain,cwal=cwal),ex['return_eax'],gain*(16 if cwal else 1),ex)
    # Actual HP setter and decrement chain, without reaching finish at timer0.
    for cwal in (False,True):
        for timer in (1,2,15,16,17,32767,32768,65535):
            for hp in (1000,25599,25600):
                u=new(dict(cwal=cwal,timer=timer));put(u,CHILD+8,hp)
                ex=call(u,0x41A280,CHILD,args=(257,1))
                remaining=timer-16 if cwal else timer-1
                if cwal:remaining=remaining&65535;remaining=0 if remaining&32768 else remaining
                record(f'progress_timer_{cwal}_{timer}_{hp}',dict(timer=timer,hp=hp,hp_gain_argument=257,cwal=cwal),
                    [get(u,CHILD+8),get(u,CHILD+0xAC,2),get(u,CHILD+0xDC),ex['return_eax']],
                    [min(hp+257,25600),remaining,0,1],ex)
    # Disabled, Zerg and unknown state gates; no queue mutation occurs.
    for state_id in (0,1,2,3,255):
        for reason in ('status','lockdown','stasis','maelstrom','zerg'):
            f=dict(state=state_id);u=new(f)
            if reason=='status':put(u,UNIT+0xDC,0x403)
            elif reason=='zerg':put(u,0x523C40+111,1,1)
            else:put(u,UNIT+dict(lockdown=0x117,stasis=0x119,maelstrom=0x124)[reason],1,1)
            before=state(u);ex=call(u,0x439310)
            record(f'train_disabled_{state_id}_{reason}',dict(state=state_id,reason=reason),state(u),before,ex)
    for state_id in (3,4,255):
        u=new(dict(state=state_id));before=state(u);ex=call(u,0x439310)
        record(f'train_unknown_state_{state_id}',dict(state=state_id),state(u),before,ex)
    for state_id in (0,1):
        for kind in ('producer_not_capable','supply','unit_cap','no_unit_pool','outside_map'):
            u=new(dict(state=state_id,unit_count=1700 if kind=='unit_cap' else 0))
            if kind=='producer_not_capable':put(u,UNIT+0xDC,1)
            if kind=='supply':put(u,0x529514+12,0)
            if kind=='outside_map':put(u,SPRITE+0x14,0,2)
            ex=call(u,0x439310)
            record(f'train_start_failure_{state_id}_{kind}',dict(state=state_id,failure=kind,owner=3,viewer=0),
                [get(u,UNIT+0xE2,1),get(u,UNIT+0xEC),get(u,0x665B38),get(u,0x5DA398,2),get(u,UNIT+0xA4,1),get(u,UNIT+0x98,2),get(u,0x5263D0+12)],
                [1,0,1700 if kind=='unit_cap' else 0,0,0,0,1000],ex)
    for slot in range(5):
        for ai in (False,True):
            q=[228]*5;u=new(dict(slot=slot,queue=q,ai=ai,state=1));ex=call(u,0x439310)
            record(f'train_empty_{slot}_{ai}',dict(slot=slot,queue=q,ai=ai),
                [get(u,UNIT+0xA6,1),get(u,UNIT+0xE2,1),get(u,UNIT+0xEC)],[23,0,0],ex)
        u=new(dict(slot=slot,state=2));ex=call(u,0x439310)
        record(f'train_missing_child_{slot}',dict(slot=slot,state=2,current_build=0),
            [get(u,UNIT+0xE2,1),get(u,UNIT+0xA4,1),get(u,UNIT+0x98,2),get(u,0x6AAAFC),get(u,0x6AB3A0,1),get(u,0x6AAB30,1)],
            [0,slot,0,1,1,1],ex)
    # Actual Train calls through HP gain and ProgressBuild.
    for cwal in (False,True):
        for timer in (1,2,16,17):
            for gain in (1,257,4096,65535):
                u=new(dict(state=2,current_build=CHILD,timer=timer,hp_gain=gain,cwal=cwal))
                ex=call(u,0x439310)
                hp_delta=(gain*(16 if cwal else 1))&65535
                remaining=max(timer-(16 if cwal else 1),0)
                record(f'train_progress_{cwal}_{timer}_{gain}',dict(timer=timer,hp_gain=gain,cwal=cwal),
                    [get(u,CHILD+8),get(u,CHILD+0xAC,2),get(u,UNIT+0xE2,1),get(u,UNIT+0xEC),get(u,UNIT+0xA4,1)],
                    [min(1000+hp_delta,25600),remaining,2,CHILD,0],ex)
    # The normal hidden child's finish invokes original spatial placement.
    # Creation failure returns include real RNG, bounds, SSE initialization and
    # a genuinely exhausted original sprite pool rather than a stubbed result.
    for count in (0,1699,1700,1701):
        for pool in (0,NODE):
            u=new(dict(unit_count=count,free_unit=pool))
            if pool:u.mem_write(NODE+8,bytes([0xA5])*0x148)
            ex=call(u,0x424900,ecx=0,edx=200,args=(200,3))
            record(f'create_failure_count_{count}_pool_{pool}',dict(unit_count=count,free_unit=pool,sprite_free_head=0,unit_type=0),
                [ex['return_eax'],get(u,0x665B38),get(u,0x665AF0),get(u,0x5DA398,2),get(u,0x673940+14*4)],
                [0,count,pool,61 if count==1700 else 62,1],ex)
            if pool and count!=1700:
                record(f'create_sse_reset_{count}',dict(previous_record_fill=165,unit_pool=pool,sprite_pool=0),
                    [get(u,NODE+0x74),get(u,NODE+0x84),get(u,NODE+0xDC),get(u,NODE+0x134),get(u,NODE+0xAE,2),get(u,NODE+0xC),get(u,0x543300)],
                    [0,0,0,0,1,0,0xABCDEF],ex)
    for x,y,okay in ((8,8,1),(7,8,0),(8,7,0),(4087,4087,1),(4088,4087,0),(4087,4088,0),(-1,200,0),(200,-1,0)):
        u=new();ex=call(u,0x424D30,ecx=0,edx=x,args=(y,))
        record(f'create_bounds_{x}_{y}',dict(x=x,y=y,extent=8,map_size=4096),ex['return_eax'],okay,ex)
    # Real sprite allocation rolls back after the real image pool is exhausted.
    for length in (1,2,3):
        u=new(dict(free_unit=NODE));nodes=[NODE+0x200+i*0x40 for i in range(length)]
        put(u,0x5C51C8,nodes[0]);put(u,0x5C51CC,nodes[-1]);put(u,0x544694,0)
        for i,node in enumerate(nodes):put(u,node,nodes[i-1] if i else 0);put(u,node+4,nodes[i+1] if i+1<length else 0)
        put(u,0x51F248,1,1);put(u,0x51FAA8,0,1);put(u,0x510AE0,0,2)
        ex=call(u,0x424900,ecx=0,edx=200,args=(200,3))
        chain=[];at=get(u,0x5C51C8)
        while at:chain.append(at);at=get(u,at+4);assert len(chain)<=length
        expected=nodes if length==1 else [nodes[1],nodes[0],*nodes[2:]]
        record(f'create_image_pool_failure_sprite_rollback_{length}',
            dict(unit_free_head=NODE,sprite_free_chain=nodes,image_free_head=0,visible_sprite_dat_flag=1),
            [ex['return_eax'],get(u,0x665AF0),get(u,0x665B38),get(u,0x5DA398,2),chain,get(u,0x5C51CC)],
            [0,NODE,0,62,expected,expected[-1]],ex)
    for cursor in range(5):
        for offset in range(5):
            for ai in (False,True):
                q=[10,11,12,13,14];u=new(dict(slot=cursor,queue=q,ai=ai))
                for uid in q:put(u,0x523520+uid*2,50,2);put(u,0x5236E8+uid*2,25,2);put(u,0x521660+uid*4,0)
                ex=call(u,0x41B810,args=(offset,))
                newq=q.copy();tags=list(range(1,6));values=[0x11110000+i for i in range(5)]
                at=(cursor+offset)%5
                for i in range(4-offset):
                    nextslot=(at+1)%5;newq[at]=newq[nextslot]
                    if ai:tags[at]=tags[nextslot];values[at]=values[nextslot]
                    at=nextslot
                newq[at]=228
                record(f'cancel_queue_cursor_{cursor}_offset_{offset}_ai_{ai}',dict(cursor=cursor,relative_cancel_slot=offset,queue=q,ai=ai,current_build=0),
                    [list(struct.unpack('<5H',u.mem_read(UNIT+0x98,10))),[get(u,AI+9+i,1) for i in range(5)],
                     [get(u,AI+0x18+i*4) for i in range(5)],get(u,UNIT+0xA4,1),get(u,0x5263D0+12),get(u,0x526400+12)],
                    [newq,tags,values,cursor,1050,1025],ex)
    for offset in (5,255,0xFFFFFFFF):
        u=new(dict(ai=True,queue=[10,11,12,13,14]));before=state(u)
        ex=call(u,0x41B810,args=(offset,))
        record(f'cancel_reject_unsigned_offset_{offset}',dict(relative_slot=offset),state(u),before,ex)
    for sparse in (False,True):
        for building in (False,True):
            q=[10,11,228,13,14] if sparse else [10,11,12,13,14]
            u=new(dict(ai=True,queue=q));put(u,0x523520+20,50,2);put(u,0x5236E8+20,25,2)
            put(u,0x521660+40,int(building));ex=call(u,0x41B810,args=(0,))
            record(f'cancel_sparse_{sparse}_building_{building}',dict(queue=q,cancel_type_building=building),
                [list(struct.unpack('<5H',u.mem_read(UNIT+0x98,10))),[get(u,AI+9+i,1) for i in range(5)],
                 get(u,0x5263D0+12),get(u,0x526400+12)],
                [[11,228,228,13,14] if sparse else [11,12,13,14,228],
                 [2,3,3,4,5] if sparse else [2,3,4,5,5],1000 if building else 1050,1000 if building else 1025],ex)
    u=new(dict(queue=[228]*5,ai=True));before=state(u);ex=call(u,0x41B810,args=(0,))
    record('cancel_empty_slot_noop',dict(queue=[228]*5),state(u),before,ex)
    for completed in (False,True):
        u=new(dict(ai=True,current_build=CHILD,queue=[10,11,12,13,14],child_status=int(completed)))
        put(u,0x523520,50,2);put(u,0x5236E8,25,2)
        ex=call(u,0x41B810,args=(0,),stop_before=None if completed else 0x42A4D0)
        record(f'cancel_current_child_completed_{completed}',dict(child_completed=completed,stop_before=None if completed else '0x42a4d0'),
            [get(u,0x5263D0+12),get(u,0x526400+12),get(u,UNIT+0xEC),list(struct.unpack('<5H',u.mem_read(UNIT+0x98,10)))],
            [1000 if completed else 1050,1000 if completed else 1025,CHILD,[11,12,13,14,228] if completed else [10,11,12,13,14]],ex)
    for kind,uid,worker in (('SCV',7,True),('Probe',64,True),('Drone',41,True),('Overlord',42,False)):
        for producer_ai in (False,True):
            for available in (False,True):
                u=new(dict(ai=producer_ai,player_type=1,child_id=uid))
                put(u,0x521660+uid*4,8 if worker else 0);put(u,AI+0x14,TOWN);put(u,TOWN+8,AREA)
                put(u,TOWN+0x10,AREA);put(u,TOWN+0x18,3,1)
                put(u,0x67D280+3*0x4E8,0x20,2)
                put(u,AREA+(0x5DC0 if worker else 0xABE0),NODE if available else 0)
                ex=call(u,0x48D320,edx=CHILD)
                accepted=producer_ai and available
                record(f'new_{kind}_ai_{producer_ai}_pool_{available}',dict(child_type=uid,producer_ai=producer_ai,ai_pool_available=available,campaign=True),
                    [get(u,CHILD+0x134),get(u,NODE+8,1),get(u,NODE+0x10),get(u,NODE+0x14),get(u,TOWN+0x1A,1)],
                    [NODE if accepted else 0,(2 if worker else 3) if accepted else 0,CHILD if accepted else 0,TOWN if accepted else 0,int(accepted and worker)],ex)
    # The same unit can finish a morph: detach its BuildingAI before reassigning.
    for kind,uid,worker in (('Drone',41,True),('Overlord',42,False)):
        for available in (False,True):
            u=new(dict(ai=True,player_type=1,producer_id=uid,status=1 if worker else 5))
            put(u,0x521660+uid*4,8 if worker else 0);put(u,AI+0x10,UNIT)
            put(u,AI+0x14,TOWN);put(u,TOWN+8,AREA);put(u,TOWN+0x10,AREA)
            put(u,TOWN+0x14,AI);put(u,TOWN+0x18,3,1)
            put(u,TOWN+0x24,UNIT);put(u,TOWN+0x28,UNIT)
            put(u,0x67D280+3*0x4E8,0x20,2)
            put(u,AREA+(0x5DC0 if worker else 0xABE0),NODE if available else 0)
            ex=call(u,0x48D320,edx=UNIT)
            chosen=NODE if worker and available else AI if not worker else 0
            record(f'finished_morph_{kind}_pool_{available}',dict(unit_type=uid,same_unit=True,worker_pool_available=available),
                [get(u,UNIT+0x134),get(u,TOWN+0x24),get(u,TOWN+0x28),get(u,TOWN+0x1A,1),
                 get(u,TOWN+0x14),get(u,AREA+0xABE0)],
                [chosen,0,0,int(worker and available),0 if worker else AI,AI if worker else NODE if available else 0],ex)
    for producer in (None,0,106,111,113,114,130,131,132,133,154,160,167,155):
        for relation in ('position','same_owner','other_owner','self','zero_x'):
            u=new(dict(producer_id=producer or 0));put(u,UNIT+0xF8,321,2);put(u,UNIT+0xFA,0,2)
            put(u,UNIT+0xFC,TARGET if relation in ('same_owner','other_owner') else UNIT if relation=='self' else 0)
            if relation=='other_owner':put(u,TARGET+0x4C,4,1)
            if relation=='zero_x':put(u,UNIT+0xF8,0,2)
            ex=call(u,0x41BD70,CHILD,args=(UNIT if producer is not None else 0,))
            valid=producer in (106,111,113,114,130,131,132,133,154,160,167,155) and relation not in ('self','zero_x')
            order=49 if valid and relation=='same_owner' else 6 if valid else 3
            xy=[200,200] if valid and relation=='same_owner' else [321,0] if valid else [0,0]
            record(f'rally_{producer}_{relation}',dict(producer_type=producer,rally_relation=relation,rally_xy=[0 if relation=='zero_x' else 321,0]),
                [get(u,CHILD+0x4D,1),get(u,CHILD+0x58,2),get(u,CHILD+0x5A,2),get(u,CHILD+0x5C)],
                [order,*xy,TARGET if valid and relation=='same_owner' else 0],ex)
    for cwal in (False,True):
        u=new(dict(state=2,current_build=CHILD,timer=0,cwal=cwal,hidden=True))
        ex=call(u,0x439310,stop_before=0x47DC30)
        record(f'normal_hidden_completion_placement_prefix_{cwal}',dict(timer=0,hidden=True,cwal=cwal),
            [get(u,CHILD+8),get(u,CHILD+0xAC,2),get(u,CHILD+0xDC),get(u,UNIT+0xE2,1),ex['boundary_registers_arguments']['ecx']],
            [1000+257*(16 if cwal else 1),0,0,2,CHILD],ex)
    # Complete ordinary finish for explicitly already-visible child profiles.
    for slot in range(5):
        for ai in (False,True):
            q=[10,11,12,13,14];q[slot]=0
            u=new(dict(state=2,current_build=CHILD,timer=0,slot=slot,queue=q,ai=ai))
            ex=call(u,0x439310)
            expected=q.copy();expected[slot]=228
            record(f'visible_child_finish_{slot}_{ai}',dict(timer=0,hidden=False,slot=slot,queue=q,producer_ai=ai,player_type=2),
                [get(u,CHILD+0xDC),get(u,CHILD+0x4D,1),get(u,UNIT+0xE2,1),get(u,UNIT+0xEC),get(u,UNIT+0xA4,1),list(struct.unpack('<5H',u.mem_read(UNIT+0x98,10))),
                 get(u,AI+9+slot,1),get(u,AI+0x18+slot*4),get(u,0x52C0C4+3*4),get(u,0x5290F4+3*4)],
                [1,3,0,0,(slot+1)%5,expected,0 if ai else 1+slot,0 if ai else 0x11110000+slot,1,1],ex)
    # AI completion reads current producer slot unless child type equals producer.
    for slot in range(5):
        for same_type in (False,True):
            u=new(dict(slot=slot,ai=True,player_type=1,child_id=111 if same_type else 0))
            chosen=0 if same_type else slot
            put(u,AI+9+chosen,2,1);put(u,AI+0x18+chosen*4,GUARD)
            put(u,GUARD+0x16,321,2);put(u,GUARD+0x18,654,2)
            ex=call(u,0x48D320,edx=CHILD)
            record(f'finished_guard_ai_{slot}_{same_type}',dict(slot=slot,same_type=same_type,tag=2,guard_parent=0),
                [get(u,CHILD+0x134),get(u,GUARD+0xC),get(u,GUARD+0x12,2),get(u,GUARD+0x14,2),get(u,AI+9+chosen,1)],
                [GUARD,CHILD,321,654,2],ex)
    # Private military-node allocation: real list mutation, then explicit order dependency.
    for flying in (False,True):
        u=new(dict(ai=True,player_type=1));put(u,AI+9,1,1);put(u,AI+0x18,REGION)
        put(u,AREA+0x4E20,NODE);put(u,CHILD+0xDC,5 if flying else 1)
        ex=call(u,0x48D320,edx=CHILD,stop_before=0x4A9B20)
        record(f'finished_military_ai_order_prefix_{flying}',dict(tag=1,flying=flying,region_state=0,military_free_head=NODE),
            [get(u,CHILD+0x134),get(u,REGION+0x30),get(u,AREA+0x4E20),get(u,NODE+8,1),get(u,NODE+0xC),get(u,NODE+0x10),get(u,REGION+0xA,2)],
            [NODE,NODE,0,4,CHILD,REGION,0 if flying else 1],ex)
    for tag in (1,2,3,5,8):
        u=new(dict(ai=True,player_type=1));put(u,AI+9,tag,1);put(u,AI+0x18,REGION if tag==1 else GUARD)
        if tag==2:put(u,GUARD+0xC,TARGET)
        if tag==2:
            ex=call(u,0x48D320,edx=CHILD)
            record('occupied_guard_actual_region_fallback',dict(tag=2,guard_parent=TARGET,region=0,military_pool=0),
                [get(u,AI+9,1),get(u,AI+0x18),get(u,CHILD+0x134)],[1,REGION,0],ex)
        else:
            ex=call(u,0x48D320,edx=CHILD)
            record(f'finished_ai_empty_pool_{tag}',dict(tag=tag,military_pool=0,guard_pool=0),get(u,CHILD+0x134),0,ex)
    result=dict(target_sha256=local.TARGET_HASH,target_version='1.17.0.1',original_code_executed=True,
        engine=f'Unicorn {unicorn.__version__}',host_python_arch=platform.machine(),probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        extractor_dependencies={Path(m.__file__).name:hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in (local,primary,unit_helpers,admission,regions)},
        helper_stubs=[],limits=PLAN,cases=cases,passed=len(cases),failed=0,
        complete_returns=sum(x['execution']['complete_return'] for x in cases))
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('passed','failed','complete_returns')}))

if __name__=='__main__':main()
