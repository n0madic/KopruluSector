#!/usr/bin/env python3
"""Guarded native BIN fixups, admission, UI frame and color-bank contracts."""
import argparse, hashlib, json, platform, struct, sys
from collections import Counter
from pathlib import Path
import pefile
import inspect_native_dialog_bin_117 as source
from probe_native_pixel_blitters_117 import pcx

PRIVATE, CONTROL, PARENT, OUTPUT = 0x800000, 0x800100, 0x800200, 0x800400
FILE, GRP, FONT, STACK, STOP = 0x810000, 0x820000, 0x830000, 0x900000, 0xA00000
MASK = 0xFFFFFFFF

def sha(b):
    return hashlib.sha256(b).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('exe', 'assets', 'asset-manifest', 'font-assets', 'font-manifest'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--execute-original-code', action='store_true')
    args = parser.parse_args()
    raw = args.exe.read_bytes()
    assert sha(raw) == source.HASH
    if not args.execute_original_code:
        print(json.dumps(dict(original_code_executed=False, target_sha256=source.HASH)))
        return
    assert args.out and platform.machine().lower() in ('x86_64', 'amd64')
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EFLAGS
    pe = pefile.PE(data=raw)
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(0x400000, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095)
    machine.mem_write(0x400000, pe.get_memory_mapped_image())
    machine.mem_map(PRIVATE, 0x80000)
    machine.mem_map(STACK, 0x10000)
    machine.mem_map(STOP, 4096)
    def write(at, value, width=4):
        machine.mem_write(at, (value & ((1 << (width * 8)) - 1)).to_bytes(width, 'little'))
    def read(at, width=4):
        return int.from_bytes(machine.mem_read(at, width), 'little')
    manifest = json.loads(args.asset_manifest.read_text())
    assets = {}
    for name, meta in manifest['assets'].items():
        blob = (args.assets / name.replace('\\', '/')).read_bytes()
        assert sha(blob) == meta['sha256']
        assets[name] = blob
    font_manifest = json.loads(args.font_manifest.read_text())
    fonts = {}
    for name in ('font8', 'font10', 'font16', 'font16x'):
        path = 'files\\font\\' + name + '.fnt'
        blob = (args.font_assets / path.replace('\\', '/')).read_bytes()
        assert sha(blob) == font_manifest['assets'][path]['sha256']
        fonts[name] = blob
    ranges = [bounds for name, bounds in source.BLOCKS.items() if not name.endswith('_static')]
    entries = {lo for lo, hi in ranges}
    approved, visited, cases = set(), set(), []
    writes = 0
    def write_hook(uc, access, at, length, value, unused):
        nonlocal writes
        assert any(lo <= at and at + length <= hi for lo, hi in [
            (PRIVATE, PRIVATE + 0x50000), (STACK, STACK + 0x10000),
            (0x6CE65C, 0x6CE65E), (0x6CE6E0, 0x6CE764), (0x6D3008, 0x6D311C)
        ]), ('Undeclared guest write', hex(at), length)
        writes += 1
    machine.hook_add(UC_HOOK_MEM_WRITE, write_hook)
    def call(start, ecx=0, edx=0, stack=(), stop_before=None, cdecl=False):
        done, prefix, steps, calls = False, False, 0, []
        def code_hook(uc, pc, length, unused):
            nonlocal done, prefix, steps
            if pc == STOP:
                done = True
                uc.emu_stop()
                return
            if pc == stop_before:
                prefix = True
                uc.emu_stop()
                return
            if (pc, length) not in approved:
                assert any(lo <= pc and pc + length <= hi for lo, hi in ranges), ('Unreviewed instruction', hex(pc), calls[-8:])
                approved.add((pc, length))
            visited.add(pc)
            steps += 1
            if pc in entries:
                calls.append(hex(pc))
        hook = machine.hook_add(UC_HOOK_CODE, code_hook)
        sp = STACK + 0xF000
        machine.mem_write(sp, struct.pack('<' + 'I' * (len(stack) + 1), STOP, *stack))
        machine.reg_write(UC_X86_REG_ESP, sp)
        machine.reg_write(UC_X86_REG_ECX, ecx & MASK)
        machine.reg_write(UC_X86_REG_EDX, edx & MASK)
        machine.reg_write(UC_X86_REG_EAX, 0)
        machine.reg_write(UC_X86_REG_EFLAGS, 2)
        try:
            machine.emu_start(start, STOP + 1, count=1000000)
        except Exception as error:
            raise RuntimeError((hex(start), hex(machine.reg_read(UC_X86_REG_EIP)), calls[-8:])) from error
        finally:
            machine.hook_del(hook)
        assert done or prefix
        if done:
            assert machine.reg_read(UC_X86_REG_ESP) == sp + 4 * (1 if cdecl else len(stack) + 1)
        return dict(start_va=hex(start), complete_return=done, stopped_before_unexecuted_callee=prefix,
                    stop_va=hex(stop_before) if prefix else None, original_instructions=steps,
                    calls=calls, return_eax=machine.reg_read(UC_X86_REG_EAX))
    def record(family, inputs, actual, expected, execution):
        assert actual == expected, (family, inputs, actual, expected)
        cases.append(dict(case=family, inputs=inputs, actual=actual, expected=expected, passed=True, execution=execution))
    interactions = struct.unpack('<15I', pe.get_data(0xF10F8, 60))
    draws = struct.unpack('<15I', pe.get_data(0xF1148, 60))
    write(0x6D3220, 0)

    def fixup_model(data, first, base, stop_images=False):
        out = bytearray(data)
        def val(at, size=4): return int.from_bytes(out[at:at + size], 'little')
        def put(at, value, size=4): out[at:at + size] = (value & ((1 << (size * 8)) - 1)).to_bytes(size, 'little')
        at = first
        while at is not None:
            nxt, string, flags, kind = val(at), val(at + 0x14), val(at + 0x18), val(at + 0x22, 2)
            if nxt: put(at, base + nxt)
            if string:
                if flags & 0x200:
                    char = val(string, 1)
                    if 65 <= char <= 90: put(string, char + 32, 1)
                put(at + 0x14, base + string)
            if kind == 14:
                cell = at + 0x42
                while val(cell):
                    node = val(cell)
                    put(cell, base + node)
                    filename = val(node + 0xA)
                    if filename: put(node + 0xA, base + filename)
                    cell = node
                x, y = val(at + 4, 2), val(at + 6, 2)
                for offset in (0x46, 0x4A): put(at + offset, val(at + offset, 2) + x, 2)
                for offset in (0x48, 0x4C): put(at + offset, val(at + offset, 2) + y, 2)
            put(at + 0x2A, interactions[kind])
            put(at + 0x2E, draws[kind])
            if kind == 5 and string:
                assert stop_images
                return bytes(out), False
            bitmap = val(at + 0x10)
            if bitmap & 0x80000000:
                signed = bitmap - (1 << 32)
                put(at + 0x10, val(signed + 0x10))
                put(at + 0x18, flags | 0x20)
            if kind == 0:
                child = val(at + 0x42)
                if child: put(at + 0x42, base + child)
                at = child if child else None
            else:
                put(at + 0x32, base)
                at = nxt if nxt else None
        return bytes(out), True

    # Full original file-linked statport chain; other full files stop at real PCX loader.
    for path, blob in assets.items():
        if not path.endswith('.bin'): continue
        machine.mem_write(FILE, blob)
        expected, complete = fixup_model(blob, 0, FILE, True)
        ex = call(0x4C33E0, FILE, FILE, (0x444930, 0, 0), stop_before=None if complete else 0x444930)
        record('authentic_entire_bin_chain_fixup_or_image_boundary', dict(resource=path, file_sha256=sha(blob)),
               dict(fixed_bytes_sha256=sha(bytes(machine.mem_read(FILE, len(blob)))), complete=ex['complete_return']),
               dict(fixed_bytes_sha256=sha(expected), complete=complete), ex)
        # Each authentic widget is isolated by clearing only its list successor.
        for widget in source.audiovisual.bins(blob):
            at, kind = widget['offset'], widget['type']
            isolated = bytearray(blob)
            struct.pack_into('<I', isolated, at if kind else at + 0x42, 0)
            expected, complete = fixup_model(isolated, at, FILE, True)
            machine.mem_write(FILE, bytes(isolated))
            ex = call(0x4C33E0, FILE + at, FILE, (0x444930, 0, 0), stop_before=None if complete else 0x444930)
            record('stock_widget_isolated_fixup_or_image_boundary', dict(resource=path, offset=at, type=kind,
                   identifier=widget['identifier'], declared_fixture_mutation='Successor link cleared; every other file byte retained'),
                   dict(fixed_bytes_sha256=sha(bytes(machine.mem_read(FILE, len(blob)))), complete=ex['complete_return']),
                   dict(fixed_bytes_sha256=sha(expected), complete=complete), ex)
    for value in list(range(256)) + [0xFFFFFFFF, 0xFFFFFF80, 0x7FFFFFFF, 0x80000000]:
        ex = call(0x4D1375, stack=(value,), cdecl=True)
        expected = value + 32 if 65 <= value <= 90 else value
        record('complete_default_locale_lowercase_byte_and_signed_boundaries', dict(value=value, locale_state=0), ex['return_eax'], expected, ex)
    # General fixup profiles independent of stock type membership.
    for kind in range(15):
        for flags in (0, 0x200):
            blob = bytearray(512)
            struct.pack_into('<I', blob, 0x14, 0 if kind == 5 else 200)
            struct.pack_into('<I', blob, 0x18, flags)
            struct.pack_into('<H', blob, 0x22, kind)
            blob[200:204] = b'AbC\0'
            machine.mem_write(FILE, bytes(blob))
            expected, complete = fixup_model(blob, 0, FILE)
            ex = call(0x4C33E0, FILE, FILE, (0x444930, 0, 0))
            record('synthetic_all_15_type_callback_pairs_and_null_image', dict(type=kind, flags=flags, image_text_null=kind == 5),
                   sha(bytes(machine.mem_read(FILE, len(blob)))), sha(expected), ex)
    # The shared-bitmap branch uses a negative *signed relative displacement*,
    # not a positive offset with bit31 merely stripped.
    for bitmap in (0, 1, 0x12345678):
        machine.mem_write(FILE - 256, bytes(768))
        write(FILE - 256 + 0x10, bitmap)
        write(FILE + 0x10, -256)
        write(FILE + 0x22, 9, 2)
        ex = call(0x4C33E0, FILE, FILE, (0x444930, 0, 0))
        record('synthetic_negative_relative_shared_bitmap_reference', dict(relative=-256, referenced_bitmap=bitmap),
               dict(bitmap=read(FILE + 0x10), flags=read(FILE + 0x18)), dict(bitmap=bitmap, flags=0x20), ex)
    ex = call(0x4C33E0, 0, FILE, (0x444930, 0, 0))
    record('null_fixup_has_complete_return', {}, True, ex['complete_return'], ex)

    # Every low-byte UI frame ID of every authenticated stock UI GRP.
    for path, blob in assets.items():
        if not path.endswith('.grp'): continue
        inventory = source.grp(blob)
        machine.mem_write(GRP, blob)
        for frame in range(256):
            machine.mem_write(OUTPUT, bytes([0xA5] * 32))
            row = inventory['frames'][frame if frame < inventory['frame_count'] else 0]
            ex = call(0x4CF570, GRP, frame, (OUTPUT, OUTPUT + 8, OUTPUT + 10, OUTPUT + 12, OUTPUT + 14))
            actual = dict(bitmap_width=read(OUTPUT, 2), bitmap_height=read(OUTPUT + 2, 2),
                          pixel_offset=read(OUTPUT + 4) - GRP, x=read(OUTPUT + 8, 2), y=read(OUTPUT + 10, 2),
                          logical_width=read(OUTPUT + 12, 2), logical_height=read(OUTPUT + 14, 2))
            expected = dict(bitmap_width=row['width'], bitmap_height=row['height'], pixel_offset=row['offset'],
                            x=row['x'], y=row['y'], logical_width=inventory['logical_width'], logical_height=inventory['logical_height'])
            record('all_ui_grp_low_byte_frame_geometry_native_fallback', dict(resource=path, input_frame=frame), actual, expected, ex)
    for argument in (0, 1, 255, 256, 257, 65535, 0xFFFFFFFF):
        machine.mem_write(GRP, struct.pack('<3H4BI', 1, 256, 128, 4, 5, 0, 7, 0x80000020) + bytes(64))
        ex = call(0x4CF570, GRP, argument, (OUTPUT, OUTPUT + 8, OUTPUT + 10, OUTPUT + 12, OUTPUT + 14))
        record('synthetic_raw_256_width_flag_and_frame_u8_truncation', dict(argument=argument),
               dict(width=read(OUTPUT, 2), height=read(OUTPUT + 2, 2), offset=read(OUTPUT + 4) - GRP),
               dict(width=256, height=7, offset=32), ex)

    # Only 48 color indices come from each PCX. The original theme buffer's
    # remaining 144 bytes cross an unexecuted Storm allocator/bitmap boundary.
    # Both suffixes below are explicitly synthetic; no zero-fill is inferred.
    for path, blob in assets.items():
        if not path.endswith('.pcx'): continue
        width, height, pixels = pcx(blob)
        assert len(pixels) == 48
        for suffix in (0, 165):
            bank = pixels + bytes([suffix] * 144)
            for misalignment in range(4):
                machine.mem_write(FILE + misalignment, bank)
                machine.mem_write(0x6D3050, bytes([0x5A] * 192))
                ex = call(0x47CB00, FILE + misalignment)
                record('stock_theme_color_prefix_and_explicit_synthetic_suffix_copy', dict(resource=path, source_alignment=misalignment,
                       width=width, height=height, authentic_prefix_bytes=48, synthetic_suffix_bytes=144, synthetic_suffix=suffix),
                       sha(bytes(machine.mem_read(0x6D3050, 192))), sha(bank), ex)
    # The theme's 30 word destinations can be verified without allocator/file I/O.
    for same in (False, True):
        for profile in range(32):
            values = [((i + 1) * 1009) & 65535 for i in range(30)] if profile == 0 else [65535 if profile - 1 == i else 0 for i in range(30)]
            if profile == 31: values = [65535] * 30
            machine.mem_write(CONTROL, bytes(0x200))
            machine.mem_write(0x6CE6E0, bytes([0xA5] * 0x84))
            write(0x6CE764, CONTROL if same else 0)
            write(CONTROL + 0x10C, GRP)
            for (offset, destination), value in zip(source.THEME_FIELDS, values): write(CONTROL + offset, value, 2)
            ex = call(0x4C31D0, CONTROL)
            actual = dict(grp=read(0x6CE6E0), fields=[read(destination, 2) for offset, destination in source.THEME_FIELDS])
            expected = dict(grp=0xA5A5A5A5 if same else GRP, fields=[0xA5A5] * 30 if same else values)
            record('complete_theme_word_destination_and_skip_pointer_guard', dict(matches_global_skip_pointer=same, profile=profile, declared_native_theme_fixture=True), actual, expected, ex)

    # Pure admission/focus predicates; every flag/identifier boundary is explicit.
    for identifier in (0, 1, 32767, 32768, 65534, 65535):
        for flags in range(32):
            machine.mem_write(CONTROL, bytes(0x60))
            write(CONTROL + 0x20, identifier, 2)
            write(CONTROL + 0x18, flags)
            expected = int(bool(flags & 8) and not (flags & 2) and identifier != 65535 and (bool(identifier & 32768) or bool(flags & 16)))
            ex = call(0x4C2400, CONTROL)
            record('complete_admission_visible_enabled_identifier_responsive', dict(identifier=identifier, flags=flags), ex['return_eax'], expected, ex)
    for kind in range(15):
        for flags in (0, 8, 10, 8 | 0x1000, 10 | 0x1000):
            for active in (0, CONTROL, PARENT):
                for parent_flags in (0, 0x20000000):
                    machine.mem_write(CONTROL, bytes(0x60)); machine.mem_write(PARENT, bytes(0x60))
                    write(CONTROL + 0x18, flags); write(CONTROL + 0x22, kind, 2); write(CONTROL + 0x32, PARENT)
                    write(PARENT + 0x18, parent_flags); write(PARENT + 0x3E, active)
                    selected = bool(flags & 8) and not (flags & 2) and (active == CONTROL or kind == 1 and active == 0 and not (parent_flags & 0x20000000))
                    expected = flags | 0x1000 if selected else flags & ~0x1000
                    ex = call(0x4C2640, CONTROL)
                    record('complete_focus_flag_default_button_and_parent_guard', dict(type=kind, flags=flags, active=active, parent_flags=parent_flags), read(CONTROL + 0x18), expected, ex)
    for flags in (0, MASK, 0x4C00, 0x4000, 0x800, 0x400):
        write(CONTROL + 0x18, flags)
        ex = call(0x4C2680, CONTROL)
        record('clear_native_press_flags', dict(flags=flags), read(CONTROL + 0x18), flags & ~0x4C00, ex)

    # Authentic statport children connect fixup -> ID lookup -> custom bindings.
    blob = assets['rez\\statport.bin']
    for identifier in (0, 1, 2, 3, 65535, 65536, 65537):
        machine.mem_write(FILE, blob)
        expected, _ = fixup_model(blob, 0, FILE)
        ex_fix = call(0x4C33E0, FILE, FILE, (0x444930, 0, 0))
        for from_child in (False, True):
            pointer = FILE + 86 if from_child else FILE
            ex = call(0x4C2200, pointer, identifier)
            low = identifier & 65535
            want = next((FILE + w['offset'] for w in source.audiovisual.bins(blob)[1:] if w['identifier'] == low), 0)
            ex['setup_calls'] = ex_fix['calls']
            record('authentic_statport_fixup_find_child_u16_identifier', dict(identifier=identifier, from_child=from_child), ex['return_eax'], want, ex)
    for callback_mask in range(4):
        machine.mem_write(FILE, blob)
        call(0x4C33E0, FILE, FILE, (0x444930, 0, 0))
        callbacks = [0x4C2400 if callback_mask & 1 else 0, 0x4C2680 if callback_mask & 2 else 0]
        machine.mem_write(OUTPUT, struct.pack('<2I', *callbacks))
        ex = call(0x4C25B0, FILE, OUTPUT, (11,))
        actual = [read(FILE + w['offset'] + 0x2A) for w in source.audiovisual.bins(blob)]
        # The static-text widget has ID0xfff6; signed ID<=0 is skipped.
        expected = [interactions[0], callbacks[0] or interactions[2], interactions[9]]
        record('authentic_statport_identifier_callback_override', dict(callback_mask=callback_mask, callback_byte_argument=11),
               dict(callbacks=actual, extent_word=read(0x6CE65C, 2)), dict(callbacks=expected, extent_word=2), ex)
    for identifier in (0, 1, 2, 32767, 32768, 65535):
        for kind in range(15):
            for supplied in (False, True):
                machine.mem_write(CONTROL, bytes(0x60))
                write(CONTROL + 0x20, identifier, 2); write(CONTROL + 0x22, kind, 2)
                write(CONTROL + 0x2A, interactions[kind])
                callback = 0x4C2400 if supplied else 0
                if 1 <= identifier <= 32767: write(OUTPUT + 4 * (identifier - 1), callback)
                ex = call(0x4C25B0, CONTROL, OUTPUT, (11,))
                expected = callback if kind != 0 and 1 <= identifier <= 32767 and callback else interactions[kind]
                record('synthetic_identifier_binding_signed_boundary_and_root_exclusion', dict(identifier=identifier, type=kind, supplied=supplied),
                       read(CONTROL + 0x2A), expected, ex)

    # Four genuine default-mode fonts reach their unexecuted language query.
    font_names = ['font8', 'font10', 'font16', 'font16x']
    for index, name in enumerate(font_names):
        address = FONT + index * 0x10000
        machine.mem_write(address, fonts[name])
        write(0x6D3024 + index * 4, address)
    for flags in (0, 0x400, 0x800, 0x4000, 0xC00, 0x4400, 0x4800, 0x4C00, 0x10000):
        write(CONTROL + 0x18, flags)
        masked = flags & 0x4C00
        index = {0x400: 0, 0x800: 2, 0x4000: 3}.get(masked, 1)
        ex = call(0x4C3660, CONTROL, stop_before=0x4CE1C5)
        name = font_names[index]; blob = fonts[name]
        record('stock_default_mode_font_selection_prefix_before_language_import', dict(flags=flags, source_font=name, source_sha256=sha(blob), bootstrap_mode_binding='Explicit mode2 (game-start 0x465c40) slots, not executed font loader'),
               dict(font_pointer=read(0x6D3020), width=read(0x6D3036, 1), height=read(0x6D3037, 1)),
               dict(font_pointer=FONT + index * 0x10000, width=blob[6], height=blob[7]), ex)

    dependencies = {Path(source.__file__).name: sha(Path(source.__file__).read_bytes()),
                    Path(source.audiovisual.__file__).name: sha(Path(source.audiovisual.__file__).read_bytes()),
                    'probe_native_pixel_blitters_117.py': sha(Path(__file__).with_name('probe_native_pixel_blitters_117.py').read_bytes())}
    result = dict(target_sha256=source.HASH, target_version='1.17.0.1', original_code_executed=True,
                  complete_game_executed=False, imports_executed=[], helper_stubs=[],
                  probe_script_sha256=sha(Path(__file__).read_bytes()), source_dependencies_sha256=dependencies,
                  asset_manifest_sha256=sha(args.asset_manifest.read_bytes()), font_manifest_sha256=sha(args.font_manifest.read_bytes()),
                  runtime=dict(architecture=platform.machine(), python=sys.version, unicorn=unicorn.__version__),
                  case_count=len(cases), complete_case_count=sum(c['execution']['complete_return'] for c in cases),
                  prefix_case_count=sum(c['execution']['stopped_before_unexecuted_callee'] for c in cases),
                  visited_instruction_count=len(visited), guarded_guest_writes=writes,
                  case_families=dict(Counter(c['case'] for c in cases)), cases=cases,
                  limits=['Full stock BIN loader and dialog initialization do not execute; pure fixup starts with privately extracted bytes',
                          'Image widgets and entire image-bearing chains stop before unchanged444930 Storm bitmap loader',
                          'Isolated widget probes explicitly clear successor; positive image branch is only a prefix',
                          'No nondefault-locale CRT/TLS; actual default-locale lowercase leaf executes unchanged',
                          'Each theme PCX contributes 48 genuine indices; remaining 144 bytes are declared synthetic padding, not a proved original allocator/bitmap result',
                          'Selected font setup stops before GetUserDefaultLangID; font bootstrap bindings are explicit fixture state',
                          'No actual dialog interaction/rendering/screen upload/full campaign UI parity'])
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: result[key] for key in ('case_count', 'complete_case_count', 'prefix_case_count',
                     'visited_instruction_count', 'guarded_guest_writes', 'probe_script_sha256')}))

if __name__ == '__main__':
    main()
