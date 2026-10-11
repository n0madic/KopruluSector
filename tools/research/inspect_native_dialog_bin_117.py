#!/usr/bin/env python3
"""Review original BIN fixups, UI GRP geometry and theme/font consumers."""
import argparse, hashlib, json, struct
from pathlib import Path
import capstone, pefile
import inspect_native_audiovisual_117 as audiovisual
from probe_native_pixel_blitters_117 import pcx

HASH = audiovisual.HASH
BLOCKS = {
    'bin_fixup': (0x4C33E0, 0x4C34D1),
    'ascii_lowercase_default_locale': (0x4D1375, 0x4D13A1),
    'find_child': (0x4C2200, 0x4C2220),
    'control_admission': (0x4C2400, 0x4C2430),
    'bind_identifier_callbacks': (0x4C25B0, 0x4C25FF),
    'focus_display_flag': (0x4C2640, 0x4C267B),
    'clear_press_flags': (0x4C2680, 0x4C2688),
    'ui_grp_frame': (0x4CF570, 0x4CF5DF),
    'theme_apply': (0x4C31D0, 0x4C336E),
    'text_color_bank_copy': (0x47CB00, 0x47CB13),
    'font_select': (0x4C3660, 0x4C36A9),
    'font_setup': (0x4CE160, 0x4CE23F),
    'bin_file_loader_static': (0x4C33A0, 0x4C33DA),
    'theme_allocate_build_static': (0x4C2CA0, 0x4C31C7),
    'dialog_init_static': (0x4C2A90, 0x4C2B16),
    'ui_font_text_draw_static': (0x4C3E70, 0x4C3FA4),
    'text_color_file_load_static': (0x47CA80, 0x47CAB2),
    'font_bootstrap_mode2_static': (0x465C40, 0x465CD7),
    'font_bootstrap_mode1_static': (0x465CE0, 0x465D77),
    'theme_resource_load_static': (0x458550, 0x4585D2),
    'theme_color_bank_load_static': (0x458690, 0x458734),
    'theme_color_bank_select_static': (0x458075, 0x4580B0),
    'bitmap_resource_import_static': (0x4444E0, 0x444513),
}
THEME_FIELDS = [
    (0x110, 0x6CE6EC), (0x112, 0x6CE6F0), (0x114, 0x6CE6FC),
    (0x116, 0x6CE700), (0x118, 0x6CE6F4), (0x11A, 0x6CE6F8),
    (0x11C, 0x6CE704), (0x11E, 0x6CE708), (0x120, 0x6CE70C),
    (0x122, 0x6CE710), (0x124, 0x6CE714), (0x126, 0x6CE718),
    (0x128, 0x6CE71C), (0x12A, 0x6CE720), (0x12C, 0x6CE728),
    (0x12E, 0x6CE72C), (0x130, 0x6CE724), (0x132, 0x6CE730),
    (0x134, 0x6CE734), (0x136, 0x6CE738), (0x138, 0x6CE73C),
    (0x13A, 0x6CE744), (0x13C, 0x6CE748), (0x13E, 0x6CE740),
    (0x140, 0x6CE74C), (0x142, 0x6CE750), (0x144, 0x6CE754),
    (0x146, 0x6CE758), (0x148, 0x6CE75C), (0x14A, 0x6CE760),
]

def sha(b):
    return hashlib.sha256(b).hexdigest()

def grp(b):
    count, width, height = struct.unpack_from('<3H', b)
    rows = []
    for index in range(count):
        x, y, w, h, offset = struct.unpack_from('<4BI', b, 6 + 8 * index)
        assert (offset & 0x7FFFFFFF) < len(b)
        rows.append(dict(index=index, x=x, y=y, width=w + (256 if offset & 0x80000000 else 0),
                         height=h, offset=offset & 0x7FFFFFFF, raw_flag=bool(offset & 0x80000000)))
    return dict(frame_count=count, logical_width=width, logical_height=height, frames=rows)

