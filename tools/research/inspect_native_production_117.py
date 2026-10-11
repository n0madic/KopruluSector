#!/usr/bin/env python3
"""Inspect reviewed 1.17 production routines; optionally run isolated native code.

The default only disassembles bytes. Explicit execution maps a private PE image,
fixtures and stack in Unicorn. No helper stubs, WinMain, Windows APIs or full game
are permitted. Consumer cases stop at a declared checkpoint before its tail.
"""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct

import capstone
import pefile

TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
PLAYER_BASE, PLAYER_STRIDE = 0x67D068, 0x4E8
WITNESSES = {
    "town_request_consumer": (0x4865B0, 0x4869ED),
    "town_request_to_spending": (0x482B60, 0x482BBF),
    "take_training_request": (0x482BE0, 0x482C07),
    "accumulate_spending_cost": (0x4836D0, 0x483744),
    "unit_tech_upgrade_costs": (0x408790, 0x40885E),
    "upgrade_gas_mineral_formula": (0x43B270, 0x43B2C9),
    "technology_state": (0x49A420, 0x49A43F),
    "upgrade_level": (0x49A470, 0x49A496),
    "unit_count_queued": (0x41BDE0, 0x41BF66),
    "cancel_relative_queue_slot": (0x41B810, 0x41B8F3),
    "copy_training_ai_slot": (0x484F90, 0x484FBC),
    "full_unit_cost_refund": (0x408460, 0x408485),
    "protoss_building_target_predicate": (0x42F240, 0x42F268),
    "addon_target_predicate": (0x435990, 0x4359BE),
    "technology_satisfied_or_in_progress": (0x4863B0, 0x486408),
    "dead_or_spriteless_predicate": (0x41FE40, 0x41FE55),
    "racial_worker_id": (0x486070, 0x486095),
    "campaign_predicate": (0x4831E0, 0x4831F1),
    "upgrade_ready_flag": (0x483650, 0x483674),
    "player_unit_list": (0x4256F0, 0x4256F8),
    "raw_player_unit_count": (0x4143C0, 0x4143F4),
    "spending_heap_push": (0x482040, 0x482113),
    "spending_unit_limit": (0x4835E0, 0x48364E),
}

CONTRACTS = {
    "consumer": "ECX=player. Persistent 100x4 town records at town+0x3c. Stop scan at first flags&0xf8==0. Unit count scopes and morph equivalences documented separately. Emit one cost+heap request per deficit; do not consume persistent record. Queue cleanup calls relative positions1,2,3,4 in increasing order before counting. Isolated prefix ends before0x48686d tail.",
    "cost": "Unit costs are DAT u16 minerals/gas and u8 supply; property0x400 doubles only supply with byte wrap. Tech costs u16. Upgrade base+current_level*factor is truncated to u16 by0x408820. Demand accumulation at PlayerAi+0/+4/+8 wraps u32 and precedes heap insertion.",
    "satisfaction": "kind0 scans town worker/building lists, or raw all-player table for special bit0. kind1 counts current upgrade level plus every nondead/nonspriteless matching Upgrade order. Other kinds use researched-or-matching-Research predicate. Unit requests alone are skipped while town+0x1e!=0.",
    "priority": "Campaign missing upgrade/tech requests use shared local decrement across this entire player pass; decrement increments only when <= authored priority. Missing kind3 tech has no such adjustment. Noncampaign racial-main-building request is forced to priority150 when signed minerals<600.",
    "dependency_boundary": "Request insertion, cost/heap conversion, queue satisfaction and cancellation are distinct from execution of build/train/research orders. Helpers0x486480,0x485ab0,0x485970,0x4ae220,0x4b1420 are not executed by these consumer fixtures. Cancel index0 with currentBuildUnit invokes unrecovered0x41a340 and is not covered.",
}


