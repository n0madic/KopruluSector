#!/usr/bin/env python3
"""Measure original local-path heap identity, signed priorities and tie rules."""
import argparse
from collections import Counter
import json
from pathlib import Path
import platform
import struct

import capstone
import pefile
import probe_native_local_paths_117 as local

assets = local.assets
TARGET_HASH = local.TARGET_HASH
BLOCKS = {name: local.BLOCKS[name] for name in ('local_heap_insert', 'local_heap_down', 'local_heap_priority')}
HEAP, NODES, SENTINEL, STACK, STOP = 0x810000, 0x820000, 0x830000, 0x900000, 0xA00000
LENGTHS = (1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 149, 150)


def down(queue, costs, index):
    """Reference sift using explicit object IDs and signed integer costs."""
    value = queue[index]
    while index * 2 < len(queue):
        child = index * 2
        if child + 1 < len(queue) and costs[queue[child + 1]] < costs[queue[child]]:
            child += 1
        if costs[value] <= costs[queue[child]]:
            break
        queue[index] = queue[child]
        index = child
    queue[index] = value


def up(queue, costs, index):
    value = queue[index]
    while index > 1 and costs[queue[index // 2]] >= costs[value]:
        queue[index] = queue[index // 2]
        index //= 2
    queue[index] = value


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    for name in ('static-out', 'out'):
        p.add_argument('--' + name, type=Path)
    p.add_argument('--execute-original-code', action='store_true')
    a = p.parse_args()
    raw = a.exe.read_bytes()
    if assets.sha(raw) != TARGET_HASH:
        p.error('Unknown executable hash')
    pe = pefile.PE(data=raw)
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    reviewed, witnesses = {}, {}
    for name, (lo, hi) in BLOCKS.items():
        rows = list(md.disasm(pe.get_data(lo - base, hi - lo), lo))
        assert rows[-1].address + rows[-1].size == hi
        reviewed.update({r.address: r.size for r in rows})
        witnesses[name] = [{'va': hex(r.address), 'bytes': r.bytes.hex(), 'mnemonic': r.mnemonic,
                            'operands': r.op_str} for r in rows]
    if a.static_out:
        a.static_out.write_text(json.dumps({'target_sha256': TARGET_HASH, 'version': '1.17.0.1',
            'evidence': 'static; analytical labels', 'witnesses': witnesses}, indent=2) + '\n')
    if not a.execute_original_code:
        print(json.dumps({'mode': 'static only', 'blocks': len(BLOCKS)}))
        return
    assert a.out
    import unicorn
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
    for address, size in ((base, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095),
                          (0x800000, 0x100000), (STACK, 0x10000), (STOP, 0x1000)):
        uc.mem_map(address, size)
    uc.mem_write(base, pe.get_memory_mapped_image())
    cases = []
    entries = {lo: name for name, (lo, hi) in BLOCKS.items()}

    def put(address, value):
        uc.mem_write(address, struct.pack('<I', value & 0xFFFFFFFF))

    def call(entry, arguments):
        sp = STACK + 0xF000
        uc.mem_write(sp, struct.pack('<' + 'I' * (len(arguments) + 1), STOP, *(v & 0xFFFFFFFF for v in arguments)))
        for register, value in ((UC_X86_REG_ESP, sp), (UC_X86_REG_ECX, HEAP), (UC_X86_REG_EDX, 0)):
            uc.reg_write(register, value)
        count, trace = 0, Counter()
        def gate(cpu, address, size, unused):
            nonlocal count
            if address == STOP:
                cpu.emu_stop()
                return
            if reviewed.get(address) != size:
                raise RuntimeError('Unreviewed original instruction ' + hex(address))
            count += 1
            if address in entries:
                trace[entries[address]] += 1
        hook = uc.hook_add(unicorn.UC_HOOK_CODE, gate)
        try:
            uc.emu_start(entry, STOP, count=10000)
            assert uc.reg_read(UC_X86_REG_EIP) == STOP
            assert uc.reg_read(UC_X86_REG_ESP) == sp + 4 + len(arguments) * 4
        finally:
            uc.hook_del(hook)
        return {'entry': hex(entry), 'scope': 'complete original function', 'instructions': count,
                'entry_visits': dict(trace), 'returned_eax_uninterpreted': uc.reg_read(UC_X86_REG_EAX)}

    def fixture(costs, queue):
        uc.mem_write(HEAP - 16, b'\xA5' * (0x280 + 32))
        uc.mem_write(HEAP, bytes(0x264))
        put(SENTINEL + 0x1C, -2147483648)
        put(HEAP + 0x260, SENTINEL)
        put(HEAP, SENTINEL)
        put(HEAP + 0x25C, len(queue) - 1)
        for index, value in enumerate(queue[1:], 1):
            put(HEAP + index * 4, NODES + value * 32)
        for index, cost in costs.items():
            put(NODES + index * 32 + 0x1C, cost)

    def observe(expected_queue, costs):
        count = int.from_bytes(uc.mem_read(HEAP + 0x25C, 4), 'little')
        pointers = list(struct.unpack('<' + 'I' * count, uc.mem_read(HEAP + 4, count * 4)))
        actual_queue = [0] + [(v - NODES) // 32 for v in pointers]
        assert actual_queue == expected_queue
        assert int.from_bytes(uc.mem_read(HEAP + 0x260, 4), 'little') == SENTINEL
        assert uc.mem_read(HEAP - 16, 16) == b'\xA5' * 16 and uc.mem_read(HEAP + 0x264, 16) == b'\xA5' * 16
        for index, cost in costs.items():
            assert struct.unpack('<i', uc.mem_read(NODES + index * 32 + 0x1C, 4))[0] == cost
        return {'object_ids': actual_queue[1:], 'priorities': [costs[i] for i in actual_queue[1:]],
                'count': count, 'guards_preserved': True}

    for length in LENGTHS:
        for profile in ('ascending', 'descending', 'equal', 'alternating'):
            costs = {i: i if profile == 'ascending' else length - i if profile == 'descending' else
                     17 if profile == 'equal' else (i % 2) * 100 for i in range(1, length + 1)}
            queue, runs, states = [0], [], []
            fixture(costs, queue)
            for i in range(1, length + 1):
                queue.append(i)
                up(queue, costs, len(queue) - 1)
                runs.append(call(0x492A30, (NODES + i * 32,)))
                states.append(observe(queue, costs))
            cases.append({'name': 'insert_{}_{}'.format(length, profile), 'passed': True,
                'inputs': {'length': length, 'profile': profile}, 'runs': runs, 'observed': states,
                'oracle': 'Exact object identity at every insertion; equal keys rise above parents'})
        baseline = {i: (i + 1) // 2 for i in range(1, length + 2)}
        base_queue = [0] + list(range(1, length + 1))
        for index in sorted({1, length, max(1, length // 2), min(length, 2), min(length, 3)}):
            for change in (-100, -1, 0, 1, 1000000):
                costs, queue = dict(baseline), list(base_queue)
                target, previous = queue[index], costs[queue[index]]
                new = previous + change
                fixture(costs, queue)
                costs[target] = new
                if new != previous:
                    (up if new < previous else down)(queue, costs, index)
                run = call(0x492900, (NODES + target * 32, new))
                state = observe(queue, costs)
                cases.append({'name': 'priority_{}_{}_{}'.format(length, index, change), 'passed': True,
                    'inputs': {'length': length, 'index': index, 'old_priority': previous, 'new_priority': new},
                    'runs': [run], 'observed': state, 'oracle': 'Signed priority transition and exact queue identity; unchanged key does not reorder'})
            costs, queue = dict(baseline), list(base_queue)
            fixture(costs, queue)
            costs[queue[index]] = 1000000
            put(NODES + queue[index] * 32 + 0x1C, 1000000)
            down(queue, costs, index)
            run = call(0x4929B0, (index,))
            state = observe(queue, costs)
            cases.append({'name': 'down_{}_{}'.format(length, index), 'passed': True,
                'inputs': {'length': length, 'index': index, 'changed_priority': 1000000}, 'runs': [run],
                'observed': state, 'oracle': 'Exact sift-down identity; equal children choose left, current equal child stays'})
        for new in (-1, baseline[length + 1], 1000000):
            costs, queue = dict(baseline), list(base_queue)
            fixture(costs, queue)
            old = costs[length + 1]
            costs[length + 1] = new
            run = call(0x492900, (NODES + (length + 1) * 32, new))
            state = observe(queue, costs)
            cases.append({'name': 'absent_{}_{}'.format(length, new), 'passed': True,
                'inputs': {'length': length, 'absent_id': length + 1, 'old_priority': old, 'new_priority': new},
                'runs': [run], 'observed': state, 'oracle': 'Absent-node cost still updates; queue count/order remain unchanged'})
    result = {'target_sha256': TARGET_HASH, 'version': '1.17.0.1', 'probe_script_sha256': assets.file_sha(Path(__file__)),
        'dependency_hashes': {Path(m.__file__).name: assets.file_sha(Path(m.__file__)) for m in (local, assets)},
        'case_count': len(cases), 'total_native_checks': len(cases), 'all_expected_results_matched': True, 'failed_cases': 0,
        'complete_function_returns': sum(len(c['runs']) for c in cases), 'maximum_instructions_per_execution': 10000,
        'no_helper_stubs': True, 'no_import_execution': True, 'full_game_or_campaign_executed': False,
        'runtime': {'python': platform.python_version(), 'architecture': platform.machine(), 'unicorn': unicorn.__version__},
        'cases': cases, 'limits': 'Declared valid heap lengths1..150 and sentinel lower than all costs; search capacity callers and malformed sentinel/overflow inputs are separate'}
    a.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'cases': len(cases), 'complete_returns': result['complete_function_returns']}))


if __name__ == '__main__':
    main()
