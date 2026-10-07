#!/usr/bin/env python3
"""Prototype of the item-icon lookup the native module performs against the player's romfs.

Minecraft Bedrock stacks its vanilla resource packs (vanilla_base, vanilla, vanilla_1.14 ...
vanilla_<latest>); later packs override earlier ones. An item's icon name (Item::mIconName, usually
the short item name) is looked up in textures/item_texture.json; a block item's icon is derived from
blocks.json (block -> terrain texture key, prefer a side face) and textures/terrain_texture.json.

usage: icon_resolver.py <romfs dir> <name> [aux]      e.g. icon_resolver.py romfs diamond_sword
       icon_resolver.py <romfs dir> --selftest
"""
import json, os, re, sys

def lenient_json(path):
    s = open(path, encoding='utf-8-sig').read()
    s = re.sub(r'//[^\n]*', '', s)            # line comments (blocks.json uses them)
    s = re.sub(r',(\s*[}\]])', r'\1', s)      # trailing commas
    return json.loads(s)

def pack_order(packs_dir):
    """vanilla packs oldest first: vanilla_base, vanilla, then vanilla_<version> ascending."""
    out = []
    for name in os.listdir(packs_dir):
        if name == 'vanilla_base': key = (0, ())
        elif name == 'vanilla': key = (1, ())
        elif name.startswith('vanilla_'):
            ver = name[len('vanilla_'):]
            if not re.fullmatch(r'[0-9.]+', ver): continue      # vanilla_music etc.
            key = (2, tuple(int(x) for x in ver.split('.')))
        else: continue
        out.append((key, name))
    return [n for _, n in sorted(out)]

class Resolver:
    def __init__(self, romfs):
        self.romfs = romfs
        packs_dir = os.path.join(romfs, 'resource_packs')
        self.packs = pack_order(packs_dir)            # oldest .. newest
        self.item_tex = {}      # icon name -> textures entry (newest pack wins)
        self.terrain_tex = {}   # terrain key -> textures entry
        self.blocks = {}        # block name -> block entry
        self.lang = {}          # en_US.lang key -> text
        for p in self.packs:
            base = os.path.join(packs_dir, p)
            f = os.path.join(base, 'textures', 'item_texture.json')
            if os.path.isfile(f): self.item_tex.update(lenient_json(f).get('texture_data', {}))
            f = os.path.join(base, 'textures', 'terrain_texture.json')
            if os.path.isfile(f): self.terrain_tex.update(lenient_json(f).get('texture_data', {}))
            f = os.path.join(base, 'blocks.json')
            if os.path.isfile(f):
                for k, v in lenient_json(f).items():
                    if k != 'format_version' and isinstance(v, dict): self.blocks[k] = v
            f = os.path.join(base, 'texts', 'en_US.lang')
            if os.path.isfile(f):
                for line in open(f, encoding='utf-8-sig', errors='replace'):
                    line = line.split('#', 1)[0].rstrip('\r\n')   # trailing ## comments
                    if '=' in line:
                        k, _, v = line.partition('='); self.lang[k.strip()] = v.strip()

    @staticmethod
    def _pick(textures, aux):
        """textures: str | [str|{path}] | {path}; aux selects a variant in a list."""
        if isinstance(textures, dict): textures = textures.get('path')
        if isinstance(textures, list):
            if not textures: return None
            textures = textures[aux if 0 <= aux < len(textures) else 0]
            if isinstance(textures, dict): textures = textures.get('path')
        return textures if isinstance(textures, str) else None

    def find_file(self, rel):
        """A texture path has no extension; the pack that defines the newest file wins."""
        for p in reversed(self.packs):
            for ext in ('.png', '.tga', '.jpg'):
                f = os.path.join(self.romfs, 'resource_packs', p, rel + ext)
                if os.path.isfile(f): return f
        return None

    def item_icon(self, icon_name, aux=0):
        e = self.item_tex.get(icon_name)
        if e is None: return None
        rel = self._pick(e.get('textures'), aux)
        return self.find_file(rel) if rel else None

    def block_icon(self, block_name, aux=0):
        b = self.blocks.get(block_name)
        if b is None: return None
        t = b.get('textures')
        if isinstance(t, dict):
            key = t.get('side') or t.get('north') or t.get('east') or t.get('west') or t.get('south') or t.get('up') or t.get('down')
        else: key = t
        if not isinstance(key, str): return None
        e = self.terrain_tex.get(key)
        if e is None: return None
        rel = self._pick(e.get('textures'), aux)
        return self.find_file(rel) if rel else None

    BLOCK_ALIASES = {'grass_block': 'grass'}

    def icon(self, full_name, frame=0, icon_name=None, block_name=None):
        """full_name like minecraft:diamond_sword. The game keys item art by Item::mIconName plus
        Item::mIconFrame (an index into the texture array); a block item has no icon name and
        draws its block, for which we take a side face from blocks.json."""
        short = full_name.split(':', 1)[-1]
        if icon_name:
            f = self.item_icon(icon_name, frame)
            if f: return f
        if block_name or not icon_name:
            b = (block_name or full_name).split(':', 1)[-1]
            f = self.block_icon(b, 0) or self.block_icon(self.BLOCK_ALIASES.get(b, b), 0)
            if f: return f
        return self.item_icon(short, frame)

    def display_name(self, full_name, aux=0):
        short = full_name.split(':', 1)[-1]
        for k in (f'item.{short}.name', f'tile.{short}.name', f'tile.{short}.{short}.name'):
            if k in self.lang: return self.lang[k]
        return None

if __name__ == '__main__':
    romfs = sys.argv[1]
    r = Resolver(romfs)
    print(f'{len(r.packs)} packs, {len(r.item_tex)} item textures, {len(r.terrain_tex)} terrain textures, {len(r.blocks)} blocks, {len(r.lang)} lang keys')
    if sys.argv[2] == '--selftest':
        tests = ['minecraft:diamond_sword', 'minecraft:apple', 'minecraft:stone', 'minecraft:grass_block', 'minecraft:oak_log',
                 'minecraft:crafting_table', 'minecraft:torch', 'minecraft:cobblestone', 'minecraft:dirt', 'minecraft:oak_planks',
                 'minecraft:bed', 'minecraft:iron_pickaxe', 'minecraft:bread', 'minecraft:cooked_beef', 'minecraft:wooden_sword',
                 'minecraft:stick', 'minecraft:coal', 'minecraft:furnace', 'minecraft:chest', 'minecraft:bucket', 'minecraft:water_bucket',
                 'minecraft:spear', 'minecraft:copper_sword', 'minecraft:tnt', 'minecraft:sand', 'minecraft:bookshelf']
        frames = {'minecraft:diamond_sword': ('sword', 4), 'minecraft:wooden_sword': ('sword', 0), 'minecraft:copper_sword': ('sword', 5),
                  'minecraft:iron_pickaxe': ('pickaxe', 2), 'minecraft:water_bucket': ('bucket', 8), 'minecraft:cooked_beef': ('beef_cooked', 0),
                  'minecraft:spear': ('wood_spear', 0)}
        miss = 0
        for t in tests:
            ic, fr = frames.get(t, (None, 0))
            f = r.icon(t, fr, ic); n = r.display_name(t)
            if not f: miss += 1
            print(f'{t:28s} {n!s:22s} {os.path.relpath(f, romfs) if f else "-- MISSING --"}')
        print('missing:', miss, 'of', len(tests))
    else:
        aux = int(sys.argv[3]) if len(sys.argv) > 3 else 0
        print(r.icon(sys.argv[2], aux), r.display_name(sys.argv[2]))