def isolated_probes(data, pe):
    if platform.machine().lower() not in ("x86_64", "amd64"):
        raise RuntimeError("Use x86_64 Rosetta; arm64 Unicorn mem_map crashed in runtime validation")
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP

    scratch, stack, stop = 0x800000, 0x900000, 0xA00000
    unit, target, town, worker_ai, worker, main, building_ai, building = [scratch + i * 0x400 for i in range(8)]
    base, image = pe.OPTIONAL_HEADER.ImageBase, pe.get_memory_mapped_image()
    size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    allowed = list(WITNESSES.values())
    cases = []

    def w32(uc, a, v): uc.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))
    def w16(uc, a, v): uc.mem_write(a, struct.pack("<H", v & 0xFFFF))
    def w8(uc, a, v): uc.mem_write(a, bytes([v & 255]))
    def r32(uc, a): return struct.unpack("<I", uc.mem_read(a, 4))[0]
    def r16(uc, a): return struct.unpack("<H", uc.mem_read(a, 2))[0]
    def r8(uc, a): return uc.mem_read(a, 1)[0]

    def new():
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, size)
        uc.mem_write(base, image)
        uc.mem_map(scratch, 0x8000)
        uc.mem_map(stack, 0x2000)
        uc.mem_map(stop, 0x1000)
        # File-mapped DAT storage is not a game-initialized table. Fixtures set
        # every relevant scalar and linked-list head independently.
        uc.mem_write(PLAYER_BASE, bytes(PLAYER_STRIDE * 8))
        uc.mem_write(0x665B08, bytes(4 * 12))
        uc.mem_write(0x69B51C, bytes(8 * 8))
        uc.mem_write(0x534224, bytes(24 * 12))
        uc.mem_write(0x536420, bytes(20 * 12))
        uc.mem_write(0x534590, bytes(46 * 12))
        uc.mem_write(0x53660C, bytes(15 * 12))
        uc.mem_write(0x529604, bytes(228 * 12 * 4))
        return uc

    def call(uc, start, ecx=0, edx=0, args=(), checkpoint=None):
        reached, count, trace = False, 0, []
        def hook(machine, address, ins_size, _):
            nonlocal reached, count
            if address == stop or address == checkpoint:
                reached = True
                machine.emu_stop()
                return
            if not any(lo <= address and address + ins_size <= hi for lo, hi in allowed):
                raise RuntimeError(f"Instruction outside reviewed allowlist: {address:#x}")
            count += 1
            if address in (0x482B60, 0x41B810):
                sp = machine.reg_read(UC_X86_REG_ESP)
                n = 3 if address == 0x482B60 else 1
                trace.append({"callee": hex(address), "ecx": machine.reg_read(UC_X86_REG_ECX),
                              "edx": machine.reg_read(UC_X86_REG_EDX),
                              "stack_arguments": [r32(machine, sp + 4 + j*4) for j in range(n)]})
        handle = uc.hook_add(UC_HOOK_CODE, hook)
        sp = stack + 0x1800
        uc.mem_write(sp, struct.pack("<" + "I" * (len(args) + 1), stop, *args))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        uc.reg_write(UC_X86_REG_EDX, edx)
        uc.emu_start(start, stop + 1, count=100000)
        uc.hook_del(handle)
        if not reached:
            raise RuntimeError("Probe failed to reach declared return/checkpoint within100000 instructions")
        return uc.reg_read(UC_X86_REG_EAX), count, trace

    def observe(kind, inputs, got, expected, execution, scope="complete reviewed function for fixture path"):
        if got != expected:
            raise AssertionError((kind, inputs, got, expected))
        _, count, trace = execution
        cases.append({"kind": kind, "fixture_inputs": inputs, "observed": got,
                      "expected_from_static_contract": expected, "original_instructions": count,
                      "native_calls": trace, "execution_scope": scope, "helper_stubs": [], "matched": True})

    def set_unit(uc, ptr, ty, queue=None, head=0, order=0, props=0, owner=0):
        w16(uc, ptr + 0x64, ty); w8(uc, ptr + 0xA4, head)
        w8(uc, ptr + 0x4D, order); w8(uc, ptr + 0x4C, owner)
        w32(uc, 0x521660 + 4*ty, props)
        uc.mem_write(ptr + 0x98, struct.pack("<5H", *(queue if queue is not None else [228]*5)))

    def unit_cost(uc, ty, minerals=10, gas=20, supply=2, props=0):
        w16(uc, 0x523520 + 2*ty, minerals); w16(uc, 0x5236E8 + 2*ty, gas)
        w8(uc, 0x523E10 + ty, supply); w32(uc, 0x521660 + 4*ty, props)

    def upgrade_cost(uc, ty, level=0, player=0, mineral_base=50, mineral_factor=25, gas_base=75, gas_factor=35):
        level_address = (0x534590 + player*46 + ty) if ty < 46 else (0x5365DE + player*15 + ty)
        w8(uc, level_address, level)
        for addr, value in [(0x5248D0, mineral_base), (0x524950, mineral_factor), (0x5249D0, gas_base), (0x524A50, gas_factor)]:
            w16(uc, addr + ty*2, value)

    for props, supply, initial in [(0, 2, 0), (0x400, 2, 0), (0x400, 128, 0), (0x400, 255, 0), (0, 255, 0xFFFFFFFA)]:
        uc = new(); unit_cost(uc, 1, 10, 20, supply, props)
        for j in range(3): w32(uc, PLAYER_BASE + j*4, initial)
        ex = call(uc, 0x4836D0, 0, 3, [1])
        got = [r32(uc, PLAYER_BASE + j*4) for j in range(3)]
        effective_supply = (supply * (2 if props & 0x400 else 1)) & 255
        observe("unit demand cost", {"properties": props, "supply_byte": supply, "initial_each": initial}, got,
                [(initial+v)&0xFFFFFFFF for v in [10,20,effective_supply]], ex)
    for ty in (0, 23, 24, 43):
        uc = new(); w16(uc, 0x51F760 + ty*2, 65535); w16(uc, 0x51F7B8 + ty*2, 321)
        ex = call(uc, 0x4836D0, 7, 6, [ty]); p = PLAYER_BASE + 7*PLAYER_STRIDE
        observe("technology demand cost", {"technology": ty, "player": 7}, [r32(uc,p+j*4) for j in range(3)], [65535,321,0], ex)
    for ty in (0, 45, 46, 60):
        for level in (0, 1, 3, 255):
            uc = new(); upgrade_cost(uc, ty, level, 7, 65000, 1000, 50000, 2000)
            ex = call(uc, 0x4836D0, 7, 5, [ty]); p = PLAYER_BASE + 7*PLAYER_STRIDE
            observe("upgrade demand cost u16 truncation", {"upgrade":ty,"player":7,"current_level":level},
                    [r32(uc,p+j*4) for j in range(3)], [(65000+level*1000)&65535,(50000+level*2000)&65535,0], ex)

    for town_kind, properties, spending_kind, limit in [(0,0,3,0),(0,2,7,0),(0,8,4,0),(0,8,4,255),(1,0,5,0),(2,0,6,0),(3,0,6,0)]:
        uc=new();unit_cost(uc,1,10,20,2,properties);upgrade_cost(uc,1)
        w16(uc,0x51F760+2,30);w16(uc,0x51F7B8+2,40);w8(uc,PLAYER_BASE+0x3F0+1,limit)
        ex=call(uc,0x482B60,0,town,[town_kind,1,90])
        accepted=not(spending_kind==4 and limit==255)
        demand=[50,75,0] if spending_kind==5 else [30,40,0] if spending_kind==6 else [10,20,2]
        got={"demand":[r32(uc,PLAYER_BASE+j*4) for j in range(3)],"heap_count":r8(uc,PLAYER_BASE+0x210),
             "entry":[r8(uc,PLAYER_BASE+0x18),r8(uc,PLAYER_BASE+0x19),r16(uc,PLAYER_BASE+0x1A),r32(uc,PLAYER_BASE+0x1C)]}
        observe("town-to-spending kind mapping and cost-before-limit",{"town_kind":town_kind,"properties":properties,"limit":limit},got,
                {"demand":demand,"heap_count":int(accepted),"entry":[90,spending_kind,1,town] if accepted else [0,0,0,0]},ex)

    for head in range(5):
        for relative in range(5):
            uc = new(); q = [228]*5; q[(head+relative)%5] = 106
            set_unit(uc,unit,111,q,head)
            ex = call(uc,0x41BDE0,unit,0,[106])
            observe("ordinary queue all positions", {"head":head,"matching_relative":relative}, ex[0],1,ex)
    for ty, order in [(36,0),(59,0),(97,0),(111,0x2B),(111,0x2D)]:
        for relative in (0,1):
            uc = new(); q=[228]*5;q[(2+relative)%5]=106;set_unit(uc,unit,ty,q,2,order)
            ex=call(uc,0x41BDE0,unit,0,[106])
            observe("egg or building morph head only", {"type":ty,"order":order,"match_relative":relative},ex[0],int(relative==0),ex)
    for order in (0x19,0x1A,0x1E,0x1F,0x46,0x21,0x22,0,2):
        for existing in (False,True):
            uc=new();set_unit(uc,unit,7,[106,106,106,106,106],0,order,8)
            w32(uc,unit+0x5C,target if existing else 0);w8(uc,unit+0xDC,2)
            ex=call(uc,0x41BDE0,unit,0,[106])
            eligible = order in (0x19,0x1A,0x1E,0x1F,0x46)
            excludes = existing and order in (0x1E,0x1F,0x21,0x22)
            observe("worker queued construction", {"order":order,"existing_target":existing},ex[0],int(eligible and not excludes),ex)
    for secondary in (0x25,0x26,0x3F,0):
        for own_status, target_status in [(0,0),(2,0),(2,1)]:
            uc=new();set_unit(uc,unit,7,[106]*5,0,0x19,8)
            w8(uc,unit+0xA6,secondary);w8(uc,unit+0xDC,own_status)
            w32(uc,unit+0xEC,target);w8(uc,target+0xDC,target_status)
            ex=call(uc,0x41BDE0,unit,0,[106])
            excludes = secondary in (0x26,0x3F) or (secondary==0x25 and own_status&2 and not target_status&1)
            observe("worker addon target exclusion", {"secondary_order":secondary,"own_status":own_status,"target_status":target_status},ex[0],int(not excludes),ex)
    uc=new();set_unit(uc,unit,111,[106]*5,4);ex=call(uc,0x41BDE0,unit,0,[106]);observe("ordinary five matches",{},ex[0],5,ex)

    def relative_queue(q, head): return [q[(head+j)%5] for j in range(5)]
    for head in range(5):
        for relative in (1,2,3,4,5):
            uc=new();q=[0]*5
            for j in range(5): q[(head+j)%5]=j
            set_unit(uc,unit,111,q,head,owner=7);w32(uc,unit+0x134,building_ai);w8(uc,building_ai+8,3)
            for j in range(5):
                unit_cost(uc,j,10+j,20+j);w8(uc,building_ai+9+j,30+j);w32(uc,building_ai+0x18+4*j,100+j)
            w32(uc,0x5263D0+28,0);w32(uc,0x526400+28,0)
            expectedq=q[:];expectedtags=list(range(30,35));expectedptrs=list(range(100,105))
            if relative<=4:
                pos=(head+relative)%5
                for j in range(4-relative):
                    nxt=(pos+1)%5;expectedq[pos]=expectedq[nxt];expectedtags[pos]=expectedtags[nxt];expectedptrs[pos]=expectedptrs[nxt];pos=nxt
                expectedq[pos]=228
            ex=call(uc,0x41B810,unit,0,[relative])
            got={"queue_relative":relative_queue(list(struct.unpack("<5H",uc.mem_read(unit+0x98,10))),head),
                 "ai_tags_physical":list(uc.mem_read(building_ai+9,5)),"ai_contexts_physical":[r32(uc,building_ai+0x18+j*4) for j in range(5)],
                 "refund":[r32(uc,0x5263D0+28),r32(uc,0x526400+28)]}
            observe("cancel circular slot and copy AI metadata",{"head":head,"relative":relative},got,
                    {"queue_relative":relative_queue(expectedq,head),"ai_tags_physical":expectedtags,"ai_contexts_physical":expectedptrs,
                     "refund":[10+relative,20+relative] if relative<=4 else [0,0]},ex)
    for properties in (0,1):
        uc=new();set_unit(uc,unit,111,[1,1,228,228,228]);unit_cost(uc,1,50,25,2,properties)
        w32(uc,0x5263D0,0xFFFFFFF0);w32(uc,0x526400,0)
        ex=call(uc,0x41B810,unit,0,[1]);observe("cancel refund building property and u32 wrap",{"properties":properties},
                    [r32(uc,0x5263D0),r32(uc,0x526400)], [0xFFFFFFF0,0] if properties&1 else [34,25],ex)

    for researched, active, sprite, order in [(0,False,0,0),(1,False,0,0),(0,True,1,0x4B),(0,True,0,0x4B),(0,True,1,0),(0,True,1,0x4C)]:
        uc=new();w8(uc,town+0x18,7);w8(uc,0x536408+7*20+24,researched)
        if active:
            w32(uc,0x665B08+28,unit);w32(uc,unit+0xC,target if sprite else 0);w8(uc,unit+0x4D,order);w8(uc,unit+0xC8,24)
        ex=call(uc,0x4863B0,town,24);observe("technology researched or matching active order",{"researched":researched,"active":active,"sprite":sprite,"order":order},
                    ex[0],int(bool(researched or (active and sprite and order==0x4B))),ex)

    def consumer_fixture(requests, campaign=False, battle=False, worker_limit=4, minerals=1000, building_type=None, queue=None, worker_order=0x1E, existing_worker_target=True):
        uc=new();w32(uc,0x69B51C,town);w8(uc,0x6726D1,1)
        w32(uc,town+0xC,worker_ai);w32(uc,worker_ai+0x10,worker);w8(uc,town+0x1A,1);w8(uc,town+0x1B,worker_limit)
        w32(uc,town+0x24,main);set_unit(uc,main,106);set_unit(uc,worker,7,[106]*5,0,worker_order,8)
        if existing_worker_target:w32(uc,worker+0x5C,main)
        w8(uc,town+0x1E,int(battle));w16(uc,PLAYER_BASE+0x218,0x20 if campaign else 0)
        w32(uc,0x5263D0,minerals);w32(uc,0x526400,0)
        if building_type is not None:
            w32(uc,town+0x14,building_ai);w32(uc,building_ai+0x10,building);w8(uc,building_ai+8,3)
            w32(uc,building+0x134,building_ai);set_unit(uc,building,building_type,queue)
        for j,(kind,ty,desired,priority,special) in enumerate(requests):
            uc.mem_write(town+0x3C+j*4,struct.pack("<BBH",(desired<<3)|(kind<<1)|special,priority,ty))
            if kind==1:upgrade_cost(uc,ty)
            elif kind in (2,3):w16(uc,0x51F760+ty*2,50);w16(uc,0x51F7B8+ty*2,75)
            else:unit_cost(uc,ty,props=8 if ty==7 else 0)
        return uc

    def consumer_observed(uc, execution):
        calls=[c["stack_arguments"] for c in execution[2] if c["callee"]=="0x482b60"]
        return {"emissions_kind_id_priority":calls,"demand":[r32(uc,PLAYER_BASE+j*4) for j in range(3)],"flags":r16(uc,PLAYER_BASE+0x218),"heap_count":r8(uc,PLAYER_BASE+0x210)}

    def consumer_case(name, requests, emissions, demand, expected_flag=0x400, **fixture):
        uc=consumer_fixture(requests,**fixture);ex=call(uc,0x4865B0,0,checkpoint=0x48686D)
        observe(name,{"requests_kind_id_count_priority_special":requests,**fixture},consumer_observed(uc,ex),
                {"emissions_kind_id_priority":emissions,"demand":demand,"flags":expected_flag,"heap_count":len(emissions)},ex,
                "original consumer prefix through all fixture town requests; stopped before0x48686d tail")
        return uc,ex

    consumer_case("unit deficit emits per missing unit",[(0,1,3,90,0)],[[0,1,90]]*3,[30,60,6])
    consumer_case("unit paused in battle",[(0,1,3,90,0)],[],[0,0,0],battle=True)
    consumer_case("upgrade bypasses battle",[(1,0,2,90,0)],[[1,0,90]]*2,[100,150,0],battle=True)
    consumer_case("technology bypasses battle",[(2,0,1,90,0)],[[2,0,90]],[50,75,0],battle=True)
    consumer_case("worker count clamped to town limit",[(0,7,6,90,0)],[[0,7,90]]*3,[30,60,6])
    consumer_case("campaign shared priority decrement",[(1,0,3,100,0),(2,1,1,100,0)],[[1,0,100],[1,0,99],[1,0,98],[2,1,97]],[200,300,0],expected_flag=0x20,campaign=True)
    consumer_case("campaign zero priority clamps decrement",[(1,0,4,0,0),(2,1,1,100,0)],[[1,0,0]]*4+[[2,1,99]],[250,375,0],expected_flag=0x20,campaign=True)
    consumer_case("kind3 tech lacks campaign decrement",[(3,0,2,100,0)],[[3,0,100]]*2,[100,150,0],expected_flag=0x420,campaign=True)
    for minerals,priority,campaign in [(599,50,False),(599,255,False),(600,50,False),(0xFFFFFFFF,255,False),(599,50,True)]:
        emitted = priority if campaign or (minerals<0x80000000 and minerals>=600) else 150
        consumer_case("noncampaign main building priority150 signed threshold",[(0,106,1,priority,0)],[[0,106,emitted]],[10,20,2],
                      expected_flag=0x420 if campaign else 0x400,minerals=minerals,campaign=campaign)
    consumer_case("first zero count terminates town scan",[(0,1,0,90,0),(0,1,3,90,0)],[],[0,0,0])
    for requested,actual in [(131,132),(131,133),(132,133),(141,137),(143,146),(143,144),(132,131),(141,141),(143,143)]:
        satisfied = requested==actual or (requested,actual) in [(131,132),(131,133),(132,133),(141,137),(143,146),(143,144)]
        consumer_case("town building morph satisfaction",[(0,requested,1,90,0)],[] if satisfied else [[0,requested,90]],[0,0,0] if satisfied else [10,20,2],building_type=actual)
    for special in (0,1):
        uc=consumer_fixture([(0,1,3,90,special)]);w32(uc,0x529604+1*12*4,3)
        ex=call(uc,0x4865B0,0,checkpoint=0x48686D)
        observe("player_need global count scope",{"special_bit":special,"raw_global_count":3},consumer_observed(uc,ex),
                {"emissions_kind_id_priority":[] if special else [[0,1,90]]*3,"demand":[0,0,0] if special else [30,60,6],"flags":0x400,"heap_count":0 if special else 3},ex,
                "consumer prefix before0x48686d")
    uc,ex=consumer_case("cleanup increasing indices preserves alternate queue entries",[(0,0,5,90,0)],[[0,0,90]]*2,[20,40,4],building_type=111,queue=[0]*5)
    observe("cleanup survivor queue and full refunds",{}, {"queue":list(struct.unpack("<5H",uc.mem_read(building+0x98,10))),"resources":[r32(uc,0x5263D0),r32(uc,0x526400)],
            "cancel_indices":[x["stack_arguments"][0] for x in ex[2] if x["callee"]=="0x41b810"]},
            {"queue":[0,0,0,228,228],"resources":[1020,40],"cancel_indices":[1,2,3,4]},ex,"consumer prefix before0x48686d")
    for sprite,order,upgrade_id in [(1,0x4C,0),(0,0x4C,0),(1,0,0),(1,0x4C,1)]:
        uc=consumer_fixture([(1,0,2,90,0)]);w32(uc,0x665B08,unit);w32(uc,unit+0xC,target if sprite else 0);w8(uc,unit+0x4D,order);w8(uc,unit+0xC9,upgrade_id)
        ex=call(uc,0x4865B0,0,checkpoint=0x48686D);missing=1 if sprite and order==0x4C and upgrade_id==0 else 2
        observe("upgrade in progress contributes one",{"sprite":sprite,"order":order,"upgrade_id":upgrade_id},consumer_observed(uc,ex),
                {"emissions_kind_id_priority":[[1,0,90]]*missing,"demand":[50*missing,75*missing,0],"flags":0x400,"heap_count":missing},ex,"consumer prefix before0x48686d")
    # Two towns share the pass-local upgrade decrement, not a per-town counter.
    uc=consumer_fixture([(1,0,1,90,0)],campaign=True)
    second=scratch+0x3000;second_ai=scratch+0x3400;second_worker=scratch+0x3800
    w32(uc,town,second);w32(uc,second+0xC,second_ai);w32(uc,second_ai+0x10,second_worker)
    set_unit(uc,second_worker,7,[228]*5,0,0,8);w32(uc,second+0x24,main)
    w8(uc,second+0x1A,1);w8(uc,second+0x1B,4)
    uc.mem_write(second+0x3C,struct.pack("<BBH",(1<<3)|2,90,0))
    ex=call(uc,0x4865B0,0,checkpoint=0x48686D)
    observe("campaign decrement persists across towns",{"town_count":2},consumer_observed(uc,ex),
            {"emissions_kind_id_priority":[[1,0,90],[1,0,89]],"demand":[100,150,0],"flags":0x20,"heap_count":2},ex,"consumer prefix before0x48686d")
    uc=consumer_fixture([(0,1,1,90,0)]);before=bytes(uc.mem_read(town+0x3C,4))
    first=call(uc,0x4865B0,0,checkpoint=0x48686D);ex=call(uc,0x4865B0,0,checkpoint=0x48686D)
    got=consumer_observed(uc,ex);got["persistent_record_unchanged"]=bytes(uc.mem_read(town+0x3C,4))==before
    observe("consumer itself preserves request and does not reset demand",{"consecutive_calls":2},got,
            {"emissions_kind_id_priority":[[0,1,90]],"demand":[20,40,4],"flags":0x400,"heap_count":2,"persistent_record_unchanged":True},ex,"two consumer prefixes before0x48686d; outer queue reset excluded")
    for stored in (0,1,228,65535):
        uc=new();w16(uc,PLAYER_BASE+0x220,stored);ex=call(uc,0x482BE0,0,target)
        observe("training request take and clear",{"stored_unit_plus_one":stored},{"return":ex[0],"output":r32(uc,target),"stored":r16(uc,PLAYER_BASE+0x220)},
                {"return":int(stored!=0),"output":stored-1 if stored else 0,"stored":0},ex)

    return {"format_version":1,"target_sha256":hashlib.sha256(data).hexdigest(),"probe_script_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "original_code_executed":True,"complete_game_executed":False,"helper_stubs":[],
            "runtime_versions":{"python":platform.python_version(),"unicorn":unicorn.__version__,"capstone":capstone.__version__,"pefile":pefile.__version__,"architecture":platform.machine(),"system":platform.system()},
            "allowlisted_preferred_va_ranges":[[hex(a),hex(b)] for a,b in allowed],"case_count":len(cases),"all_matched":True,"cases":cases}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe",type=Path,required=True)
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--execute-isolated-original-code",action="store_true")
    parser.add_argument("--probe-out",type=Path)
    args=parser.parse_args()
    if args.execute_isolated_original_code and args.probe_out is None:parser.error("--probe-out required for explicit execution")
    data=args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest()!=TARGET_HASH:parser.error("EXE hash differs from reviewed1.17 artifact")
    pe=pefile.PE(data=data);base=pe.OPTIONAL_HEADER.ImageBase
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    result={"format_version":1,"target_sha256":TARGET_HASH,"inspector_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "preferred_image_base":hex(base),"original_code_executed":False,"complete_game_executed":False,"reviewed_contracts":CONTRACTS,"witnesses":{}}
    for name,(a,b) in WITNESSES.items():
        raw=pe.get_data(a-base,b-a)
        result["witnesses"][name]={"start_preferred_va":hex(a),"end_exclusive_preferred_va":hex(b),"bytes_sha256":hashlib.sha256(raw).hexdigest(),
            "instructions":[{"address":hex(i.address),"bytes":i.bytes.hex(),"mnemonic":i.mnemonic,"operands":i.op_str} for i in md.disasm(raw,a)]}
    result["raw_switch_tables"]={"building_morph_count":{"table_va":"0x4869f0","targets":[hex(x) for x in struct.unpack("<5I",pe.get_data(0x4869F0-base,20))],
        "index_va":"0x486a04","indices_for_ids131_to143":list(pe.get_data(0x486A04-base,13))},"heap_type_table":{"table_va":"0x482114","bytes":pe.get_data(0x482114-base,16).hex()}}
    args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(result,indent=2)+"\n")
    if args.execute_isolated_original_code:
        probes=isolated_probes(data,pe);args.probe_out.parent.mkdir(parents=True,exist_ok=True);args.probe_out.write_text(json.dumps(probes,indent=2)+"\n")
        print(f"{probes['case_count']} isolated original production cases matched; no helper stubs")
    else: print(f"Wrote {len(WITNESSES)} static witnesses; original code not executed")


if __name__=="__main__":main()
