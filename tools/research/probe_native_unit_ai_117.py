#!/usr/bin/env python3
"""Bounded isolated unit-AI probes. Default mode only prints a plan.

The explicit execution flag runs reviewed original x86 instructions in private
Unicorn memory, with artificial unit/DAT inputs. No PE process, Windows API,
DLL, host-file operation, or network call is executed by the guest. Helpers are
real instructions. Dispatcher probes stop before the selected external handler.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import pefile

TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
UNIT, OTHER, AI, SPRITE = 0x800000, 0x800200, 0x804000, 0x806000
STACK, STOP, NATIVE_ARRAY = 0x900000, 0xA00000, 0x5DA3A0
ALLOW = [(0x405CF0, 0x405D3D), (0x409C90, 0x409CB9),
         (0x40A180, 0x40A1B8), (0x40B990, 0x40B9AB), (0x40BF00, 0x40BF45),
         (0x40CBD0, 0x40CBF7), (0x41A250, 0x41A27D), (0x41B3D0, 0x41B3E7),
         (0x41DA50, 0x41DB87), (0x41E4A0, 0x41E506), (0x41E700, 0x41E753),
         (0x41FAA0, 0x41FAD1), (0x41FE20, 0x41FE3B),
         (0x41FC20, 0x41FC3C), (0x41FF30, 0x41FF47), (0x420000, 0x420027),
         (0x4200E0, 0x420120), (0x4255F0, 0x4255F6),
         (0x420600, 0x4206D4), (0x420770, 0x420908),
         (0x420AB0, 0x420B43), (0x420F00, 0x420FE8),
         (0x421060, 0x421118), (0x421120, 0x42117F),
         (0x4229A0, 0x422ADD), (0x422C80, 0x422D02),
         (0x428B50, 0x428B9C), (0x438140, 0x43817B),
         (0x438350, 0x438385), (0x439420, 0x4399BD),
         (0x478270, 0x4782C4), (0x49A470, 0x49A496), (0x4A8F30, 0x4A8F56),
         (0x4A9630, 0x4A96A3), (0x4A96B0, 0x4A975A),
         (0x4AF2C0, 0x4AF2F7)]
PLAN = {"target_sha256": TARGET_HASH,
        "functions": ["current strength and cache", "native cargo resolution/space",
                      "guard transition with native ranged RNG", "main-order dispatch prefixes"],
        "stubs": "None",
        "limits": "Private x86-32 emulator; no full game, movement or campaign execution",
        "instruction_bound": 10000,
        "dispatcher_boundary": "Stops before external selected handler, except reviewed no-op help path"}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--execute-original-code", action="store_true")
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Unknown executable hash")
    if not args.execute_original_code:
        print(json.dumps(PLAN, indent=2))
        return
    if args.out is None:
        parser.error("--out is required for execution evidence")
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP
    pe = pefile.PE(data=data)
    assert pe.OPTIONAL_HEADER.ImageBase == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    image = pe.get_memory_mapped_image()
    size = (pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095
    cases = []
    def new():
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(0x400000, size)
        uc.mem_write(0x400000, image)
        for addr, length in ((UNIT, 0x10000), (STACK, 4096), (STOP, 4096)):
            uc.mem_map(addr, length)
        return uc
    def put(uc, addr, value, n=4):
        uc.mem_write(addr, (value&((1<<(n*8))-1)).to_bytes(n, "little"))
    def get(uc, addr, n=4):
        return int.from_bytes(uc.mem_read(addr, n), "little")
    def unit(uc, addr=UNIT, unit_id=15, hp=100*256, status=1):
        put(uc, addr+8, hp)
        put(uc, addr+0xC, SPRITE)
        put(uc, addr+0x64, unit_id, 2)
        put(uc, addr+0xDC, status)
        put(uc, addr+0x4D, 3, 1)
        put(uc, 0x520458+unit_id, 0, 1)
        put(uc, 0x520708+unit_id*4, 100*256)
        put(uc, 0x521660+unit_id*4, 0)
        put(uc, 0x6AC530+unit_id*4, 200)
        put(uc, 0x6AC8C0+unit_id*4, 300)
    def call(uc, start, ecx=UNIT, edx=0, args=(), dispatch_boundary=False, stop_before=None):
        visited, boundary = [], None
        def instruction(machine, addr, width, _):
            nonlocal boundary
            if addr == STOP or addr == stop_before:
                boundary = addr
                machine.emu_stop()
                return
            if not any(lo <= addr and addr+width <= hi for lo, hi in ALLOW):
                if dispatch_boundary:
                    boundary = addr
                    machine.emu_stop()
                    return
                raise RuntimeError(f"Outside reviewed code: {addr:#x}")
            visited.append(addr)
        hook = uc.hook_add(UC_HOOK_CODE, instruction)
        sp = STACK+0x800
        uc.mem_write(sp, struct.pack("<"+"I"*(1+len(args)), STOP, *(x&0xFFFFFFFF for x in args)))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        uc.reg_write(UC_X86_REG_EDX, edx)
        uc.emu_start(start, STOP+1, count=10000)
        uc.hook_del(hook)
        if boundary is None:
            raise RuntimeError("Instruction limit before a reviewed boundary")
        return {"return_eax": uc.reg_read(UC_X86_REG_EAX), "stop_va": hex(boundary),
                "complete_return": boundary == STOP, "instruction_count": len(visited),
                "visited_functions": [hex(lo) for lo, hi in ALLOW if any(lo <= a < hi for a in visited)]}
    def record(label, inputs, actual, expected, execution):
        assert actual == expected, (label, actual, expected)
        cases.append({"case": label, "inputs": inputs, "actual": actual,
                      "expected": expected, "passed": True, "execution": execution})
    strength_inputs = [
        {"label": "full_hp"}, {"label": "half_hp", "hp": 50*256},
        {"label": "ceil_fraction_hp", "hp": 50*256+1},
        {"label": "zero_hp", "hp": 0},
        {"label": "over_max_hp", "hp": 125*256},
        {"label": "shields_floor", "hp": 50*256, "shields": 25*256+255, "max_shields": 50},
        {"label": "shield_disable", "hp": 50*256, "shields": 25*256+255, "max_shields": 50, "shield_enabled": 0},
        {"label": "zero_dat_hp_fallback", "hp": 3*256+1, "max_hp": 0},
        {"label": "zero_dat_and_current_hp", "hp": 0, "max_hp": 0},
        {"label": "spellcaster_energy_odd", "energy": 15*256+255, "properties": 0x200000},
        {"label": "spellcaster_energy_even", "energy": 16*256, "properties": 0x200000},
        {"label": "noncaster_energy", "energy": 250*256},
        {"label": "hallucination_full", "status": 0x40000001},
        {"label": "hallucination_one_fixed_point_damage_rounds_full", "status": 0x40000001, "hp": 100*256-1},
        {"label": "hallucination_whole_hp_damage", "status": 0x40000001, "hp": 99*256},
        {"label": "hallucination_shield_fraction_damage", "status": 0x40000001, "shields": 50*256-1, "max_shields": 50},
        {"label": "hallucination_no_energy_bonus", "status": 0x40000001, "properties": 0x200000, "energy": 200*256},
        {"label": "disabled_still_has_strength", "status": 0x401},
        {"label": "uint32_product_wrap", "base": 0x40000001},
    ] + [{"label": f"zero_form_{uid}", "unit_id": uid} for uid in (35, 36, 59, 97)]
    for fixture in strength_inputs:
        for ground in (0, 1):
            uc = new()
            uid = fixture.get("unit_id", 15)
            hp = fixture.get("hp", 25600)
            status = fixture.get("status", 1)
            unit(uc, unit_id=uid, hp=hp, status=status)
            enabled = fixture.get("shield_enabled", int("max_shields" in fixture))
            max_hp = fixture.get("max_hp", 100)
            max_shield = fixture.get("max_shields", 0)
            shield = fixture.get("shields", 0)
            energy = fixture.get("energy", 0)
            base_score = fixture.get("base", 300 if ground else 200)
            properties = fixture.get("properties", 0)
            put(uc, UNIT+0x60, shield)
            put(uc, UNIT+0xA2, energy, 2)
            put(uc, 0x520458+uid, enabled, 1)
            put(uc, 0x520540+uid*2, max_shield, 2)
            put(uc, 0x520708+uid*4, max_hp*256)
            put(uc, 0x521660+uid*4, properties)
            put(uc, 0x6AC530+(ground*228+uid)*4, base_score)
            life = (hp+255)//256 + (shield//256 if enabled else 0)
            maximum = (max_hp or ((hp+255)//256) or 1) + (max_shield if enabled else 0)
            multiplier = base_score + ((energy>>9) if properties&0x200000 and not status&0x40000000 else 0)
            expected = 0 if uid in (35,36,59,97) or status&0x40000000 and life < maximum else ((life*multiplier)&0xFFFFFFFF)//(maximum or 1)
            execution = call(uc, 0x4A96B0, edx=ground)
            record(f"strength_{fixture['label']}_{ground}", dict(fixture, ground=ground), execution["return_eax"], expected, execution)
    uc = new()
    unit(uc, hp=50*256)
    put(uc, UNIT+0x138, 0xFFFF, 2)
    put(uc, UNIT+0x13A, 0xFFFF, 2)
    execution = call(uc, 0x4A8F30)
    record("cache_refresh_both_directions", {"hp": 12800, "air_base": 200, "ground_base": 300},
           [get(uc, UNIT+0x138, 2), get(uc, UNIT+0x13A, 2)], [100,150], execution)
    def cargo(uc, index, uid=0, generation=3, order=3, state=0, sprite=SPRITE, space=1):
        addr = NATIVE_ARRAY+(index-1)*0x150
        unit(uc, addr, uid)
        put(uc, addr+0xA5, generation, 1)
        put(uc, addr+0x4D, order, 1)
        put(uc, addr+0x4E, state, 1)
        put(uc, addr+0xC, sprite)
        put(uc, 0x523EF8+uid, space, 1)
        return addr, index+(generation<<11)
    for label, values in [
            ("valid", {}), ("wrong_generation", {"generation": 4}),
            ("removed", {"sprite": 0}), ("die_state_one", {"order": 0, "state": 1}),
            ("die_state_zero", {"order": 0}), ("die_state_two", {"order": 0, "state": 2})]:
        uc = new()
        addr, _ = cargo(uc, 2, **values)
        packed = 2+(3<<11)
        execution = call(uc, 0x428B50, ecx=packed)
        expected = 0 if label in ("wrong_generation", "removed", "die_state_one") else addr
        record("cargo_resolve_"+label, dict(values, packed=packed), execution["return_eax"], expected, execution)
    uc = new()
    execution = call(uc, 0x428B50, ecx=0)
    record("cargo_resolve_empty", {"packed": 0}, execution["return_eax"], 0, execution)
    for label, slots in [("empty", []), ("one", [(0,1)]), ("space_two", [(0,2)]),
                         ("eighth_slot", [(7,1)]), ("all_eight", [(i,1) for i in range(8)])]:
        uc = new()
        unit(uc, unit_id=125)
        put(uc, 0x523FE0+125, 4, 1)
        put(uc, 0x6AC530, 200)
        put(uc, 0x6AC8C0, 300)
        for slot, space in slots:
            _, packed = cargo(uc, slot+2, uid=slot, space=space)
            put(uc, UNIT+0xB0+slot*2, packed, 2)
        execution = call(uc, 0x438350)
        used = sum(space for _, space in slots)
        record("cargo_used_"+label, {"slots": slots, "capacity": 4}, execution["return_eax"], used, execution)
        for ground in (0,1):
            execution = call(uc, 0x4A9630, edx=ground)
            record(f"bunker_multiplier_{label}_{ground}", {"slots": slots, "ground": ground}, execution["return_eax"], used*(300 if ground else 200), execution)
        execution = call(uc, 0x438140)
        first_slot = next((slot for slot, _ in slots if slot < 4), None)
        expected = 0 if first_slot is None else NATIVE_ARRAY+(first_slot+1)*0x150
        record("cargo_first_"+label, {"slots": slots, "capacity": 4}, execution["return_eax"], expected, execution)
    guard_cases = [("human", 2, None, 0), ("computer_no_ai", 1, None, 0),
                   ("computer_guard_ai", 1, 1, 0), ("computer_worker_ai", 1, 2, 0),
                   ("computer_building_ai", 1, 3, 0), ("computer_military_ai", 1, 4, 0),
                   ("computer_flyer_no_ai", 1, None, 0x40)]
    for label, player_type, ai_type, properties in guard_cases:
        for enabled in (0,1):
            uc = new()
            unit(uc)
            put(uc, 0x6726D0, player_type, 1)
            put(uc, 0x521660+15*4, properties)
            if ai_type is not None:
                put(uc, UNIT+0x134, AI)
                put(uc, AI+8, ai_type, 1)
            seed = 0xFFFFFFFF
            put(uc, 0x673930, enabled)
            put(uc, 0x67393C, seed)
            execution = call(uc, 0x41E700)
            final_seed = (seed*22695477+1)&0xFFFFFFFF if enabled else seed
            raw = (final_seed>>16)&32767 if enabled else 0
            expected_order = 3 if player_type != 1 or ai_type is None and properties&0x40 else (160 if ai_type == 1 else 156)
            record(f"guard_{label}_rng_{enabled}", {"player_type": player_type, "ai_type": ai_type, "properties": properties, "rng_enabled": enabled, "seed": seed},
                   [get(uc, UNIT+0x4D,1), get(uc, UNIT+0x54,1), get(uc, 0x67393C), get(uc, 0x673938), get(uc, 0x673940+29*4)],
                   [expected_order, (raw*16)>>15, final_seed, enabled, enabled], execution)
    # Compare each numeric order's path against the native switch-table data.
    def table_target(table, mapping, order, low):
        off = pe.get_offset_from_rva(mapping-0x400000)
        ordinal = data[off+order-low]
        off = pe.get_offset_from_rva(table-0x400000)+ordinal*4
        return struct.unpack_from("<I", data, off)[0]
    def external_transfer(case):
        # The Nothing case only prepares a queued order when +0x74 is nonzero.
        # This fixture has an empty queue; its conditional branch returns.
        if case == 0x43969B:
            return STOP
        import capstone
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        off = pe.get_offset_from_rva(case-0x400000)
        for ins in md.disasm(data[off:off+64], case):
            if ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
                dest = int(ins.op_str,16)
                if not 0x439420 <= dest < 0x4399BC:
                    return dest
            if ins.mnemonic == "ret":
                return STOP
        raise RuntimeError(f"Unresolved dispatcher case {case:#x}")
    for order in range(188):
        uc = new()
        unit(uc)
        put(uc, UNIT+0x4D, order, 1)
        put(uc, UNIT+0x85, 0, 1)
        put(uc, UNIT+0x92, 0xA5, 1)
        special = table_target(0x4399C0,0x4399D4,order,0) if order <= 174 else 0x439454
        every = table_target(0x439A84,0x439B18,order,4) if 4 <= order <= 183 else 0x439640
        throttled = table_target(0x439BCC,0x439D5C,order,1) if 1 <= order <= 187 else 0x4399BB
        chosen = special if special != 0x439454 else every if every != 0x439640 else throttled
        boundary = external_transfer(chosen)
        execution = call(uc, 0x439420, dispatch_boundary=True, stop_before=boundary)
        periodic = special == 0x439454 and every == 0x439640
        record(f"dispatch_order_{order}", {"order": order, "old_throttle": 0, "old_reservation": 0xA5, "expected_case_va": hex(chosen)},
               [execution["stop_va"], get(uc,UNIT+0x85,1),get(uc,UNIT+0x92,1)],
               [hex(boundary),8 if periodic else 0,0 if periodic else 0xA5],execution)
    for throttle in (1,2,8,255):
        uc = new(); unit(uc)
        put(uc, UNIT+0x4D, 14,1); put(uc,UNIT+0x85,throttle,1); put(uc,UNIT+0x92,0xA5,1)
        execution = call(uc,0x439420)
        record(f"dispatch_throttle_{throttle}", {"order":14,"old_throttle":throttle,"reservation":0xA5},
               [get(uc,UNIT+0x85,1),get(uc,UNIT+0x92,1)], [throttle-1,0xA5], execution)
    for owner in (0,1,7,8):
        for visible in (0,255):
            uc = new(); unit(uc); unit(uc,OTHER,16)
            put(uc,OTHER+0xC,SPRITE+0x200)
            put(uc,UNIT+0x4C,owner,1); put(uc,SPRITE+0x200+0xC,visible,1)
            execution = call(uc,0x422C80,args=(OTHER,))
            record(f"visibility_owner_{owner}_mask_{visible}", {"owner":owner,"sprite_visibility":visible},
                   execution["return_eax"], visible&((1<<owner)&255),execution)
    for same, immobile in ((True,False),(True,True),(False,False),(False,True)):
        uc = new(); unit(uc,status=1|(0x80000 if immobile else 0))
        put(uc,SPRITE+0x14,100,2); put(uc,SPRITE+0x16,120,2)
        put(uc,UNIT+0x10,100 if same else 101,2); put(uc,UNIT+0x12,120,2)
        execution=call(uc,0x41A250)
        record(f"movement_state_same_{same}_immobile_{immobile}", {"same_position":same,"status_bit_80000":immobile},
               execution["return_eax"], (2 if immobile else 1) if same else 0,execution)
    for uid, weapon, upgrade, extra in ((0,1,16,32),(38,1,30,32),(66,1,33,64),
                                      (3,8,54,96),(4,8,54,96),(3,1,54,0),
                                      (17,10,None,96),(18,10,None,96),(78,1,None,64)):
        for enabled in (0,1):
            uc=new(); unit(uc,unit_id=uid)
            put(uc,0x5254B8+weapon*4,128)
            put(uc,0x536720,enabled,1)
            if upgrade is not None:
                address=0x534590+upgrade if upgrade < 46 else 0x5365DE+upgrade
                put(uc,address,enabled,1)
            execution=call(uc,0x420F00,args=(weapon,OTHER))
            record(f"weapon_range_{uid}_{weapon}_{enabled}", {"unit_id":uid,"weapon":weapon,"upgrade_or_expansion":enabled},
                   execution["return_eax"],128+(extra if enabled else 0),execution)
    def combat_fixture(uc, target_x=100, target_y=0, box=0, max_range=64, min_range=0):
        unit(uc); unit(uc,OTHER,16)
        put(uc,OTHER+0xC,SPRITE+0x200)
        put(uc,SPRITE+0x200+0xC,1,1)
        put(uc,SPRITE+0x200+0x14,target_x,2);put(uc,SPRITE+0x200+0x16,target_y,2)
        put(uc,UNIT+0x5C,OTHER)
        put(uc,0x5211D8+15,1,1);put(uc,0x5213A8+15,1,1)
        put(uc,0x5254B8+4,max_range);put(uc,0x5252B0+4,min_range)
        for uid in (15,16):
            for offset in (0,2,4,6):
                put(uc,0x522C38+uid*8+offset,box,2)
    for label,x,y,box,max_r,min_r,expected in [
        ("max_equal",65,0,0,64,0,1),("max_outside",66,0,0,64,0,0),
        ("boxes_reduce_gap",100,0,8,83,0,1),("boxes_one_pixel_outside",100,0,8,82,0,0),
        ("min_equal_rejected",11,0,0,64,10,0),("min_one_past",12,0,0,64,10,1),
        ("zero_gap_no_min",0,0,0,64,0,1)]:
        uc=new();combat_fixture(uc,x,y,box,max_r,min_r)
        execution=call(uc,0x421060,args=(OTHER,))
        record("weapon_in_range_"+label, {"x":x,"y":y,"box_half_extent":box,"max_range":max_r,"min_range":min_r},
               execution["return_eax"],expected,execution)
    for target_moving,heading,expected in ((False,0,1),(True,0,0),(True,32,0),(True,33,1),(True,128,1)):
        uc=new();combat_fixture(uc,150,max_range=64)
        put(uc,UNIT+0x20,2,1);put(uc,UNIT+0x3C,2560);put(uc,UNIT+0x48,128,2)
        put(uc,OTHER+0x20,2 if target_moving else 0,1);put(uc,OTHER+0x23,heading,1)
        execution=call(uc,0x421060,args=(OTHER,))
        record(f"moving_range_target_moving_{target_moving}_heading_{heading}", {"target_x":150,"base_range":64,"speed_fp8":2560,"acceleration":128,"target_moving":target_moving,"heading_difference":heading},
               execution["return_eax"],expected,execution)
    for label,x,visible,stop in (("state0_in_range",50,1,STOP),
                                ("state0_out_of_range",100,1,0x43A180),
                                ("state0_invisible",50,0,0x43A180),
                                ("state1_in_range_stop",50,1,0x43A490),
                                ("state1_ranged_approach",100,1,0x438BA0)):
        uc=new();combat_fixture(uc,x,max_range=64)
        state=1 if label.startswith("state1") else 0
        put(uc,UNIT+0x4E,state,1);put(uc,SPRITE+0x200+0xC,visible,1)
        put(uc,0x524F18+4,1)  # Ranged flingy; avoids replacement movement helpers.
        execution=call(uc,0x420770,dispatch_boundary=stop!=STOP,stop_before=stop)
        record("attack_main_"+label,{"state":state,"target_x":x,"visible":visible,"range":64},
               [execution["stop_va"],get(uc,UNIT+0x4E,1),get(uc,UNIT+0x58,2),get(uc,UNIT+0x1C,2)],
               [hex(stop),1 if stop==STOP else state,x,x],execution)
    for timer in (1,15):
        uc=new();unit(uc)
        put(uc,UNIT+0x4D,14,1);put(uc,UNIT+0x4E,1,1);put(uc,UNIT+0x54,timer,1)
        execution=call(uc,0x41E4A0)
        record(f"attack_move_timer_{timer}",{"state":1,"timer":timer,"auto_target":0},get(uc,UNIT+0x54,1),timer,execution)
    uc=new();unit(uc)
    put(uc,UNIT+0x4D,14,1);put(uc,UNIT+0x4E,1,1);put(uc,UNIT+0xDC,0x80001)
    execution=call(uc,0x41E4A0,dispatch_boundary=True,stop_before=0x42A5A0)
    record("attack_move_exact_arrival_immobile_bit",{"state":1,"timer":0,"status_bit_80000":True,"move_target_equals_position":True},
           [execution["stop_va"],get(uc,UNIT+0x54,1)],["0x42a5a0",15],execution)
    result = {"target_sha256":TARGET_HASH,"target_version":"1.17.0.1",
              "probe_script_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "engine":{"name":"Unicorn","version":unicorn.__version__,"host_arch":platform.machine(),"guest":"x86-32","python":platform.python_version()},
              "original_code_executed":True,"no_game_process_started":True,
              "replacement_helper_stubs":[],"allowlisted_code_ranges":[[hex(lo),hex(hi)] for lo,hi in ALLOW],
              "scope":PLAN,"cases":cases,"passed":len(cases),"failed":0}
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({"passed":len(cases),"failed":0,"complete_returns":sum(x["execution"]["complete_return"] for x in cases)}))

if __name__ == "__main__":
    main()
