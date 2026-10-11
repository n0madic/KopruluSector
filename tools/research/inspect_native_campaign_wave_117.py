#!/usr/bin/env python3
"""Recover actual military transfer and selected campaign-wave launch paths.

Static by default. Explicit execution permits only reviewed original x86
instructions and real callees in private fixture memory, with no helper stubs.
"""
import argparse, hashlib, json, platform, struct
from pathlib import Path
import capstone, pefile
import inspect_native_campaign_attack_117 as attack

arrival = attack.arrival
TARGET_HASH = attack.TARGET_HASH
WITNESSES = {
    'transfer_military_records': (0x4B15D0, 0x4B1702),
    'step_attacking_group': (0x4AC870, 0x4AC9B0),
    'update_regional_strength': (0x4ADBD0, 0x4ADCE3),
    'update_regional_targets': (0x4ADCF0, 0x4ADDD1),
    'air_region_target': (0x4A9800, 0x4A981D),
    'ground_region_target': (0x4A9820, 0x4A983D),
    'choose_region_target': (0x4A90D0, 0x4A9326),
    'military_ground_and_missing_weapons': (0x4AAEF0, 0x4AAF67),
    'missing_attack_roster': (0x4AA500, 0x4AA660),
    'any_group_member_outside_region': (0x4AB150, 0x4AB1C5),
    'attack_group_timeout': (0x483940, 0x483983),
    'transport_or_weapon_route_predicate': (0x4AB490, 0x4AB5C9),
    'region_state_dispatch': (0x4B0CE0, 0x4B0D3C),
    'air_weapon_with_subunit': (0x41FB40, 0x41FB63),
    'ground_weapon_without_subunit': (0x41FC20, 0x41FC3C),
    'completed_unit_count': (0x4143E0, 0x4143F4),
    'max_force_word': (0x482FD0, 0x482FDE),
    'unit_target_hostility': (0x4290E0, 0x429110),
    'campaign_nearby_strength_callback': (0x4AA2C0, 0x4AA304),
    'insufficient_strength_disperse': (0x4AA9B0, 0x4AAAA0),
    'owned_base_or_ground_neighbor': (0x4AB810, 0x4AB865),
    'release_military_record': (0x4A9A30, 0x4A9A8E),
    'disperse_into_current_regions': (0x4B1500, 0x4B15C9),
    'pick_global_attack_target': (0x4ABA70, 0x4ABC8A),
    'player_has_completed_transport': (0x4AA8A0, 0x4AA8EC),
}


def dependencies():
    mods = (attack, arrival, attack.removal, attack.vm, arrival.lifecycle, arrival.primary,
            arrival.military, arrival.combat, arrival.spatial)
    return {Path(m.__file__).name: hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in mods}


