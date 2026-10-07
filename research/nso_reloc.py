#!/usr/bin/env python3
"""Relocate a decompressed Switch NSO image (as written by `hactool --uncompressed`) so that
data pointers hold their runtime values (main+X), then locate Itanium-ABI RTTI typeinfo objects
and the vtables that reference them.

usage: nso_reloc.py <main.bin> <out_dir> [ClassName ...]
  writes <out_dir>/main_reloc.bin and <out_dir>/rtti.json
"""
import json, struct, sys, os, re
from collections import defaultdict

def u32(b, o): return struct.unpack_from('<I', b, o)[0]
def u64(b, o): return struct.unpack_from('<Q', b, o)[0]
def s64(b, o): return struct.unpack_from('<q', b, o)[0]
def s32(b, o): return struct.unpack_from('<i', b, o)[0]

def load_image(path):
    data = bytearray(open(path, 'rb').read())
    if data[:4] == b'NSO0':
        # NSO file with header: segments at (file_off, mem_off, size)
        segs = []
        for i in range(3):
            fo, mo, sz = u32(data, 0x10 + i*0x10), u32(data, 0x14 + i*0x10), u32(data, 0x18 + i*0x10)
            segs.append((fo, mo, sz))
        bss = u32(data, 0x3C)
        total = max(mo+sz for fo, mo, sz in segs)
        img = bytearray(total + bss)
        for fo, mo, sz in segs:
            img[mo:mo+sz] = data[fo:fo+sz]
        return img, segs
    return data, None

def main():
    src, out_dir = sys.argv[1], sys.argv[2]
    classes = sys.argv[3:]
    os.makedirs(out_dir, exist_ok=True)
    img, segs = load_image(src)
    size = len(img)
    mod0_off = u32(img, 4)
    assert img[mod0_off:mod0_off+4] == b'MOD0', 'MOD0 not found'
    dyn = mod0_off + s32(img, mod0_off + 4)
    bss_start = mod0_off + s32(img, mod0_off + 8)
    bss_end = mod0_off + s32(img, mod0_off + 12)
    print(f'image size 0x{size:x} MOD0 @0x{mod0_off:x} dynamic @0x{dyn:x} bss 0x{bss_start:x}-0x{bss_end:x}')
    if bss_end > size:
        img.extend(b'\0' * (bss_end - size))
    tags = {}
    o = dyn
    while True:
        tag, val = s64(img, o), u64(img, o+8)
        if tag == 0: break
        tags.setdefault(tag, val)
        o += 16
    DT_RELA, DT_RELASZ, DT_JMPREL, DT_PLTRELSZ, DT_SYMTAB = 7, 8, 23, 2, 6
    symtab = tags.get(DT_SYMTAB, 0)
    def sym_value(idx):
        if not symtab: return None
        st = symtab + idx*24
        shndx = struct.unpack_from('<H', img, st+6)[0]
        val = u64(img, st+8)
        return val if shndx != 0 else None
    applied = defaultdict(int); skipped = defaultdict(int)
    def apply(table, tsize):
        for e in range(table, table+tsize, 24):
            r_off, r_info, r_add = u64(img, e), u64(img, e+8), s64(img, e+16)
            rtype, rsym = r_info & 0xffffffff, r_info >> 32
            if r_off + 8 > len(img): skipped[rtype] += 1; continue
            if rtype == 1027:   # R_AARCH64_RELATIVE
                struct.pack_into('<Q', img, r_off, r_add & 0xffffffffffffffff); applied[rtype] += 1
            elif rtype in (257, 1025, 1026):  # ABS64, GLOB_DAT, JUMP_SLOT
                v = sym_value(rsym)
                if v is None: skipped[rtype] += 1; continue
                struct.pack_into('<Q', img, r_off, (v + r_add) & 0xffffffffffffffff); applied[rtype] += 1
            else:
                skipped[rtype] += 1
    if DT_RELA in tags: apply(tags[DT_RELA], tags[DT_RELASZ])
    if DT_JMPREL in tags: apply(tags[DT_JMPREL], tags[DT_PLTRELSZ])
    print('relocations applied:', dict(applied), 'skipped:', dict(skipped))
    open(os.path.join(out_dir, 'main_reloc.bin'), 'wb').write(img)

    # ---- RTTI ----
    # index all 8-byte aligned pointers in the data segment (where typeinfo/vtables live)
    data_start = segs[2][1] if segs else None
    if data_start is None:
        # flat image: guess data start as the first segment that holds the dynamic section
        data_start = (dyn // 0x1000) * 0x1000
        # move back to the page where relocations begin: use min r_off
        data_start = min(data_start, u64(img, tags[DT_RELA]) & ~0xfff) if DT_RELA in tags else data_start
    ptr_index = defaultdict(list)
    for off in range(0, len(img) - 8, 8):
        v = u64(img, off)
        if v and v < len(img):
            ptr_index[v].append(off)
    print('pointer index built:', len(ptr_index), 'distinct targets')

    blob = bytes(img)
    result = {}
    def find_name(name):
        # Itanium mangled type name without _ZTS prefix, e.g. "9ItemStack". Prefer NUL-delimited
        # hits; fall back to suffix hits (linker tail-merged strings share their terminator).
        pat = name.encode() + b'\0'
        hits = [m.start() for m in re.finditer(re.escape(pat), blob)]
        exact = [h for h in hits if h == 0 or blob[h-1] == 0]
        return exact if exact else hits
    for cls in classes:
        mangled = f'{len(cls)}{cls}' if not cls[0].isdigit() and not cls.startswith('N') else cls
        names = find_name(mangled)
        entry = {'mangled': mangled, 'name_addrs': [hex(a) for a in names], 'typeinfos': []}
        for a in names:
            for ti_name_field in ptr_index.get(a, []):
                ti = ti_name_field - 8
                ti_entry = {'typeinfo': hex(ti), 'vtables': []}
                # vtables reference the typeinfo at vtable+8
                for ref in ptr_index.get(ti, []):
                    off_to_top = s64(img, ref - 8)
                    if -0x10000 < off_to_top <= 0:
                        vt = ref - 8
                        fns = []
                        o2 = vt + 16
                        while o2 + 8 <= len(img):
                            f = u64(img, o2)
                            if not (0 < f < 0x0ce64000 + 0x1000000) or f % 4: break
                            fns.append(f); o2 += 8
                            if len(fns) > 400: break
                        ti_entry['vtables'].append({'vtable': hex(vt), 'offset_to_top': off_to_top,
                                                    'fn_count': len(fns), 'first_fns': [hex(x) for x in fns[:6]]})
                entry['typeinfos'].append(ti_entry)
        result[cls] = entry
        print(cls, '->', json.dumps(entry)[:400])
    json.dump(result, open(os.path.join(out_dir, 'rtti.json'), 'w'), indent=1)

if __name__ == '__main__':
    main()