def smk_links(blob, widgets):
    result = []
    for widget in widgets:
        if widget['type'] != 14:
            continue
        at = struct.unpack_from('<I', blob, widget['offset'] + 0x42)[0]
        seen = set()
        nodes = []
        while at:
            assert at not in seen and at + 30 <= len(blob)
            seen.add(at)
            nxt, flags, unknown, filename, unknown2, x, y, unknown3, unknown4 = struct.unpack_from('<LH3LHHLL', blob, at)
            text = blob[filename:blob.index(0, filename)] if filename else b''
            nodes.append(dict(offset=at, next_offset=nxt, flags=flags, filename_offset=filename,
                              filename_bytes=len(text), filename_sha256=sha(text), x=x, y=y))
            at = nxt
        result.append(dict(widget_offset=widget['offset'], identifier=widget['identifier'], nodes=nodes))
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('exe', 'assets', 'asset-manifest', 'out'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    raw = args.exe.read_bytes()
    assert sha(raw) == HASH
    pe = pefile.PE(data=raw)
    dis = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    blocks = {}
    for name, (lo, hi) in BLOCKS.items():
        blob = pe.get_data(lo - 0x400000, hi - lo)
        instructions = list(dis.disasm(blob, lo))
        assert sum(i.size for i in instructions) == len(blob), name
        blocks[name] = [dict(va=hex(i.address), rva=hex(i.address - 0x400000),
                            file_offset=hex(pe.get_offset_from_rva(i.address - 0x400000)),
                            bytes=i.bytes.hex(), mnemonic=i.mnemonic, operands=i.op_str)
                        for i in instructions]
    assets = json.loads(args.asset_manifest.read_text())
    resources = {}
    for name, meta in assets['assets'].items():
        blob = (args.assets / name.replace('\\', '/')).read_bytes()
        assert sha(blob) == meta['sha256']
        extra = {}
        if name.endswith('.bin'):
            extra['widgets'] = audiovisual.bins(blob)
            extra['smk_links'] = smk_links(blob, extra['widgets'])
        elif name.endswith('.grp'):
            extra['grp'] = grp(blob)
        else:
            width, height, pixels = pcx(blob)
            extra['pcx'] = dict(width=width, height=height, decoded_bytes=len(pixels),
                               indexed_sha256=sha(pixels), color_rows=len(pixels) // 8)
        resources[name] = dict(**meta, **extra)
    callbacks = dict(interaction_va='0x4f10f8', draw_va='0x4f1148',
                     interaction=[hex(x) for x in struct.unpack('<15I', pe.get_data(0xF10F8, 60))],
                     draw=[hex(x) for x in struct.unpack('<15I', pe.get_data(0xF1148, 60))])
    result = dict(target_sha256=HASH, original_code_executed=False,
                  extractor_sha256=sha(Path(__file__).read_bytes()),
                  source_dependencies_sha256={Path(audiovisual.__file__).name: sha(Path(audiovisual.__file__).read_bytes()),
                                              'probe_native_pixel_blitters_117.py': sha(Path(__file__).with_name('probe_native_pixel_blitters_117.py').read_bytes())},
                  asset_manifest_sha256=sha(args.asset_manifest.read_bytes()), blocks=blocks,
                  callbacks=callbacks, theme_field_destinations=[[hex(x), hex(y)] for x, y in THEME_FIELDS],
                  resources=resources,
                  limits=['File/bitmap loading, Storm allocation, dialog initialization and timer creation are static boundaries',
                          'Default-locale ASCII lowercase is a complete EXE leaf; nondefault locale/TLS is excluded',
                          'Raw resource metrics and private color indices are not original display/Windows observations'])
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(blocks=len(blocks), instructions=sum(map(len, blocks.values())),
                          resources=len(resources), extractor_sha256=result['extractor_sha256'])))

if __name__ == '__main__':
    main()