def probes(pe, orders):
    if platform.machine().lower() not in ('x86_64', 'amd64'):
        raise RuntimeError('Use validated x86_64 Rosetta Unicorn runtime')
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    unit, sprite, imageptr, node, regions, pool, otherpool, orderpool = 0x800000, 0x805000, 0x806000, 0x807000, 0x810000, 0x820000, 0x828000, 0x840000
    output, script, program, mapbase, stack, stop = 0x850000, 0x860000, 0x870000, 0xB00000, 0x900000, 0xA00000
    player, p = 7, 0x67D068 + 7 * 0x4E8
    image = pe.get_memory_mapped_image(); size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    allowed = list(WITNESSES.values()) + list(attack.WITNESSES.values()) + list(arrival.WITNESSES.values())
    allowed += list(arrival.primary.BLOCKS.values()) + list(arrival.military.BLOCKS.values()) + list(arrival.spatial.BLOCKS.values()) + list(attack.removal.WITNESSES.values())
    allowed += [arrival.lifecycle.BLOCKS[x] for x in ('military_ai_allocate', 'raw_rng')]
    allowed += [arrival.combat.BLOCKS[x] for x in ('campaign_ai_flag', 'approximate_distance', 'read_cached_air_strength', 'read_cached_ground_strength', 'radius_callback_iteration', 'upgrade_level')]
    allowed += [attack.vm.WITNESSES[x] for x in ('dispatch_and_all_handlers', 'byte_reader', 'word_reader', 'script_scheduler', 'attack_clear_and_default_build', 'attack_clear_region_effects', 'player_unit_head', 'global_unit_head', 'unit_hostility')]
    allowed += [(0x41FE40, 0x41FE55), (0x481B00, 0x481B09)]
    cases = []
    def put(u, a, v, n=4): u.mem_write(a, (v & ((1 << (n * 8)) - 1)).to_bytes(n, 'little'))
    def get(u, a, n=4): return int.from_bytes(u.mem_read(a, n), 'little')
    def region(i): return regions + i * 0x34
    def new(campaign=True, members=(37,), flying=True, shared_pool=True, destination_available=True):
        u = Uc(UC_ARCH_X86, UC_MODE_32); u.mem_map(0x400000, size); u.mem_write(0x400000, image)
        for a, n in ((unit, 0x90000), (mapbase, 0x70000), (stack, 0x20000), (stop, 0x1000)): u.mem_map(a, n)
        u.mem_write(0x67D068, bytes(8 * 0x4e8)); put(u, p + 0x218, 0x20 if campaign else 0, 2)
        put(u, p + 0x222, 0, 1); put(u, p + 0x228, 100); put(u, p + 0x21e, 2, 2)
        put(u, 0x5349d8, 100); put(u, 0x69b584, mapbase); put(u, mapbase, 4, 2)
        for ident in (69, 11, 42): put(u, 0x52c0c4 + (ident * 12 + player) * 4, 0)
        put(u, 0x534590 + player * 46 + 24, 0, 1)
        for y in range(32):
            for x in range(32): put(u, mapbase + 0xc + 2 * (y * 256 + x), 1, 2)
        put(u, mapbase + 0xc + 2 * (16 * 256 + 16), 2, 2)
        for i, center in ((0, (0, 0)), (1, (128, 128)), (2, (512, 512)), (3, (768, 768))):
            t, r = mapbase + 0x449fc + i * 64, region(i)
            put(u, t + 2, 1, 2); put(u, t + 0x10, center[0] * 256); put(u, t + 0x14, center[1] * 256)
            u.mem_write(t + 0x18, struct.pack('<4H', max(0, center[0] - 32), max(0, center[1] - 32), center[0] + 32, center[1] + 32))
            put(u, r, i, 2); put(u, r + 4, player, 1); put(u, r + 5, 8 if i == 1 else 0, 1); put(u, r + 6, 77, 1)
            put(u, r + 0x2c, pool if shared_pool or i != 2 else otherpool)
        put(u, mapbase + 0x449fc + 64 + 7, 1, 1); put(u, mapbase + 0x449fc + 64 + 0xc, mapbase + 0x60000); put(u, mapbase + 0x60000, 2, 2)
        put(u, 0x6b1a74 + player * 4, regions); put(u, region(1) + 2, 2, 2)
        put(u, region(1) + 0x30, node if members else 0); put(u, region(1) + 0xa, len(members) if not flying else 0, 2)
        put(u, region(1) + 0x28, unit + 0x7000); put(u, pool + 0x4e20, 0)
        put(u, otherpool + 0x4e20, node + 0x300 if destination_available else 0)
        for j, ident in enumerate(members):
            a, s, im, n = unit + j * 0x200, sprite + j * 0x100, imageptr + j * 0x100, node + j * 0x20
            put(u, a + 0x64, ident, 2); put(u, a + 0x4c, player, 1); put(u, a + 0x4d, 1, 1)
            put(u, a + 0xdc, 5 if flying else 1); put(u, a + 0xc, s); put(u, a + 0x34, 20 + j)
            put(u, a + 0x134, n); put(u, a + 0x6c, unit + (j + 1) * 0x200 if j + 1 < len(members) else 0)
            put(u, s + 0x14, 128, 2); put(u, s + 0x16, 128, 2)
            for f in (0x18, 0x1c, 0x20): put(u, s + f, im)
            put(u, im + 0x16, 0, 1); put(u, n, node + (j + 1) * 0x20 if j + 1 < len(members) else 0)
            put(u, n + 4, node + (j - 1) * 0x20 if j else 0); put(u, n + 8, 4, 1); put(u, n + 0xc, a); put(u, n + 0x10, region(1))
            put(u, a + 0x138, 20 + j, 2); put(u, a + 0x13a, 30 + j, 2)
            put(u, 0x521660 + ident * 4, 0); put(u, 0x5211d8 + ident, 130, 1); put(u, 0x5213a8 + ident, 130, 1)
            u.mem_write(a + 0x98, struct.pack('<5H', *([228] * 5)))
        put(u, 0x665b08 + player * 4, unit if members else 0); put(u, 0x665ae0, 0)
        put(u, 0x543230, 1024, 2); put(u, 0x543234, 1024, 2); u.mem_write(0x534914, bytes([1]) * 144)
        put(u, 0x673930, 1); put(u, 0x67393c, 1); put(u, 0x673938, 0); put(u, 0x673940 + 56 * 4, 0)
        for a in (0x5c99b0, 0x5d6e48, 0x5d6e5c, 0x5da38c): put(u, a, 0)
        put(u, 0x5aef00, orderpool); put(u, 0x5aef04, orderpool + 19 * 20); put(u, 0x5aef08, 0)
        for j in range(20):
            put(u, orderpool + j * 20, orderpool + (j - 1) * 20 if j else 0); put(u, orderpool + j * 20 + 4, orderpool + (j + 1) * 20 if j < 19 else 0)
        for row in orders:
            put(u, 0x51b200 + row['id'], row['interruptible'], 1); put(u, 0x51b980 + row['id'] * 2, row['highlight'], 2); put(u, 0x51b080 + row['id'], row['subunit_inheritance'], 1)
        put(u, 0x6ab4f8, orderpool + 0x1000); put(u, orderpool + 0x1000, 0, 2); put(u, 0x508d50, 0)
        put(u, 0x6d319c, 0); put(u, 0x50f310, 0)
        return u
    def call(u, start, ecx=None, edx=0, args=()):
        reached, count, calls = False, 0, []; entries = {a for a, b in allowed}
        def hook(m, a, width, _):
            nonlocal reached, count
            if a == stop: reached = True; m.emu_stop(); return
            if not any(lo <= a and a + width <= hi for lo, hi in allowed): raise RuntimeError(f'Unreviewed original instruction {a:#x}; calls{calls[-8:]}')
            count += 1
            if a in entries:
                sp = m.reg_read(UC_X86_REG_ESP); calls.append({'callee': hex(a), 'ecx': m.reg_read(UC_X86_REG_ECX), 'edx': m.reg_read(UC_X86_REG_EDX), 'stack_arguments': [get(m, sp + 4 + j * 4) for j in range(4)]})
        h = u.hook_add(UC_HOOK_CODE, hook); sp = stack + 0x1e000
        u.mem_write(sp, struct.pack('<' + 'I' * (len(args) + 1), stop, *(v & 0xffffffff for v in args)))
        u.reg_write(UC_X86_REG_ESP, sp); u.reg_write(UC_X86_REG_ECX, region(1) if ecx is None else ecx); u.reg_write(UC_X86_REG_EDX, edx)
        try: u.emu_start(start, stop + 1, count=500000)
        except unicorn.UcError as e: raise RuntimeError(f'{e} at{u.reg_read(UC_X86_REG_EIP):#x}; calls{calls[-8:]}') from e
        finally: u.hook_del(h)
        if not reached: raise RuntimeError('No complete return within 500000 original instructions')
        return {'complete_return': True, 'instruction_count': count, 'return_eax': u.reg_read(UC_X86_REG_EAX), 'native_call_trace': calls}
    def record(label, inputs, ex, actual, expected):
        assert actual == expected, (label, inputs, actual, expected)
        cases.append({'label': label, 'fixture_inputs': inputs, 'observed': actual, 'expected_from_static_contract': expected, 'matched': True, 'helper_stubs': [], 'execution': ex})
    def chain(u, head):
        out = []
        while head:
            assert head not in out and len(out) < 20
            out.append(head); head = get(u, head)
        return out
    def transferred_state(u, members):
        return {'source_head': get(u, region(1) + 0x30), 'destination_head': get(u, region(2) + 0x30),
                'source_ground': get(u, region(1) + 0xa, 2), 'destination_ground': get(u, region(2) + 0xa, 2),
                'source_detector': get(u, region(1) + 0x28), 'destination_detector': get(u, region(2) + 0x28),
                'AI': [get(u, unit + j * 0x200 + 0x134) for j in range(len(members))],
                'orders': [get(u, unit + j * 0x200 + 0x4d, 1) for j in range(len(members))],
                'rng_count': get(u, 0x673938), 'source_chain': chain(u, get(u, region(1) + 0x30)), 'destination_chain': chain(u, get(u, region(2) + 0x30))}
    for campaign in (False, True):
        for last, now in ((0, 1000), (100, 100), (100, 219), (100, 220), (100, 279), (100, 280), (0xffffffc0, 120), (200, 100)):
            u = new(campaign, members=()); put(u, p + 0x228, last); put(u, 0x5349d8, now)
            ex = call(u, 0x483940, player); due = bool(last) and (now - last) & 0xffffffff >= (180 if campaign else 120)
            record('native grouping timeout unsigned subtraction inclusive threshold', {'campaign': campaign, 'last': last, 'elapsed_counter': now}, ex,
                   [ex['return_eax'], get(u, p + 0x228)], [int(due), 0 if due else last])
    for shuttle, dropship, overlord, ventral_sacs in ((0, 0, 0, 0), (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 1, 1), (0, 0, 0, 1)):
        u = new(members=())
        for ident, count in ((69, shuttle), (11, dropship), (42, overlord)): put(u, 0x52c0c4 + (ident * 12 + player) * 4, count)
        put(u, 0x534590 + player * 46 + 24, ventral_sacs, 1); ex = call(u, 0x4aa8a0, player)
        record('transport capability uses completedcounts and VentralSacslevel actualaccessors', {'shuttle_completed': shuttle, 'dropship_completed': dropship, 'overlord_completed': overlord, 'ventral_sacs_level': ventral_sacs}, ex,
               ex['return_eax'], int(bool(shuttle or dropship or overlord and ventral_sacs)))
    for campaign in (False, True):
        for flying in (False, True):
            for ident, all_casters in ((37, 0), (34, 0), (9, 0), (9, 1)):
                members = (ident,); u = new(campaign, members, flying)
                ex = call(u, 0x4b15d0, edx=region(2), args=(0, all_casters, 1)); moved = ident != 9 or bool(all_casters)
                record('military transfer actual release allocation arrival caster gate', {'campaign': campaign, 'flying': flying, 'id': ident, 'include_casters': all_casters}, ex, transferred_state(u, members),
                       {'source_head': 0 if moved else node, 'destination_head': node if moved else 0, 'source_ground': int(not flying and not moved), 'destination_ground': int(not flying and moved), 'source_detector': 0, 'destination_detector': unit + 0x7000,
                        'AI': [node], 'orders': [177 if moved and ident == 34 else 157 if moved else 1], 'rng_count': 3 if moved else 0, 'source_chain': [] if moved else [node], 'destination_chain': [node] if moved else []})
    for campaign in (False, True):
        for arg3 in (0, 1):
            u = new(campaign); put(u, unit + 0xdc, 0x25); ex = call(u, 0x4b15d0, edx=region(2), args=(0, 0, arg3)); moved = campaign or not arg3
            got = transferred_state(u, (37,))
            record('InBuilding transfer gate preserves unit unlesscampaign or argumentzero', {'campaign': campaign, 'arg3': arg3, 'status': 0x25, 'null_connected_unit': True}, ex,
                   [got['source_head'], got['destination_head'], got['orders'][0], got['rng_count']], [0 if moved else node, node if moved else 0, 157 if moved else 1, 3 if moved else 0])
    for available in (False, True):
        u = new(shared_pool=False, destination_available=available); ex = call(u, 0x4b15d0, edx=region(2), args=(0, 0, 1))
        record('crosspool transfer release happens before destination allocation failure', {'destination_available': available, 'same_pool': False}, ex,
               [get(u, unit + 0x134), get(u, pool + 0x4e20), get(u, otherpool + 0x4e20), get(u, region(1) + 0x30), get(u, region(2) + 0x30), get(u, 0x673938), get(u, unit + 0x4d, 1), get(u, region(2) + 0x28)],
               [node + 0x300 if available else 0, node, 0, 0, node + 0x300 if available else 0, 3 if available else 0, 157 if available else 1, unit + 0x7000])
    for include_casters in (0, 1):
        u = new(members=(37, 9, 34), flying=False); ex = call(u, 0x4b15d0, edx=region(2), args=(0, include_casters, 1))
        remaining = [] if include_casters else [node + 0x20]; moved = [node + 0x40, node + 0x20, node] if include_casters else [node + 0x40, node]
        record('multi member transfer repairs both sides and reverses moved headorder', {'members': [37, 9, 34], 'include_casters': include_casters}, ex,
               [chain(u, get(u, region(1) + 0x30)), chain(u, get(u, region(2) + 0x30)), get(u, region(1) + 0xa, 2), get(u, region(2) + 0xa, 2), get(u, 0x673938), [get(u, n + 4) for n in moved]],
               [remaining, moved, len(remaining), len(moved), 3 * len(moved), [0] + moved[:-1]])
    for extra_radius in (0, 1):
        u = new(flying=False); put(u, region(1) + 0xa, 0, 2); put(u, region(1) + 0x24, unit); put(u, region(2) + 0x28, unit + 0x7200)
        ex = call(u, 0x4b15d0, edx=region(2), args=(extra_radius, 0, 1))
        record('transfer preserves occupieddetector and sourcecachedslowest groundcountwrap', {'source_ground_count': 0, 'extra_radius': extra_radius, 'empty_radius_index': True, 'destination_detector_occupied': True}, ex,
               [get(u, region(1) + 0xa, 2), get(u, region(1) + 0x24), get(u, region(1) + 0x28), get(u, region(2) + 0x28), get(u, 0x5d6e48), get(u, 0x5d6e5c)], [65535, unit, unit + 0x7000, unit + 0x7200, 0, 0])
    for campaign in (False, True):
        for cap in (0, 10, 200):
            for remote in (False, True):
                u = new(campaign, members=(37, 38), flying=False)
                put(u, unit + 0x138, 0xfff0, 2); put(u, unit + 0x13a, 0xffe0, 2)
                put(u, unit + 0x200 + 0x138, 32, 2); put(u, unit + 0x200 + 0x13a, 64, 2)
                if remote: put(u, sprite + 0x100 + 0x14, 512, 2); put(u, sprite + 0x100 + 0x16, 512, 2)
                put(u, region(1) + 0x18, 101, 2); put(u, region(1) + 0x1a, 103, 2); put(u, region(1) + 0xc, 50, 2); put(u, region(1) + 0xe, 50, 2); put(u, p + 0x21c, cap, 2)
                ex = call(u, 0x4adbd0); air, ground = (101, 103) if campaign else (151, 154)
                record('regional cached strength wraps u16 localmembership capsraisedneed', {'campaign': campaign, 'max_force': cap, 'second_unit_elsewhere': remote, 'empty_radius_index': True}, ex,
                       [get(u, region(1) + o, 2) for o in (0x10, 0x12, 0x14, 0x16, 0xc, 0xe)] + [get(u, 0x5d6e48), get(u, 0x5d6e5c)],
                       [0xffe0 if remote else 32, 0xfff0 if remote else 16, 32, 16, min(cap, ground) if cap else ground, min(cap, air) if cap else air, 0, 0])
    for status in (1, 5, 0x21, 0x25):
        u = new(members=(37,), flying=bool(status & 4)); put(u, unit + 0xdc, status); ex = call(u, 0x4adbd0)
        record('InBuilding status excludes military cachedstrength', {'status': status, 'empty_radius_index': True}, ex,
               [get(u, region(1) + o, 2) for o in (0x10, 0x12, 0x14, 0x16)], [0, 0, 0, 0] if status & 0x20 else [30, 20, 30, 20])
    for owner, current_region, status, same_military_unit in ((7, 1, 1, False), (0, 1, 1, False), (7, 2, 1, False), (7, 1, 0x21, False), (7, 1, 1, True)):
        u = new(); a, s = 0x5da3a0, sprite + 0x1000; x, y = (512, 512) if current_region == 2 else (128, 128)
        put(u, a + 0xc, s); put(u, a + 0x4c, owner, 1); put(u, a + 0xdc, status)
        put(u, a + 0x138, 20, 2); put(u, a + 0x13a, 30, 2); put(u, s + 0x14, x, 2); put(u, s + 0x16, y, 2)
        if same_military_unit: put(u, node + 0xc, a); put(u, a + 0x134, node)
        for table, lo, hi in ((0x5c99b8, x-8, x+8), (0x5d03f8, y-8, y+8)):
            put(u, table, 1); put(u, table + 4, lo); put(u, table + 8, 1); put(u, table + 12, hi)
        for off, value in ((0x13c, 0), (0x140, 1), (0x144, 0), (0x148, 1)): put(u, a + off, value)
        put(u, 0x5c99b0, 2); ex = call(u, 0x4adbd0)
        added = owner == 7 and current_region == 1 and not status & 0x20
        record('campaign actual radiuscallback adds ownedlocal cachedstrength independently of militarymembership', {'spatial_owner': owner, 'spatial_region': current_region, 'spatial_status': status, 'indexed_same_military_unit': same_military_unit}, ex,
               [get(u, region(1) + o, 2) for o in (0x10, 0x12, 0x14, 0x16)] + [get(u, 0x5d6e48), get(u, 0x5d6e5c)], [30 * (1 + int(added)), 20 * (1 + int(added)), 30 * (1 + int(added)), 20 * (1 + int(added)), 0, 0])
    for ground_weapon in (130, 35):
        for air_weapon in (130, 36):
            u = new(flying=False); put(u, 0x5211d8 + 37, ground_weapon, 1); put(u, 0x5213a8 + 37, air_weapon, 1)
            ex = call(u, 0x4aaef0, edx=output, args=(output + 4, output + 8))
            record('military weaponcomposition uses grounddirect airwithsubunit weapon getters', {'ground_weapon': ground_weapon, 'air_weapon': air_weapon, 'ground_unit': True}, ex,
                   [get(u, output + j * 4) for j in range(3)], [1, int(ground_weapon == 130), int(air_weapon == 130)])
    for burrowed in (False, True):
        u = new(members=(103,), flying=False); put(u, unit + 0xdc, 0x11 if burrowed else 1); put(u, 0x5211d8 + 103, 45, 1)
        ex = call(u, 0x4aaef0, edx=output, args=(output + 4, output + 8))
        record('unburrowed Lurker has no groundweapon in groupcomposition', {'burrowed': burrowed, 'unit_id': 103, 'loaded_ground_weapon': 45}, ex,
               [get(u, output + j * 4) for j in range(3)], [1, int(not burrowed), 1])
    u = new(members=(3,), flying=False); put(u, unit + 0x70, unit + 0x400); put(u, unit + 0x400 + 0x64, 4, 2)
    put(u, 0x5211d8 + 4, 1, 1); put(u, 0x5213a8 + 4, 2, 1)
    ex = call(u, 0x4aaef0, edx=output, args=(output + 4, output + 8))
    record('group airweapon falls back to subunit while group groundweapon does not', {'parent': 3, 'subunit': 4, 'parent_weapons': [130, 130], 'subunit_weapons': [1, 2]}, ex,
           [get(u, output + j * 4) for j in range(3)], [1, 1, 0])
    for roster, members, region_match, completed in (([38], (37,), True, True), ([38, 38], (37,), True, True), ([38, 38], (37, 37), True, True), ([38, 39, 38], (37,), True, True), ([38], (37,), False, True), ([38], (37,), True, False), ([6], (30,), True, True), ([229, 38], (37,), True, True), ([], (37,), True, True)):
        u = new(members=members)
        for j, ident in enumerate(members):
            if not region_match: put(u, node + j * 0x20 + 0x10, region(2))
            if not completed: put(u, unit + j * 0x200 + 0xdc, 4)
        for j, ident in enumerate(roster): put(u, p + 0x230 + j * 2, ident, 2)
        ex = call(u, 0x4aa500, edx=output, args=(1,))
        expected = [i - 1 for i in roster]
        if completed and region_match:
            for ident in members:
                ident = 5 if ident == 30 else ident
                if ident in expected:
                    ix = expected.index(ident); expected[ix] = 228
                    if ix == len(expected) - 1: expected.pop()
        record('missing attack roster firstmatch sentinel and trailing countshrink', {'roster_idplus1': roster, 'members': list(members), 'matching_region': region_match, 'completed': completed, 'argument': 1}, ex,
               {'count': ex['return_eax'], 'output_prefix': [get(u, output + j * 4) for j in range(len(roster))]}, {'count': len(expected), 'output_prefix': expected + ([228] if len(expected) < len(roster) else [])})
    for campaign in (False, True):
        for state in (8, 9):
            for delta, roster, remote in ((0, [38], False), (0, [44], False), (0, [38], True), (179 if campaign else 119, [44], False), (180 if campaign else 120, [44], False)):
                u = new(campaign, members=(37,)); put(u, region(1) + 5, state, 1)
                for j, i in enumerate(roster): put(u, p + 0x230 + j * 2, i, 2)
                put(u, 0x5349d8, 100 + delta)
                if remote: put(u, sprite + 0x14, 512, 2); put(u, sprite + 0x16, 512, 2)
                timed_out = delta >= (180 if campaign else 120); launched = timed_out or (roster == [38] and not remote)
                ex = call(u, 0x4b0ce0, player, 1)
                record('real state8or9 dispatch launches on rosterready arrival or inclusive timeout', {'campaign': campaign, 'state': state, 'delta': delta, 'roster': roster, 'member_current_region': 2 if remote else 1, 'empty_enemy_spatial_index': True}, ex,
                       {'source_state': get(u, region(1) + 5, 1), 'destination_state': get(u, region(2) + 5, 1), 'destination_priority': get(u, region(2) + 6, 1), 'assigned': get(u, node + 0x10), 'group_plus1': get(u, p + 0x21e, 2), 'last_attack': get(u, p + 0x228), 'roster_first': get(u, p + 0x230, 2), 'unit_order': get(u, unit + 0x4d, 1), 'rng_count': get(u, 0x673938), 'source_count': get(u, 0x673940 + 56 * 4), 'spatial_depth': get(u, 0x5d6e48)},
                       {'source_state': 0 if launched else state, 'destination_state': 1 if launched else 0, 'destination_priority': (70 if campaign else 160) if launched else 77, 'assigned': region(2) if launched else region(1), 'group_plus1': 2, 'last_attack': 0 if timed_out else 100, 'roster_first': roster[0], 'unit_order': 158 if launched else 1, 'rng_count': 3 if launched else 0, 'source_count': 3 if launched else 0, 'spatial_depth': 0})
    for campaign in (False, True):
        for source_state in (8, 9):
            u = new(campaign, members=(37,), flying=False); put(u, unit + 0x105, 1, 1)
            put(u, region(1) + 5, source_state, 1); put(u, p + 0x230, 38, 2)
            ex = call(u, 0x4b0ce0, player, 1)
            record('ground military rosterready launch runs actual transportcounts upgrade pointconnectivity', {'campaign': campaign, 'source_state': source_state, 'check_point_connectivity': True}, ex,
                   [get(u, region(1) + 5, 1), get(u, region(2) + 5, 1), get(u, node + 0x10), get(u, region(1) + 0xa, 2), get(u, region(2) + 0xa, 2), get(u, unit + 0x4d, 1), get(u, 0x673938)], [0, 1, region(2), 0, 1, 158, 3])
    for campaign in (False, True):
        u = new(campaign); put(u, region(1) + 2, 0, 2); put(u, 0x5349d8, 280 if campaign else 220)
        ex = call(u, 0x4b0ce0, player, 1)
        record('timeout with destinationzero resets source without moving its military list', {'campaign': campaign, 'destination_region': 0, 'elapsed_counter': 280 if campaign else 220}, ex,
               [get(u, region(1) + 5, 1), get(u, region(1) + 0x30), get(u, node + 0x10), get(u, unit + 0x4d, 1), get(u, p + 0x228), get(u, 0x673938)], [0, node, region(1), 1, 0, 0])
    for campaign in (False, True):
        for current_region in (1, 2):
            u = new(campaign, members=(43,)); put(u, region(1) + 8, 0x24, 1); put(u, region(1) + 7, 2, 1); put(u, region(1) + 0xc, 31, 2)
            if current_region == 2: put(u, sprite + 0x14, 512, 2); put(u, sprite + 0x16, 512, 2)
            ex = call(u, 0x4b0ce0, player, 1)
            record('flag20 insufficient localstrength disperses to current region before roster timer', {'campaign': campaign, 'unit_id': 43, 'current_region': current_region, 'required_ground': 31, 'cached_ground': 30, 'flag20': True, 'empty_enemy_and_ownedbase_spatial_index': True}, ex,
                   [get(u, region(1) + 5, 1), get(u, region(1) + 8, 1), get(u, region(1) + 7, 1), get(u, node + 0x10), get(u, unit + 0x4d, 1), get(u, p + 0x228), get(u, p + 0x21e, 2), get(u, 0x673938)], [0, 0, 1, region(current_region), 157, 100, 2, 3])
    for campaign in (False, True):
        u = new(campaign, flying=False); put(u, mapbase + 0x449fc + 64, 0x1ffd, 2)
        ex = call(u, 0x4b0ce0, player, 1)
        record('ground specialterrain marker takes globaltarget fallback then disperses with empty enemies', {'campaign': campaign, 'terrain_marker': 0x1ffd, 'empty_global_active_list': True}, ex,
               [get(u, region(1) + 5, 1), get(u, region(2) + 5, 1), get(u, node + 0x10), get(u, unit + 0x4d, 1), get(u, 0x673938), get(u, p + 0x228)], [0, 0, region(1), 157, 3, 100])
    for campaign in (False, True):
        for ident in (32, 37):
            u = new(campaign, members=(ident,)); put(u, region(1) + 8, 0x20, 1); put(u, region(1) + 0xc, 31, 2)
            ex = call(u, 0x4b0ce0, player, 1)
            record('noncampaign first Firebat or Zergling suppresses lowstrength dispersion', {'campaign': campaign, 'first_unit_id': ident, 'required_ground': 31, 'cached_ground': 30, 'empty_roster': True}, ex,
                   [get(u, region(1) + 5, 1), get(u, region(2) + 5, 1), get(u, node + 0x10), get(u, unit + 0x4d, 1)], [0, 0 if campaign else 1, region(1) if campaign else region(2), 157 if campaign else 158])
    for queue, target, hostility in ((False, False, False), (True, False, False), (False, True, False), (False, True, True)):
        u = new(); put(u, sprite + 0x14, 512, 2); put(u, sprite + 0x16, 512, 2)
        if queue: put(u, unit + 0x7c, orderpool)
        if target:
            put(u, unit + 0x5c, unit + 0x400); put(u, unit + 0x400 + 0x4c, 0, 1); put(u, 0x534914 + player * 12, 0 if hostility else 1, 1)
        ex = call(u, 0x4ab150, player)
        record('group outside-region wait ignores queued and activelyhostile targeting units', {'member_current_region': 2, 'assigned_region': 1, 'has_queue': queue, 'has_target': target, 'target_hostile': hostility}, ex, ex['return_eax'], 0 if queue or hostility else 1)
    for campaign in (False, True):
        u = new(campaign); put(u, p + 0x230, 44, 2); put(u, 0x5349d8, 280 if campaign else 220)
        launch = call(u, 0x4b0ce0, player, 1)
        u.mem_write(program, bytes([14, 11, 2, 1, 0])); put(u, 0x6a2e74, program); put(u, 0x6a2e70, script); put(u, script + 0x10, player)
        ex = call(u, 0x4938e0, player)
        record('real dispatch launch then real attackDo attackClear wait VMsequence', {'campaign': campaign, 'roster_missing_at_launch': [44], 'code': [14, 11, 2, 1, 0], 'preceding_complete_launch': launch}, ex,
               [get(u, script + 8), get(u, script + 0xc), get(u, p + 0x21e, 2), get(u, p + 0x228), bytes(u.mem_read(p + 0x230, 128)).hex(), get(u, region(1) + 5, 1), get(u, region(2) + 5, 1), get(u, node + 0x10), get(u, unit + 0x4d, 1), get(u, 0x673938)],
               [5, 1, 0, 0, bytes(128).hex(), 0, 1, region(2), 158, 3])
    return {'format_version': 1, 'original_code_executed': True, 'complete_game_executed': False, 'windows_or_host_api_called': False, 'helper_stubs': [], 'cases': cases, 'all_matched': True,
            'case_count': len(cases), 'complete_return_count': len(cases), 'prefix_case_count': 0, 'matched_count': sum(c['matched'] for c in cases),
            'allowlisted_preferred_va_ranges': [[hex(lo), hex(hi)] for lo, hi in allowed],
            'runtime': {'python': platform.python_version(), 'architecture': platform.machine(), 'platform': platform.platform(), 'pefile': pefile.__version__, 'capstone': capstone.__version__, 'unicorn': unicorn.__version__},
            'limitations': ['Synthetic objects, cached strengths, terrain groups, spatial-index contents and loaded DAT arrays are explicit inputs.', 'Positive enemy target ranking, defeat/regroup flag20, ground transport fallback and callback transfer are not substituted or executed here.', 'Order issuance is real; movement, combat and full campaign execution are not performed.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True); parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--execute-original-bounded-probes', action='store_true'); parser.add_argument('--orders-dat', type=Path); parser.add_argument('--probe-out', type=Path)
    args = parser.parse_args(); raw = args.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != TARGET_HASH: parser.error('Unknown executable hash')
    pe = pefile.PE(data=raw); assert pe.FILE_HEADER.Machine == 0x14c and pe.OPTIONAL_HEADER.ImageBase == 0x400000
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); witnesses = {}
    for name, (lo, hi) in WITNESSES.items():
        code = pe.get_data(lo - 0x400000, hi - lo); ins = list(md.disasm(code, lo)); decoded = sum(i.size for i in ins)
        witnesses[name] = {'preferred_va': hex(lo), 'end_exclusive': hex(hi), 'decoded_bytes': decoded, 'requested_bytes': len(code), 'undecoded_tail': code[decoded:].hex(),
                           'instructions': [{'va': hex(i.address), 'file_offset': hex(pe.get_offset_from_rva(i.address - 0x400000)), 'bytes': i.bytes.hex(), 'mnemonic': i.mnemonic, 'operands': i.op_str} for i in ins]}
    sha = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    result = {'format_version': 1, 'target_sha256': TARGET_HASH, 'target_version': '1.17.0.1', 'inspector_sha256': sha, 'original_code_executed': False, 'dependency_script_sha256': dependencies(), 'witnesses': witnesses,
              'region_state_dispatch_table': {'preferred_va': '0x4b0d3c', 'state0_to9_handlers': [hex(x) for x in struct.unpack('<10I', pe.get_data(0x4b0d3c - 0x400000, 40))]}}
    args.out.parent.mkdir(parents=True, exist_ok=True); args.out.write_text(json.dumps(result, indent=2) + '\n')
    if args.execute_original_bounded_probes:
        if not args.probe_out or not args.orders_dat: parser.error('Execution requires --probe-out and --orders-dat')
        dat = args.orders_dat.read_bytes()
        if hashlib.sha256(dat).hexdigest() != arrival.primary.ORDERS_HASH: parser.error('Unknown orders.dat hash')
        rows = arrival.primary.stock_orders(dat); run = probes(pe, rows)
        run.update({'target_sha256': TARGET_HASH, 'target_version': '1.17.0.1', 'probe_script_sha256': sha, 'dependency_script_sha256': dependencies(), 'orders_dat_sha256': hashlib.sha256(dat).hexdigest()})
        args.probe_out.parent.mkdir(parents=True, exist_ok=True); args.probe_out.write_text(json.dumps(run, indent=2) + '\n')
        print(json.dumps({'case_count': run['case_count'], 'matched_count': run['matched_count'], 'complete_return_count': run['complete_return_count'], 'script_sha256': sha}))
    else: print('Wrote static wave evidence; original code not executed')


if __name__ == '__main__': main()
