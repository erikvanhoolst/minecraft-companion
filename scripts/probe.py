#!/usr/bin/env python3
"""Live-console helpers for the Minecraft layout research (eden-cli with EDEN_DSMOD_CMD).

  probe.py base                      main base from the Eden log
  probe.py vt <Class>                objfind heap objects whose vptr is this class's primary vtable
  probe.py dump <hexaddr> [len]      hexdump with 8-byte words annotated (main+X, heap ptr, ints)
  probe.py chain <mainoff> <off>...  follow a pointer chain from main+<mainoff>

Uses scripts/duo.py for the console round trip and $MC_ANALYSIS/rtti.json (research/nso_reloc.py)
for the vtables. Env: MC_RUN (default /tmp/mc), EDEN_LOG (default ~/.local/share/eden/log/eden_log.txt)
"""
import json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
RTTI = os.path.join(os.environ.get('MC_ANALYSIS', '.'), 'rtti.json')
DUO = os.path.join(HERE, 'duo.py')
LOG = os.environ.get('EDEN_LOG', os.path.expanduser('~/.local/share/eden/log/eden_log.txt'))

def main_base():
    text = open(LOG, errors='replace').read()
    m = re.findall(r'loaded module main @ 0x([0-9a-f]+)', text)
    if not m:
        sys.exit('main base not found in log (needs Loader:Debug in the filter)')
    return int(m[-1], 16)  # the console reports guest addresses in this same space

def vtable(cls):
    r = json.load(open(RTTI))[cls]
    for ti in r['typeinfos']:
        for vt in ti['vtables']:
            if vt['offset_to_top'] == 0:
                return int(vt['vtable'], 16) + 0x10  # address point: an object's vptr skips offset_to_top and the typeinfo pointer
    sys.exit(f'no primary vtable for {cls}')

def console(*args, timeout=180):
    out = subprocess.run([sys.executable, DUO, 'cmd', *map(str, args)], capture_output=True, text=True, timeout=timeout)
    return out.stdout

def annotate(base, text, heap_lo=0x10_0000_0000, heap_hi=0x80_0000_0000):
    """Re-print a hexdump reply with word annotations."""
    for line in text.splitlines():
        m = re.match(r'\s*(\+[0-9A-F]{4}):((?: [0-9A-F?]{16})+)', line)
        if not m:
            print(line); continue
        words = m.group(2).split()
        notes = []
        for w in words:
            if '?' in w: notes.append('?'); continue
            v = int(w, 16)
            if base <= v < base + 0x11a85000: notes.append(f'main+{v-base:X}')
            elif heap_lo <= v < heap_hi: notes.append('heap')
            elif v < 0x100000000: notes.append(f'{v & 0xffffffff}|{(v>>32)&0xffffffff}' if v >> 32 else str(v))
            else: notes.append('-')
        print(f'{m.group(1)}: ' + ' '.join(f'{w}[{n}]' for w, n in zip(words, notes)))

if __name__ == '__main__':
    a = sys.argv[1:]
    if not a: print(__doc__); sys.exit(1)
    if a[0] == 'base':
        print(hex(main_base()))
    elif a[0] == 'vt':
        base = main_base(); vt = base + vtable(a[1])
        print(f'{a[1]} vtable = main+{vtable(a[1]):X} = {vt:X}; low32 = {vt & 0xffffffff:#x}')
        print(console('objfind', '0', str(vt & 0xffffffff)))
    elif a[0] == 'dump':
        base = main_base()
        annotate(base, console('hexdump', a[1], a[2] if len(a) > 2 else '0x100'))
    elif a[0] == 'chain':
        print(console('chain', *a[1:]))
    else:
        print(__doc__); sys.exit(1)
