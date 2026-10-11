#!/usr/bin/env python3
"""Execute isolated exact-hash primary-order functions with real order pools.

Default mode prints a plan. Explicit execution runs only allowlisted original
x86 instructions in private memory, with no helper stubs, Windows process,
imports, host I/O or network inside the guest. Animation-producing paths stop
before the original iscript callee. Ordinary animation-zero fixtures complete
the real animation-reset chain.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import pefile
from inspect_native_primary_orders_117 import TARGET_HASH, BLOCKS, ORDERS_HASH, stock_orders

UNIT,OTHER,SUB,SPRITE,IMAGE,AI,TBL,POOL=0x800000,0x800200,0x800400,0x806000,0x806100,0x804000,0x809000,0x808000
STACK,STOP=0x900000,0xA00000
ALLOW=list(BLOCKS.values())
PLAN={"target_sha256":TARGET_HASH,"instruction_bound":30000,"replacement_helpers":"None",
    "scope":"Original order allocation/free, list mutations, issuers, queue pruning/activation, Cloak, burrow/lock gates and subunit inheritance",
    "fixture":"Private 20-byte order pool and artificial unit/DAT/image/TBL records",
    "animation":"Complete native idle-reset path for main image animation 0; explicit stop before iscript for active animations",
    "host_limits":"No PE process, imports, game loop, Windows API, host-file or network guest calls"}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe",type=Path,required=True);p.add_argument("--out",type=Path)
    p.add_argument("--orders-dat",type=Path,required=True)
    p.add_argument("--execute-original-code",action="store_true")
    a=p.parse_args();raw=a.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=TARGET_HASH:p.error("Unknown executable hash")
    try:stock=stock_orders(a.orders_dat.read_bytes())
    except ValueError as error:p.error(str(error))
    if not a.execute_original_code:print(json.dumps(PLAN,indent=2));return
    if a.out is None:p.error("Execution requires --out")
    import unicorn
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP
    pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
    image=pe.get_memory_mapped_image();image_size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095
    cases=[]
    def put(uc,addr,value,n=4):uc.mem_write(addr,(value&((1<<(8*n))-1)).to_bytes(n,"little"))
    def get(uc,addr,n=4):return int.from_bytes(uc.mem_read(addr,n),"little")
    def new(f=None):
        f=f or {};uc=Uc(UC_ARCH_X86,UC_MODE_32);uc.mem_map(0x400000,image_size);uc.mem_write(0x400000,image)
        for addr,length in ((UNIT,0x10000),(STACK,4096),(STOP,4096)):uc.mem_map(addr,length)
        for addr,uid in ((UNIT,f.get("unit_id",15)),(OTHER,16),(SUB,17)):
            put(uc,addr+0xC,SPRITE);put(uc,addr+0x64,uid,2);put(uc,addr+0x4C,3,1)
            put(uc,addr+0x4D,f.get("current_order",3),1);put(uc,addr+0x4E,7,1)
            put(uc,addr+0x50,17,2);put(uc,addr+0x58,10,2);put(uc,addr+0x5A,20,2)
            put(uc,addr+0xDC,f.get("status",1));put(uc,addr+0x85,15,1);put(uc,addr+0xE3,23,1)
            put(uc,addr+0x93,0x82,1);put(uc,addr+0x20,15,1);put(uc,addr+0x7C,OTHER)
            put(uc,0x521660+uid*4,0);put(uc,0x520F20+uid,3,1)
            put(uc,0x521008+uid,10,1);put(uc,0x5210F0+uid,14,1)
        put(uc,SPRITE+0x18,IMAGE);put(uc,SPRITE+0x1C,IMAGE);put(uc,SPRITE+0x20,IMAGE)
        put(uc,SPRITE+0x14,300,2);put(uc,SPRITE+0x16,400,2);put(uc,SPRITE+0xE,255,1)
        put(uc,IMAGE+0x16,f.get("animation",0),1)
        if f.get("ai"):put(uc,UNIT+0x134,AI)
        uc.mem_write(0x51B200,bytes([1])*189);uc.mem_write(0x51B980,bytes([255])*378)
        uc.mem_write(0x51B080,bytes(189))
        put(uc,0x543300,0xABCDEF);put(uc,0x543304,0x234567)
        put(uc,0x6AB4F8,TBL);put(uc,TBL,0,2);put(uc,0x508D50,0)
        pool_count=f.get("pool_count",16)
        put(uc,0x5AEF00,POOL if pool_count else 0);put(uc,0x5AEF04,POOL+(pool_count-1)*20 if pool_count else 0)
        put(uc,0x5AEF08,0)
        for i in range(pool_count):
            put(uc,POOL+i*20,POOL+(i-1)*20 if i else 0)
            put(uc,POOL+i*20+4,POOL+(i+1)*20 if i+1<pool_count else 0)
        return uc
    def call(uc,start,ecx=UNIT,edx=0,argv=(),stop_before=None):
        visited=[];boundary=None;calls=[];boundary_args=[]
        entries={lo for lo,hi in ALLOW}
        def hook(machine,addr,width,_):
            nonlocal boundary,boundary_args
            if addr in (STOP,stop_before):
                boundary=addr
                if addr!=STOP:
                    sp=machine.reg_read(UC_X86_REG_ESP)
                    boundary_args=[get(machine,sp+4+i*4) for i in range(5)]
                machine.emu_stop();return
            if not any(lo<=addr and addr+width<=hi for lo,hi in ALLOW):raise RuntimeError(f"Outside reviewed original code: {addr:#x}")
            visited.append(addr)
            if addr in entries:
                sp=machine.reg_read(UC_X86_REG_ESP)
                calls.append({"callee":hex(addr),"ecx":hex(machine.reg_read(UC_X86_REG_ECX)),"edx":machine.reg_read(UC_X86_REG_EDX),
                    "stack_arguments":[get(machine,sp+4+i*4) for i in range(5)]})
        h=uc.hook_add(UC_HOOK_CODE,hook);sp=STACK+0x800
        uc.mem_write(sp,struct.pack("<"+"I"*(len(argv)+1),STOP,*(x&0xFFFFFFFF for x in argv)))
        uc.reg_write(UC_X86_REG_ESP,sp);uc.reg_write(UC_X86_REG_ECX,ecx);uc.reg_write(UC_X86_REG_EDX,edx)
        uc.emu_start(start,STOP+1,count=30000);uc.hook_del(h)
        if boundary is None:raise RuntimeError("Instruction bound reached without reviewed boundary")
        return {"complete_return":boundary==STOP,"stop_va":hex(boundary),"instruction_count":len(visited),
            "return_eax":uc.reg_read(UC_X86_REG_EAX),"boundary_ecx":hex(uc.reg_read(UC_X86_REG_ECX)),"boundary_stack_arguments":boundary_args,"calls":calls,
            "pool_after":{"free_head":hex(get(uc,0x5AEF00)),"free_tail":hex(get(uc,0x5AEF04)),"allocated_count":get(uc,0x5AEF08)},
            "unit_queue_after":{"head":hex(get(uc,UNIT+0x74)),"tail":hex(get(uc,UNIT+0x78))},
            "visited_functions":[hex(lo) for lo,hi in ALLOW if any(lo<=addr<hi for addr in visited)]}
    def record(label,inputs,actual,expected,execution):
        assert actual==expected,(label,inputs,actual,expected)
        cases.append({"case":label,"inputs":inputs,"actual":actual,"expected":expected,"passed":True,"execution":execution})
    def linked(uc,head,tail):
        result=[];node=head;prev=0
        while node:
            assert node not in result and len(result)<64,"Cycle or oversized list"
            assert get(uc,node)==prev,("Bad previous link",hex(node),hex(get(uc,node)),hex(prev))
            result.append(node);prev=node;node=get(uc,node+4)
        assert prev==tail,("Bad tail",hex(prev),hex(tail))
        return result
    def queue(uc,unit=UNIT):
        return [{"id":get(uc,node+8,1),"fow":get(uc,node+10,2),"position":get(uc,node+12),"target":get(uc,node+16)}
            for node in linked(uc,get(uc,unit+0x74),get(uc,unit+0x78))]
    def counts(uc):
        free=linked(uc,get(uc,0x5AEF00),get(uc,0x5AEF04))
        return [len(free),get(uc,0x5AEF08)]
    def state(uc,unit=UNIT):
        return {"order":get(uc,unit+0x4D,1),"state":get(uc,unit+0x4E,1),"fow":get(uc,unit+0x50,2),
            "x":get(uc,unit+0x58,2),"y":get(uc,unit+0x5A,2),"target":get(uc,unit+0x5C),
            "status":get(uc,unit+0xDC),"highlight":get(uc,unit+0x84,1),"wait":get(uc,unit+0x85,1),
            "move_update_timer":get(uc,unit+0xE3,1),"user_flags":get(uc,unit+0x93,1),"auto_target":get(uc,unit+0x7C),
            "flingy_flags":get(uc,unit+0x20,1),"sprite_flags":get(uc,SPRITE+0xE,1)}
    def active(order=10,x=0,y=0,fow=228,target=0,status=1,ai=False,highlight=0):
        return dict(order=order,state=0,fow=fow,x=x&65535,y=y&65535,target=target,status=status,
            highlight=highlight,wait=0,move_update_timer=0,user_flags=0x82,auto_target=OTHER if ai else 0,flingy_flags=7,sprite_flags=127)
    def enqueue(uc,orders):
        for order in orders:
            call(uc,0x4295D0,argv=(order,0,0,228,0))
    # All valid order bytes with a normal non-burrowed target fixture.
    for order in range(189):
        uc=new();interruptible=int(order!=109);put(uc,0x51B200+order,interruptible,1)
        ex=call(uc,0x42A2C0,argv=(order,))
        extra=[{"id":3,"fow":17,"position":10|(20<<16),"target":0}] if order==109 else []
        record(f"issue_no_target_{order}",dict(order=order,current_order=3,pool_count=16,interruptible=interruptible,highlight=65535),
            [state(uc),queue(uc),counts(uc),get(uc,0x543300),get(uc,0x543304)],
            [active(order=order,status=1 if interruptible else 0x1001),extra,[16-len(extra),len(extra)],0xABCDEF,0x234567],ex)
    for row in stock:
        uc=new()
        for field in stock:
            put(uc,0x51B200+field['id'],field['interruptible'],1)
            put(uc,0x51B980+field['id']*2,field['highlight'],2)
            put(uc,0x51B080+field['id'],field['subunit_inheritance'],1)
        order=row['id'];ex=call(uc,0x42A2C0,argv=(order,))
        # Retail Cloak is interruptible, so the outer second activation restores
        # the saved order in this animation-zero fixture.
        expect=active(order=3,x=10,y=20,fow=17) if order==109 else active(order=order,status=1 if row['interruptible'] else 0x1001)
        record(f"stock_flags_issue_no_target_{order}",dict(row,orders_dat_sha256=ORDERS_HASH,current_order=3,main_image_animation=0),
            [state(uc),queue(uc),counts(uc)],[expect,[],[16,0]],ex)
    for x,y in ((0,0),(123,456),(-1,-32768),(65536,65537)):
        for start,argv,fow in ((0x42A340,(10,x,y),228),(0x42A300,(10,65,x,y),65)):
            uc=new();ex=call(uc,start,argv=argv)
            record(f"position_{start:x}_{x}_{y}",dict(entry=hex(start),x=x,y=y,fow=fow),[state(uc),queue(uc),counts(uc)],
                [active(x=x,y=y,fow=fow),[],[16,0]],ex)
    for target in (0,OTHER):
        for start,argv in ((0x42A380,(10,target)),(0x42A3E0,(10,123|(456<<16),target))):
            uc=new();ex=call(uc,start,argv=argv)
            x,y=(300,400) if target else ((0,0) if start==0x42A380 else (123,456))
            record(f"target_{start:x}_{target}",dict(entry=hex(start),target=target,queued_position=[123,456]),
                [state(uc),queue(uc),counts(uc)],[active(x=x,y=y,target=target),[],[16,0]],ex)
    for pool in (0,1,16):
        uc=new({"pool_count":pool});ex=call(uc,0x413C80,ecx=0x1ABC,edx=0x1234FFFF,argv=(0xDEADBEEF,OTHER))
        allocated=ex['return_eax']
        actual=[allocated,counts(uc)]
        expect=[POOL if pool else 0,[pool-1,1] if pool else [0,0]]
        if pool:
            actual += [get(uc,allocated+8,1),get(uc,allocated+10,2),get(uc,allocated+12),get(uc,allocated+16)]
            expect += [0xBC,0xFFFF,0xDEADBEEF,OTHER]
        record(f"allocator_payload_widths_{pool}",dict(pool_count=pool,cl_input=0x1ABC,dx_input=0x1234FFFF,packed_position=0xDEADBEEF,target=OTHER),actual,expect,ex)
        if pool:
            ex=call(uc,0x413D50,ecx=allocated)
            expected_chain=[POOL] if pool==1 else [POOL+20,POOL]+[POOL+i*20 for i in range(2,pool)]
            record(f"allocator_free_roundtrip_{pool}",dict(allocated=allocated,pool_count=pool),
                [counts(uc),linked(uc,get(uc,0x5AEF00),get(uc,0x5AEF04))],[[pool,0],expected_chain],ex)
            ex=call(uc,0x413C80,ecx=10,edx=228,argv=(0,0))
            record(f"allocator_reuse_preserves_existing_head_{pool}",dict(pool_count=pool,free_chain=expected_chain),
                ex['return_eax'],POOL if pool==1 else POOL+20,ex)
    uc=new();put(uc,UNIT+0x54,33,1);put(uc,UNIT+0x4F,6,1)
    ex=call(uc,0x42A2C0,argv=(10,))
    record("activation_preserves_primary_timer_and_secondary_order",dict(main_order_timer=33,secondary_order=6,new_order=10),
        [get(uc,UNIT+0x54,1),get(uc,UNIT+0x4F,1)],[33,6],ex)
    # Highlight counts wrap as bytes; decrements clamp only the 255 result.
    for initial in (0,1,8,254,255):
        for mark in (0,65535):
            uc=new();put(uc,UNIT+0x84,initial,1);put(uc,0x51B980+10*2,mark,2)
            ex=call(uc,0x42A2C0,argv=(10,))
            final=0 if initial==255 else initial
            record(f"highlight_roundtrip_{initial}_{mark}",dict(initial=initial,highlight_descriptor=mark),get(uc,UNIT+0x84,1),final,ex)
    for status in (1,0x1001,0x2001,0x3001,0x08000009):
        for order in (0,10):
            for interruptible in (0,1):
                uc=new({"status":status});put(uc,0x51B200+order,interruptible,1)
                ex=call(uc,0x42A2C0,argv=(order,))
                blocked=bool(status&0x3000 and order!=0)
                expected_order=3 if blocked else order
                expected_status=status if blocked else ((status&0xF7FFDFF7&~0x1000)|(0 if interruptible else 0x1000))
                record(f"activation_lock_{status}_{order}_{interruptible}",dict(status=status,order=order,interruptible=interruptible),
                    [get(uc,UNIT+0x4D,1),get(uc,UNIT+0xDC),get(uc,UNIT+0x93,1),len(queue(uc)),counts(uc)],
                    [expected_order,expected_status,0x83 if blocked else 0x82,int(blocked),[15,1] if blocked else [16,0]],ex)
    # Append differs from issue: queue pruning and allocation occur without activation.
    prune_fixtures=[([10,11,12],[1,1,1],13,1,[]),([10,11,12],[1,0,1],13,1,[10,11]),
        ([10,11,12],[1,1,0],13,1,[10,11,12]),([10,11,12],[1,1,0],12,0,[10,11]),
        ([10,12,12],[0,0,0],12,0,[10]),([10,11,12],[1,1,1],13,0,[10,11,12])]
    for index,(ids,interrupts,order,clear,remaining) in enumerate(prune_fixtures):
        uc=new();enqueue(uc,ids)
        for oid,flag in zip(ids,interrupts):put(uc,0x51B200+oid,flag,1)
        ex=call(uc,0x429F60,argv=(order,123|(456<<16),0,65,clear))
        record(f"tail_pruning_{index}",dict(queue=ids,interruptible=interrupts,new_order=order,clear_others=clear),
            [[x['id'] for x in queue(uc)],counts(uc),get(uc,UNIT+0x4D,1)],
            [remaining+[order],[16-len(remaining)-1,len(remaining)+1],3],ex)
    for size in (0,1,3,8):
        for mark in (0,65535):
            uc=new();put(uc,0x51B980+10*2,mark,2)
            for i in range(size):call(uc,0x4295D0,argv=(10,0,0,228,0))
            ex=call(uc,0x429AC0)
            record(f"clear_queue_{size}_{mark}",dict(queue_size=size,highlight=mark),[queue(uc),counts(uc),get(uc,UNIT+0x84,1)],
                [[],[16,0],0],ex)
    for current in (0,3):
        uc=new({"current_order":current});ex=call(uc,0x42A2C0,argv=(10,))
        record(f"current_death_append_gate_{current}",dict(current_order=current),[get(uc,UNIT+0x4D,1),queue(uc),counts(uc),get(uc,UNIT+0x93,1)],
            [current if current==0 else 10,[],[16,0],0x83 if current==0 else 0x82],ex)
    for pool in (0,1,2):
        uc=new({"pool_count":pool});ex=call(uc,0x42A2C0,argv=(10,))
        record(f"pool_exhaustion_{pool}",dict(pool_count=pool,local_player=0,unit_owner=3),
            [get(uc,UNIT+0x4D,1),queue(uc),counts(uc),get(uc,UNIT+0x93,1)],
            [3 if pool==0 else 10,[],[pool,0],0x83 if pool==0 else 0x82],ex)
    uc=new({"pool_count":0});put(uc,0x508D50,3);put(uc,0x508D48,0);put(uc,0x508D54,0)
    ex=call(uc,0x42A2C0,argv=(10,),stop_before=0x46AE70)
    record("local_pool_exhaustion_notification_prefix",dict(pool_count=0,local_player=3,unit_owner=3,stop_before="0x46ae70"),
        [get(uc,UNIT+0x4D,1),queue(uc),counts(uc),get(uc,UNIT+0x93,1)],[3,[],[0,0],0x83],ex)
    # A surviving older head is activated instead of the newly requested order.
    for pool in (1,16):
        uc=new({"pool_count":pool});enqueue(uc,[10]);put(uc,0x51B200+10,0,1)
        ex=call(uc,0x42A2C0,argv=(13,))
        remaining=[] if pool==1 else [13]
        record(f"retained_head_activation_{pool}",dict(pool_count=pool,old_head=10,head_interruptible=0,new_order=13),
            [get(uc,UNIT+0x4D,1),get(uc,UNIT+0xDC),[x['id'] for x in queue(uc)],counts(uc)],
            [10,0x1001,remaining,[pool-len(remaining),len(remaining)]],ex)
    for status in (0x1001,0x2001):
        uc=new({"status":status});call(uc,0x42A2C0,argv=(10,))
        # Models a later original event releasing the corresponding lock;
        # the activation callee itself remains the unmodified native function.
        put(uc,UNIT+0xDC,1);ex=call(uc,0x42A610)
        record(f"activate_after_released_lock_{status}",dict(previous_status=status,released_status=1,queued_order=10),
            [state(uc),queue(uc),counts(uc)],[active(),[],[16,0]],ex)
    for ai in (False,True):
        for lock in (1,0x1001):
            uc=new(dict(ai=ai,status=lock));ex=call(uc,0x42A5A0)
            order=156 if ai else 3;blocked=bool(lock&0x1000)
            record(f"order_done_empty_queue_{ai}_{lock}",dict(ai=ai,status=lock,human_idle_order=3),
                [get(uc,UNIT+0x4D,1),[x['id'] for x in queue(uc)],counts(uc),get(uc,UNIT+0x93,1)],
                [3 if blocked else order,[order] if blocked else [],[15,1] if blocked else [16,0],0x83 if blocked else 0x82],ex)
    uc=new();ex=call(uc,0x42A610)
    record("activate_empty_queue_is_noop",dict(queue_nodes=0,current_order=3),
        [get(uc,UNIT+0x4D,1),get(uc,UNIT+0x4E,1),get(uc,UNIT+0x85,1),get(uc,UNIT+0xE3,1),get(uc,UNIT+0x93,1),counts(uc)],
        [3,7,15,23,0x82,[16,0]],ex)
    for pool in (0,1,2,16):
        for interruptible in (0,1):
            uc=new({"pool_count":pool});put(uc,0x51B200+109,interruptible,1)
            ex=call(uc,0x42A2C0,argv=(109,))
            cloak_active=pool>=2 and interruptible==0
            final_order=109 if cloak_active else 3
            remaining=[3] if cloak_active else []
            record(f"cloak_pool_{pool}_{interruptible}",dict(pool_count=pool,cloak_interruptible=interruptible,current_order=3),
                [get(uc,UNIT+0x4D,1),get(uc,UNIT+0xDC),[x['id'] for x in queue(uc)],counts(uc)],
                [final_order,0x1001 if cloak_active else 1,remaining,[pool-len(remaining),len(remaining)]],ex)
    for lock in (0x1001,0x2001,0x3001):
        uc=new({"status":lock});put(uc,0x51B200+109,0,1)
        ex=call(uc,0x42A2C0,argv=(109,))
        blocked=bool(lock&0x2000)
        record(f"cloak_lock_{lock}",dict(status=lock,cloak_interruptible=0),
            [get(uc,UNIT+0x4D,1),get(uc,UNIT+0xDC),[x['id'] for x in queue(uc)]],
            [3 if blocked else 109,(lock&~0x1000) if blocked else 0x1001,[109,3] if blocked else [3]],ex)
    for ai in (False,True):
        uc=new({"ai":ai});put(uc,UNIT+0x84,9,1);put(uc,0x51B200+109,0,1)
        ex=call(uc,0x42A2C0,argv=(109,))
        record(f"cloak_prepend_ai_limit_{ai}",dict(ai=ai,highlight_count=9,cloak_interruptible=0),
            [get(uc,UNIT+0x4D,1),[x['id'] for x in queue(uc)],counts(uc)],
            [3 if ai else 109,[] if ai else [3],[16,0] if ai else [15,1]],ex)
    for uid in (13,15,103):
        for ai in (False,True):
            for order in (0,2,10,11,117,118,156):
                uc=new(dict(unit_id=uid,ai=ai,status=17));ex=call(uc,0x42A2C0,argv=(order,))
                inject=ai and order not in (0,117,118,156) and uid!=13 and not(uid==103 and order in (2,11))
                remaining=[order] if inject else []
                record(f"burrow_{uid}_{ai}_{order}",dict(unit_id=uid,ai=ai,status=17,new_order=order),
                    [get(uc,UNIT+0x4D,1),[x['id'] for x in queue(uc)],counts(uc),get(uc,UNIT+0x7C)],
                    [118 if inject else order,remaining,[15,1] if inject else [16,0],OTHER if ai else 0],ex)
    for ai in (False,True):
        for highlighted in (8,9):
            uc=new({"ai":ai});enqueue(uc,[10]);head=get(uc,UNIT+0x74);put(uc,UNIT+0x84,highlighted,1)
            ex=call(uc,0x429E10,argv=(11,0,0,228,head))
            rejected=ai and highlighted>8
            record(f"prepend_ai_highlight_limit_{ai}_{highlighted}",dict(ai=ai,highlight_count=highlighted),
                [[x['id'] for x in queue(uc)],counts(uc)],[[10] if rejected else [11,10],[15,1] if rejected else [14,2]],ex)
    for before in (False,True):
        uc=new();enqueue(uc,[10,11,12]);nodes=linked(uc,get(uc,UNIT+0x74),get(uc,UNIT+0x78))
        entry=0x429E10 if before else 0x4295D0
        ex=call(uc,entry,argv=(13,123|(456<<16),OTHER,65,nodes[1]))
        record(f"insert_middle_{before}",dict(before=before,queue=[10,11,12],new_order=13,anchor=1),
            [[x['id'] for x in queue(uc)],counts(uc)],[[10,13,11,12] if before else [10,11,13,12],[12,4]],ex)
    uc=new({"ai":True});enqueue(uc,[10]*12);put(uc,UNIT+0x84,0,1)
    head=get(uc,UNIT+0x74);ex=call(uc,0x429E10,argv=(11,0,0,228,head))
    record("prepend_ai_limit_counts_highlights_not_nodes",dict(ai=True,queue_nodes=12,highlight_count=0),
        [len(queue(uc)),counts(uc)],[13,[3,13]],ex)
    for kind,order in (("idle",3),("attack_unit",10),("attack_move",14),("inherit",34),("noninherit",35)):
        uc=new();put(uc,UNIT+0x70,SUB);put(uc,0x521660+17*4,0x10)
        put(uc,0x520F20+17,4,1);put(uc,0x521008+17,11,1);put(uc,0x5210F0+17,15,1)
        if kind=="inherit":put(uc,0x51B080+order,1,1)
        ex=call(uc,0x42A340,argv=(order,123,456))
        child_order={"idle":4,"attack_unit":11,"attack_move":15,"inherit":34,"noninherit":3}[kind]
        record(f"subunit_mapping_{kind}",dict(parent_order=order,child_idle=4,child_attack_unit=11,child_attack_move=15),
            [get(uc,UNIT+0x4D,1),get(uc,SUB+0x4D,1),get(uc,SUB+0x58,2),get(uc,SUB+0x5A,2),counts(uc)],
            [order,child_order,10 if kind=="noninherit" else 123,20 if kind=="noninherit" else 456,[16,0]],ex)
    uc=new();put(uc,UNIT+0x70,SUB);put(uc,0x521660+17*4,0x10);put(uc,0x521008+17,11,1)
    ex=call(uc,0x42A380,argv=(10,OTHER))
    record("subunit_target_order_propagation",dict(parent_order=10,target=OTHER,target_position=[300,400],child_attack_order=11),
        [get(uc,SUB+0x4D,1),get(uc,SUB+0x5C),get(uc,SUB+0x58,2),get(uc,SUB+0x5A,2),counts(uc)],
        [11,OTHER,300,400,[16,0]],ex)
    for order in (10,109):
        for animation in (2,3,5,6,7):
            uc=new({"animation":animation});ex=call(uc,0x42A2C0,argv=(order,),stop_before=0x411730)
            record(f"animation_boundary_{order}_{animation}",dict(order=order,main_animation=animation,stop_before="0x411730"),
                [get(uc,UNIT+0x4D,1),get(uc,UNIT+0x4E,1),get(uc,UNIT+0x85,1),get(uc,UNIT+0xE3,1),ex['complete_return'],ex['boundary_stack_arguments'][0]&255],
                [order,0,0,0,False,{2:8,3:9,5:8,6:9,7:12}[animation]],ex)
    result={"target_sha256":TARGET_HASH,"target_version":"1.17.0.1",
        "probe_script_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "extractor_dependency_sha256":hashlib.sha256(Path(__file__).with_name('inspect_native_primary_orders_117.py').read_bytes()).hexdigest(),
        "engine":{"name":"Unicorn","version":unicorn.__version__,"host_arch":platform.machine(),"guest":"x86-32","python":platform.python_version()},
        "original_code_executed":True,"no_game_process_started":True,"replacement_helper_stubs":[],
        "orders_dat_sha256":ORDERS_HASH,"stock_order_field_rows":189,"stock_order_case_count":189,
        "allowlisted_code_ranges":[[hex(lo),hex(hi)] for lo,hi in ALLOW],"scope":PLAN,"cases":cases,"passed":len(cases),"failed":0}
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({"passed":len(cases),"failed":0,"complete_returns":sum(x['execution']['complete_return'] for x in cases)}))

if __name__=='__main__':main()
