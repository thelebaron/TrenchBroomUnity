"""Generate E1M10: The Sluice Works, an Episode 1 continuation map (Quake standard .map).

Writes source only; it never runs a BSP compiler.

    python build_map.py                 # full map -> e1m10.map + qa/source_checks.json
    python build_map.py --stage 2       # partial build for progress captures
    python build_map.py --cutaway       # omit ceilings/sky (overview captures only)
"""
from __future__ import annotations

import argparse
import json
import math
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent
WAD = "C:/Users/thele/Downloads/quake_map_source-master/gfx/base.wad"
T = 16  # shell thickness

# Stages used for progress captures.
BLOCKOUT, STRUCTURE, DETAIL, POPULATE = 1, 2, 3, 4
stage = BLOCKOUT

NOT_EASY, NOT_MEDIUM, NOT_HARD = 256, 512, 1024
AMBUSH = 1

# ---------------------------------------------------------------------------
# Themes

THEMES = {
    "gate":  dict(wall="tech08_1", floor="sfloor4_2", ceil="ecop1_1", base="tech04_1", top="tech04_5", pil="tech04_3"),
    "corr":  dict(wall="tech06_1", floor="sfloor4_2", ceil="ecop1_1", base="tech04_1", top="tech04_5", pil="tech04_3"),
    "hall":  dict(wall="tech07_2", floor="sfloor4_5", ceil="tech05_1", base="tech04_1", top="tech04_6", pil="tech04_3"),
    "pit":   dict(wall="tech01_6", floor="tech01_6", ceil="tech01_6", base="tech01_6", top="tech01_6", pil="tech01_6"),
    "pump":  dict(wall="twall2_3", floor="sfloor4_7", ceil="tech04_3", base="tech04_2", top="tech04_2", pil="tech04_7"),
    "under": dict(wall="twall5_1", floor="ground1_6", ceil="twall5_3", base="twall5_3", top="twall5_3", pil="twall5_3"),
    "wet":   dict(wall="twall5_3", floor="twall5_3", ceil="twall5_3", base="twall5_3", top="twall5_3", pil="twall5_3"),
    "yard":  dict(wall="tech13_2", floor="ground1_2", ceil="sky4", base="tech04_5", top="tech04_5", pil="tech14_2"),
    "tower": dict(wall="tech06_1", floor="sfloor4_1", ceil="ecop1_4", base="tech04_1", top="tech04_5", pil="tech04_3"),
    "arena": dict(wall="tech05_2", floor="sfloor4_8", ceil="tech13_2", base="tech04_5", top="tech04_6", pil="tech14_2"),
    "vault": dict(wall="tech01_7", floor="sfloor4_2", ceil="tech01_7", base="tech01_7", top="tech01_7", pil="tech01_7"),
}

STEP = dict(up="sfloor4_6", default="tech04_2")
PLAT = dict(up="plat_top1", down="plat_top1", default="plat_side1")
CRATE = dict(up="crate0_top", down="crate0_top", default="crate0_side")
METAL = "tech01_1"

# ---------------------------------------------------------------------------
# Geometry primitives

Box = tuple  # (x1, y1, z1, x2, y2, z2)


def overlap(a: Box, b: Box, eps: float = 0) -> bool:
    return all(a[i] < b[i + 3] - eps and b[i] < a[i + 3] - eps for i in range(3))


def contains_point(b: Box, p) -> bool:
    return all(b[i] <= p[i] <= b[i + 3] for i in range(3))


def subtract(p: Box, c: Box) -> list[Box]:
    if not overlap(p, c):
        return [p]
    out, p = [], list(p)
    for ax in range(3):
        if p[ax] < c[ax]:
            q = p.copy(); q[ax + 3] = c[ax]; out.append(tuple(q)); p[ax] = c[ax]
        if p[ax + 3] > c[ax + 3]:
            q = p.copy(); q[ax] = c[ax + 3]; out.append(tuple(q)); p[ax + 3] = c[ax + 3]
    return out


def fmt(p) -> str:
    return "( " + " ".join(f"{v:g}" for v in p) + " )"


class Brush:
    def __init__(self, points, faces, textures, aabb, stage_):
        self.points, self.faces, self.textures, self.aabb, self.stage = points, faces, textures, aabb, stage_
        self.joint = False  # deliberately interpenetrates its neighbour (collar, cap, lintel)

    def text(self) -> str:
        pts = self.points
        center = [sum(p[a] for p in pts) / len(pts) for a in range(3)]
        lines = []
        for i, face in enumerate(self.faces):
            a, b, c = [pts[j] for j in face]
            u = [b[k] - a[k] for k in range(3)]
            v = [c[k] - a[k] for k in range(3)]
            n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            if sum(n[k] * (center[k] - a[k]) for k in range(3)) < 0:
                b, c = c, b
            lines.append(f"{fmt(a)} {fmt(b)} {fmt(c)} {self.textures[i]} 0 0 0 1 1")
        return "{\n" + "\n".join(lines) + "\n}"


BOX_FACES = [(0, 1, 2), (4, 7, 6), (0, 4, 5), (1, 5, 6), (2, 6, 7), (3, 7, 4)]
FACE_NAMES = ("down", "up", "south", "east", "north", "west")


def make_box(b: Box, tex) -> Brush:
    x1, y1, z1, x2, y2, z2 = b
    assert x2 > x1 and y2 > y1 and z2 > z1, b
    pts = [(x1, y1, z1), (x2, y1, z1), (x2, y2, z1), (x1, y2, z1),
           (x1, y1, z2), (x2, y1, z2), (x2, y2, z2), (x1, y2, z2)]
    if isinstance(tex, dict):
        texs = [tex.get(f, tex.get("default", METAL)) for f in FACE_NAMES]
    else:
        texs = [tex] * 6
    return Brush(pts, BOX_FACES, texs, b, stage)


def make_prism(cx, cy, r, n, z1, z2, tex_side, tex_cap) -> Brush:
    ring = [(cx + r * math.cos(2 * math.pi * (i + 0.5) / n), cy + r * math.sin(2 * math.pi * (i + 0.5) / n)) for i in range(n)]
    ring = [(round(x), round(y)) for x, y in ring]
    pts = [(x, y, z1) for x, y in ring] + [(x, y, z2) for x, y in ring]
    faces = [(0, 1, 2), (n, n + 1, n + 2)] + [(i, (i + 1) % n, n + i) for i in range(n)]
    texs = [tex_cap, tex_cap] + [tex_side] * n
    aabb = (cx - r, cy - r, z1, cx + r, cy + r, z2)
    return Brush(pts, faces, texs, aabb, stage)


# ---------------------------------------------------------------------------
# Scene containers

class Space:
    def __init__(self, name, box, theme, trim=True, pilasters=True, kind="room", pil=None):
        self.name, self.box, self.theme, self.trim, self.pilasters, self.kind = name, box, THEMES[theme], trim, pilasters, kind
        # Pilaster spec: spacing, width, depth, height above floor (None = full), cap texture.
        self.pil = dict(spacing=192, width=32, depth=8, height=None, cap=None) | (pil or {})
        self.stage = BLOCKOUT


spaces: dict[str, Space] = {}
detail: list[Brush] = []        # world brushes placed inside air
entities: list[dict] = []       # {"props":..., "brushes":[...], "stage":..}


def space(name, box, theme, **kw) -> Space:
    s = Space(name, box, theme, **kw)
    spaces[name] = s
    return s


def solid(b: Box, tex=METAL, joint=False) -> Brush:
    br = make_box(b, tex)
    br.joint = joint
    detail.append(br)
    return br


def prism(cx, cy, r, z1, z2, side, cap, n=8, joint=False) -> Brush:
    br = make_prism(cx, cy, r, n, z1, z2, side, cap)
    br.joint = joint
    detail.append(br)
    return br


def ent(classname, origin=None, brushes=None, **props) -> dict:
    p = {"classname": classname}
    if origin is not None:
        p["origin"] = " ".join(f"{v:g}" for v in origin)
    for k, v in props.items():
        p[k] = str(v)
    e = {"props": p, "brushes": brushes or [], "stage": stage}
    entities.append(e)
    return e


def thin_axis(b: Box) -> int:
    d = [b[i + 3] - b[i] for i in range(3)]
    return d.index(min(d[:2]))


def door_tex(b: Box, big="edoor01_1", edge="door02_1") -> dict:
    ax = thin_axis(b)
    big_faces = ("west", "east") if ax == 0 else ("south", "north")
    tex = {"default": edge}
    for f in big_faces:
        tex[f] = big
    return tex


def door(b: Box, tex=None, **props) -> dict:
    return ent("func_door", brushes=[make_box(b, tex or door_tex(b))], **props)


def stairs(along, start, depth, count, cross, top0, rise, base, tex=STEP):
    """Solid steps. `along` is 'x+','x-','y+','y-'; cross is the (lo, hi) span on the other axis."""
    sign = 1 if along[1] == "+" else -1
    for k in range(count):
        a = start + sign * depth * k
        b = a + sign * depth
        top = top0 + rise * (k + 1)
        if top <= base:
            continue
        lo, hi = min(a, b), max(a, b)
        if along[0] == "x":
            solid((lo, cross[0], base, hi, cross[1], top), tex)
        else:
            solid((cross[0], lo, base, cross[1], hi, top), tex)


def light(x, y, z, v=200, **kw):
    return ent("light", (x, y, z), light=v, **kw)


def monster(cls, x, y, floor, angle, flags=0, **kw):
    kw = {k: v for k, v in kw.items()}
    if flags:
        kw["spawnflags"] = flags
    return ent(cls, (x, y, floor + 24), angle=angle, **kw)


def item(cls, x, y, floor, flags=0, **kw):
    if flags:
        kw["spawnflags"] = flags
    return ent(cls, (x, y, floor + 8), **kw)


# ---------------------------------------------------------------------------
# Layout: air volumes. Rooms (kind=room) receive trims, pilasters and lights;
# connectors are door/window cut-throughs that only remove wall.

def layout():
    # Arrival gate: the player steps out of the E1 slipgate network here.
    space("arrival", (-256, 0, 0, 256, 448, 224), "gate")
    space("s1_closet", (256, 176, 0, 400, 288, 112), "vault", trim=False, pilasters=False, kind="secret")
    space("intake", (-64, 448, 0, 64, 768, 128), "corr")
    space("gallery", (-64, 768, 0, 896, 896, 128), "corr")
    for i, x in enumerate((64, 288, 512)):
        space(f"window{i}", (x, 896, 40, x + 96, 912, 104), "corr", kind="window")
    space("hall_entry", (768, 896, 0, 896, 912, 128), "corr", kind="conn")

    # The Sluice Hall: slime canal, retracted bridge, upper catwalk ring.
    space("hall", (-128, 912, -128, 896, 1680, 384), "hall", pil=dict(spacing=256, width=48, depth=24))
    space("hall_pit", (-128, 1232, -320, 896, 1488, -128), "pit", trim=False, pilasters=False, kind="pit")
    space("lift_shaft", (352, 1536, -448, 480, 1664, -128), "pit", trim=False, pilasters=False, kind="shaft")
    space("s4_alcove", (896, 1424, 160, 1008, 1520, 256), "vault", trim=False, pilasters=False, kind="secret")

    # Pump room: bridge control.
    space("pump_door", (-144, 1120, -128, -128, 1216, -16), "pump", kind="conn")
    space("pump", (-576, 1040, -128, -144, 1424, 160), "pump")
    space("s3_closet", (-416, 928, -128, -320, 1040, -32), "vault", trim=False, pilasters=False, kind="secret")

    # Underworks: lift landing, flooded channel, drowned cache.
    space("landing", (256, 1440, -448, 560, 1744, -288), "under")
    space("landing_door", (560, 1504, -448, 576, 1552, -336), "under", kind="conn")
    space("channel", (576, 1504, -448, 1472, 1680, -320), "under")
    space("trough", (576, 1552, -576, 1472, 1632, -448), "wet", trim=False, pilasters=False, kind="pit")
    space("s2_tunnel", (960, 1424, -576, 1056, 1552, -480), "wet", trim=False, pilasters=False, kind="secret")
    space("s2_cache", (928, 1296, -576, 1088, 1424, -416), "vault", trim=False, pilasters=False, kind="secret")
    space("yard_stair", (1344, 1680, -448, 1472, 1984, -128), "under", pilasters=False)

    # Outdoor yard (open to the sky), guard post, key ambush closet, crate-climb secret.
    space("yard", (1216, 1984, -256, 2112, 2880, 192), "yard",
          pil=dict(spacing=224, width=64, depth=32, height=320, cap="tech04_6"))
    space("tower_window0", (1088, 2000, 144, 1216, 2080, 192), "tower", kind="window")
    space("tower_window1", (1088, 2224, 144, 1216, 2304, 192), "tower", kind="window")
    space("pool", (1664, 2368, -384, 1920, 2688, -256), "wet", trim=False, pilasters=False, kind="pit")
    space("guard_door", (2112, 2320, -256, 2128, 2416, -144), "corr", kind="conn")
    space("guard", (2128, 2240, -256, 2368, 2496, -96), "corr")
    space("key_closet", (1536, 2880, -256, 1664, 2992, -144), "vault", trim=False, pilasters=False, kind="closet")
    space("s5_alcove", (2112, 2800, -64, 2240, 2880, 48), "vault", trim=False, pilasters=False, kind="secret")

    # Control tower and the high corridor back to the hall's catwalk.
    space("tower_door", (1088, 2192, -64, 1216, 2272, 48), "tower", kind="conn")
    space("tower", (640, 1984, -64, 1088, 2368, 320), "tower")
    space("high_corr", (640, 1680, 128, 768, 1984, 256), "corr")

    # Exit wing behind the silver door, and the relay arena.
    space("silver_door", (-144, 1584, 128, -128, 1680, 240), "corr", kind="conn")
    space("exit_corr", (-576, 1584, 128, -144, 1680, 256), "corr")
    space("exit_corr2", (-576, 1680, 128, -448, 1968, 256), "corr")
    space("s6_closet", (-704, 1792, 128, -576, 1888, 224), "vault", trim=False, pilasters=False, kind="secret")
    space("arena", (-1088, 1968, -128, -320, 2736, 384), "arena", pil=dict(spacing=192, width=48, depth=24))
    space("moat", (-1088, 2256, -320, -320, 2608, -128), "pit", trim=False, pilasters=False, kind="pit")
    space("closet_e", (-320, 2112, -128, -192, 2240, 0), "vault", trim=False, pilasters=False, kind="closet")
    space("closet_w", (-1216, 2112, -128, -1088, 2240, 0), "vault", trim=False, pilasters=False, kind="closet")
    space("exit_alcove", (-768, 2736, -128, -640, 2880, 64), "gate", trim=False, pilasters=False)


# ---------------------------------------------------------------------------
# Shell generation

def build_shell(cutaway: bool, detail_stage: bool) -> list[Brush]:
    airs = [s.box for s in spaces.values()]
    emitted: list[Box] = []
    out: list[Brush] = []
    for s in spaces.values():
        x1, y1, z1, x2, y2, z2 = s.box
        th = s.theme
        slabs = [((x1 - T, y1 - T, z1 - T, x2 + T, y2 + T, z1), "floor"),
                 ((x1 - T, y1 - T, z2, x2 + T, y2 + T, z2 + T), "ceil"),
                 ((x1 - T, y1 - T, z1, x1, y2 + T, z2), "wall"),
                 ((x2, y1 - T, z1, x2 + T, y2 + T, z2), "wall"),
                 ((x1, y1 - T, z1, x2, y1, z2), "wall"),
                 ((x1, y2, z1, x2, y2 + T, z2), "wall")]
        for slab, role in slabs:
            pieces = [slab]
            for c in airs + emitted:
                if not overlap(slab, c):
                    continue
                pieces = [q for p in pieces for q in subtract(p, c)]
            emitted.extend(pieces)
            if cutaway and role == "ceil":
                continue
            for p in pieces:
                if role != "wall":
                    out.append(make_box(p, th[role]))
                    continue
                bands = [(p[2], p[5], th["wall"])]
                if detail_stage and s.trim and s.kind == "room" and z2 - z1 >= 96:
                    cuts = [z1 + 16, z2 - 16]
                    bands, lo = [], p[2]
                    for cz in cuts + [p[5]]:
                        hi = min(cz, p[5])
                        if hi > lo:
                            tex = th["base"] if hi <= z1 + 16 else th["top"] if lo >= z2 - 16 else th["wall"]
                            bands.append((lo, hi, tex))
                            lo = hi
                for lo, hi, tex in bands:
                    out.append(make_box((p[0], p[1], lo, p[3], p[4], hi), tex))
    return out


# ---------------------------------------------------------------------------
# Structure: stairs, pits, liquids, catwalks, movers (stage 2)

def structure():
    global stage
    stage = STRUCTURE

    # Arrival: worldspawn start and the inactive slipgate dais behind the player.
    solid((-128, 0, 0, 128, 64, 8), {"up": "+0slipbot", "default": "slipbotsd"})

    # Doors from the arrival chamber and into the hall.
    door((-64, 448, 0, 64, 464, 128), angle=-1, sounds=2, speed=200)
    door((768, 896, 0, 896, 912, 128), angle=-1, sounds=2, speed=200)
    for i, x in enumerate((64, 288, 512)):
        solid((x, 896, 40, x + 96, 912, 104), "clip")

    # Hall entry stair down from the gallery landing (0 -> -128).
    stairs("y+", 912, 24, 7, (768, 896), 0, -16, -128)
    # Slime canal, escape steps along the south lip.
    solid((-128, 1232, -320, 896, 1488, -224), "*slime0")
    stairs("x-", 896, 32, 5, (1232, 1264), -320, 32, -320, {"up": "tech01_6", "default": "tech01_6"})
    # Retracted bridge: START_OPEN door that slides out of the south lip when the pump room button fires.
    ent("func_door", brushes=[make_box((320, 1232, -148, 448, 1488, -132), PLAT)],
        spawnflags=1, angle=270, targetname="bridge", wait=-1, speed=80, sounds=1, lip=8)
    # Upper catwalks (z 128) along north and east walls.
    walk = {"up": "sfloor4_7", "down": "tech04_6", "default": "tech04_1"}
    solid((-128, 1584, 112, 896, 1680, 128), walk)
    solid((800, 1232, 112, 896, 1584, 128), walk)
    # Lift down to the underworks.
    ent("func_plat", brushes=[make_box((352, 1536, -160, 480, 1664, -128), PLAT)], height=312, sounds=1)
    # Hall -> pump room door.
    door((-144, 1120, -128, -128, 1216, -16), angle=-1, sounds=2, speed=200)

    # Pump room control platform and its steps.
    solid((-576, 1104, -128, -448, 1360, -64), {"up": "sfloor4_6", "default": "tech04_2"})
    stairs("x+", -448, 16, 3, (1184, 1280), -64, -16, -128)
    ent("func_button", brushes=[make_box((-576, 1216, -40, -568, 1248, -8), {"east": "+0basebtn", "default": METAL})],
        angle=180, target="bridge", wait=-1, sounds=1)
    # S3: shootable panel in the pump room south wall.
    ent("func_door_secret", brushes=[make_box((-416, 1024, -128, -320, 1040, -32), "twall2_1")], angle=270, sounds=2)

    # S1: shootable panel in the arrival chamber's east wall.
    ent("func_door_secret", brushes=[make_box((256, 176, 0, 272, 288, 112), "tech08_2")], angle=0, sounds=1)

    # Underworks.
    door((560, 1504, -448, 576, 1552, -336), angle=-1, sounds=2, speed=200)
    solid((576, 1552, -576, 1472, 1632, -456), "*water0")
    solid((960, 1424, -576, 1056, 1552, -480), "*water0")
    solid((928, 1296, -576, 1088, 1424, -512), "*water0")
    solid((928, 1296, -576, 1088, 1360, -504), {"up": "sfloor4_2", "default": "tech01_7"})  # drowned cache ledge
    grate = {"up": "sfloor4_4", "down": "sfloor4_4", "default": "tech04_2"}
    solid((768, 1552, -464, 832, 1632, -448), grate)
    solid((1216, 1552, -464, 1280, 1632, -448), grate)
    stairs("y+", 1696, 24, 12, (1344, 1472), -448, 16, -448)

    # Yard: terrace with stair, pool with stepping stones and key plinth, crate climb.
    terrace = {"up": "ground1_6", "default": "twall2_5"}
    solid((1216, 2176, -256, 1472, 2880, -64), terrace)
    stairs("y+", 1984, 16, 12, (1216, 1344), -256, 16, -256)
    solid((1664, 2368, -384, 1920, 2688, -272), "*water0")
    stone = {"up": "ground1_5", "default": "twall2_5"}
    solid((1768, 2400, -384, 1816, 2448, -256), stone)
    solid((1760, 2496, -384, 1824, 2560, -240), {"up": "key01_3", "default": "twall2_5"})
    solid((1768, 2600, -384, 1816, 2648, -256), stone)
    for k in range(6):
        x = 1728 + 64 * k
        solid((x, 2816, -256, x + 64, 2880, -224 + 32 * k), CRATE)
    door((2112, 2320, -256, 2128, 2416, -144), angle=-1, sounds=2, speed=200)
    door((1536, 2880, -256, 1664, 2896, -144), {"default": "door02_1", "south": "edoor01_1", "north": "edoor01_1"},
         angle=-1, targetname="key_ambush", wait=-1, sounds=2, speed=150)

    # Tower: mezzanine, stair along north wall, west walkway.
    mezz = {"up": "sfloor4_1", "down": "tech04_6", "default": "tech04_1"}
    solid((640, 1984, 112, 1088, 2176, 128), mezz)
    stairs("x-", 1024, 32, 12, (2272, 2368), -64, 16, -64)
    solid((640, 2176, -64, 704, 2272, 128), {"up": "sfloor4_1", "default": "tech06_2"})
    door((1088, 2192, -64, 1104, 2272, 48), angle=-1, sounds=2, speed=200)
    door((640, 1680, 128, 768, 1696, 256), angle=-1, sounds=2, speed=200)
    for y in (2000, 2224):
        solid((1088, y, 144, 1216, y + 80, 192), "clip")

    # Exit wing: silver door, S6 panel.
    door((-144, 1584, 128, -128, 1680, 240), {"default": "door02_1", "east": "key01_2", "west": "key01_2"},
         angle=-1, spawnflags=16, sounds=2, speed=150, wait=-1)
    ent("func_door_secret", brushes=[make_box((-592, 1792, 128, -576, 1888, 224), "tech06_2")], angle=180, sounds=1)

    # Relay arena: entry balcony, descending stair, slime moat, relay island, bridge, exit gate.
    solid((-640, 1968, -128, -384, 2096, 128), {"up": "sfloor4_8", "default": "tech14_2"})
    stairs("x-", -640, 16, 15, (1968, 2064), 128, -16, -128)
    solid((-1088, 2256, -320, -320, 2608, -224), "*slime0")
    stairs("x+", -1088, 32, 5, (2256, 2288), -320, 32, -320, {"up": "tech01_6", "default": "tech01_6"})
    solid((-768, 2368, -320, -640, 2496, -64), {"up": "sfloor4_5", "default": "tech09_4"})
    solid((-736, 2400, -64, -672, 2464, 96), {"up": "tlight10", "down": "tlight10", "default": "tech09_3"})
    ent("func_button", brushes=[make_box((-720, 2392, 16, -688, 2400, 48), {"south": "+0planet", "default": METAL})],
        angle=90, health=1, target="relay", wait=-1, sounds=3)
    ent("func_door", brushes=[make_box((-512, 2256, -148, -416, 2608, -132), PLAT)],
        spawnflags=1, angle=270, targetname="relay", wait=-1, speed=80, sounds=1, lip=8)
    door((-768, 2736, -128, -640, 2752, 64), {"default": "door02_1", "south": "edoor01_1", "north": "edoor01_1"},
         angle=-1, targetname="relay", wait=-1, sounds=3, speed=100)
    door((-320, 2112, -128, -304, 2240, 0), angle=-1, targetname="arena_ambush", wait=-1, sounds=2, speed=200)
    door((-1104, 2112, -128, -1088, 2240, 0), angle=-1, targetname="arena_ambush", wait=-1, sounds=2, speed=200)

    # Exit slipgate.
    solid((-768, 2864, -128, -640, 2880, 64), "tech08_1")
    solid((-752, 2856, -128, -656, 2864, 32), "*teleport")
    ent("trigger_changelevel", brushes=[make_box((-752, 2816, -128, -656, 2856, 0), "trigger")], map="start")


# ---------------------------------------------------------------------------
# Detail: trims, pilasters, machines, fixtures (stage 3)

def blocked(p: Box, own: Box, air_margin=24, detail_margin=8) -> bool:
    """True if `p` would intrude on an opening into another space or on placed detail."""
    grown = (p[0] - air_margin, p[1] - air_margin, p[2], p[3] + air_margin, p[4] + air_margin, p[5])
    if any(s.box is not own and overlap(grown, s.box) for s in spaces.values()):
        return True
    grown = (p[0] - detail_margin, p[1] - detail_margin, p[2], p[3] + detail_margin, p[4] + detail_margin, p[5])
    return any(overlap(grown, b.aabb) for b in detail + [b for e in entities for b in e["brushes"]])


def pilasters():
    for s in spaces.values():
        if not (s.pilasters and s.kind == "room"):
            continue
        x1, y1, z1, x2, y2, z2 = s.box
        spec = s.pil
        spacing, width, depth = spec["spacing"], spec["width"], spec["depth"]
        top = z2 if spec["height"] is None else z1 + spec["height"]
        tex = {"default": s.theme["pil"]}
        candidates = []
        for wall, lo, hi in (("s", x1, x2), ("n", x1, x2), ("w", y1, y2), ("e", y1, y2)):
            n = max(1, int((hi - lo) // spacing))
            step = (hi - lo) / (n + 1)
            for i in range(1, n + 1):
                c = round((lo + step * i) / 16) * 16
                if wall == "s":
                    candidates.append((c - width / 2, y1, z1, c + width / 2, y1 + depth, top))
                elif wall == "n":
                    candidates.append((c - width / 2, y2 - depth, z1, c + width / 2, y2, top))
                elif wall == "w":
                    candidates.append((x1, c - width / 2, z1, x1 + depth, c + width / 2, top))
                else:
                    candidates.append((x2 - depth, c - width / 2, z1, x2, c + width / 2, top))
        for p in candidates:
            if blocked(p, s.box):
                continue
            solid(p, tex)
            if spec["cap"]:
                cap = 8
                solid((p[0] - cap if p[0] > x1 else p[0], p[1] - cap if p[1] > y1 else p[1], top,
                       p[3] + cap if p[3] < x2 else p[3], p[4] + cap if p[4] < y2 else p[4], top + 16), spec["cap"])


def door_frames():
    """Jambs and a lintel on both faces of every door/window cut-through."""
    rooms = [s.box for s in spaces.values() if s.kind in ("room", "pit")]
    for s in list(spaces.values()):
        if s.kind not in ("conn", "window"):
            continue
        x1, y1, z1, x2, y2, z2 = s.box
        ax = 0 if x2 - x1 <= y2 - y1 else 1
        w = 8 if s.kind == "window" else 16
        pieces = []
        for side in (0, 1):
            if ax == 0:
                fx = (x1 - 8, x1) if side == 0 else (x2, x2 + 8)
                pieces += [(fx[0], y1 - w, z1, fx[1], y1, z2 + w), (fx[0], y2, z1, fx[1], y2 + w, z2 + w),
                           (fx[0], y1 - w, z2, fx[1], y2 + w, z2 + w)]
                if s.kind == "window":
                    pieces.append((fx[0], y1 - w, z1 - w, fx[1], y2 + w, z1))
            else:
                fy = (y1 - 8, y1) if side == 0 else (y2, y2 + 8)
                pieces += [(x1 - w, fy[0], z1, x1, fy[1], z2 + w), (x2, fy[0], z1, x2 + w, fy[1], z2 + w),
                           (x1 - w, fy[0], z2, x2 + w, fy[1], z2 + w)]
                if s.kind == "window":
                    pieces.append((x1 - w, fy[0], z1 - w, x2 + w, fy[1], z1))
        for p in pieces:
            c = [(p[i] + p[i + 3]) / 2 for i in range(3)]
            if not any(contains_point(r, c) for r in rooms):
                continue
            if any(o.box is not s.box and o.kind in ("conn", "window", "secret", "closet") and overlap(p, o.box)
                   for o in spaces.values()):
                continue
            solid(p, {"default": "tech04_3", "up": "tech04_2", "down": "tech04_2"})


def ribs(name, spacing, tex="tech04_3", beam="tech04_6", width=16, depth=8):
    """Structural frames across a corridor: two wall jambs and a ceiling beam."""
    s = spaces[name]
    x1, y1, z1, x2, y2, z2 = s.box
    along_x = (x2 - x1) > (y2 - y1)
    lo, hi = (x1, x2) if along_x else (y1, y2)
    n = int((hi - lo) // spacing)
    for i in range(1, n + 1):
        c = lo + (hi - lo) * i / (n + 1)
        c = round(c / 16) * 16
        if along_x:
            parts = [(c - width / 2, y1, z1, c + width / 2, y1 + depth, z2),
                     (c - width / 2, y2 - depth, z1, c + width / 2, y2, z2),
                     (c - width / 2, y1, z2 - 16, c + width / 2, y2, z2)]
        else:
            parts = [(x1, c - width / 2, z1, x1 + depth, c + width / 2, z2),
                     (x2 - depth, c - width / 2, z1, x2, c + width / 2, z2),
                     (x1, c - width / 2, z2 - 16, x2, c + width / 2, z2)]
        if any(blocked(p, s.box, air_margin=16, detail_margin=0) for p in parts):
            continue
        solid(parts[0], tex)
        solid(parts[1], tex)
        solid(parts[2], {"down": beam, "default": "tech04_2"})


def ceiling_fixtures(s: Space, spacing, z=None, size=32, tex="tlight01", margin=96, lit=None):
    x1, y1, z1, x2, y2, z2 = s.box
    top = z2 if z is None else z
    nx = max(1, round((x2 - x1 - 2 * margin) / spacing) + 1)
    ny = max(1, round((y2 - y1 - 2 * margin) / spacing) + 1)
    pts = []
    for i in range(nx):
        for j in range(ny):
            cx = x1 + margin + (x2 - x1 - 2 * margin) * (i / (nx - 1) if nx > 1 else 0.5)
            cy = y1 + margin + (y2 - y1 - 2 * margin) * (j / (ny - 1) if ny > 1 else 0.5)
            cx, cy = round(cx / 16) * 16, round(cy / 16) * 16
            pts.append((cx, cy))
            global stage
            prev = stage
            stage = DETAIL
            fixture = (cx - size / 2, cy - size / 2, top - 8, cx + size / 2, cy + size / 2, top)
            if not any(overlap(fixture, b.aabb) for b in detail):
                solid(fixture, {"down": tex, "default": "tech04_2"})
            stage = POPULATE
            if lit is not None:
                light(cx, cy, top - 24, lit)
            stage = prev
    return pts


def details():
    global stage
    stage = DETAIL

    # Arrival slipgate frame (the gate you came through) and consoles.
    solid((-96, 0, 8, -64, 32, 176), "slipside")
    solid((64, 0, 8, 96, 32, 176), "slipside")
    solid((-96, 0, 176, 96, 32, 200), {"down": "+0sliptop", "default": "sliptopsd"})
    solid((-64, 0, 8, 64, 8, 176), "slip2")
    console_w = {"east": "comp1_3", "up": "comp1_8", "default": "tech01_1"}
    console_e = {"west": "comp1_4", "up": "comp1_8", "default": "tech01_1"}
    solid((-256, 320, 0, -208, 416, 48), console_w)
    solid((208, 320, 0, 256, 416, 48), console_e)
    solid((-256, 64, 0, -224, 128, 112), {"east": "comp1_6", "default": "tech01_1"})
    solid((224, 64, 0, 256, 128, 112), {"west": "comp1_6", "default": "tech01_1"})

    # Gallery: glowing strip under each window.
    for x in (64, 288, 512):
        solid((x, 888, 24, x + 96, 896, 32), "tlight11")

    # Hall: ceiling beams, wall pipes, catwalk rails and supports, bridge lip lights.
    beam = {"down": "tech04_6", "default": "tech04_5"}
    for y in (1104, 1456):
        solid((-128, y, 352, 896, y + 32, 384), beam)
    for x in (224, 448):
        prism(x, 936, 24, -128, 384, "tech09_3", "tech01_1")
    rail = {"default": "tech04_1", "up": "tech04_2"}
    solid((-128, 1576, 128, 352, 1584, 160), rail)
    solid((480, 1576, 128, 800, 1584, 160), rail)
    solid((792, 1232, 128, 800, 1576, 160), rail)
    for x in (0, 224, 576, 736):
        solid((x, 1616, -128, x + 32, 1648, 112), {"default": "tech04_3"})
    solid((832, 1520, -128, 864, 1552, 112), {"default": "tech04_3"})
    solid((304, 1216, -128, 320, 1232, -96), {"up": "tlight01", "default": METAL})
    solid((448, 1216, -128, 464, 1232, -96), {"up": "tlight01", "default": METAL})
    # Lift shaft collar.
    solid((336, 1536, -128, 352, 1664, -120), METAL)
    solid((480, 1536, -128, 496, 1664, -120), METAL)
    # Machinery along the hall's west wall.
    solid((-128, 960, -128, -80, 1088, -48), {"east": "comp1_1", "up": "comp1_8", "default": "tech01_1"})
    solid((-128, 1520, -128, -64, 1600, -32), {"east": "comp1_2", "up": "comp1_8", "default": "tech01_1"})

    # Pump room: two pumps, consoles, crates, overhead manifold.
    for cy in (1136, 1328):
        solid((-352, cy - 64, -128, -224, cy + 64, -112), "tech01_1")
        prism(-288, cy, 48, -112, 160, "tech09_4", "tech01_1")
        prism(-288, cy, 56, 16, 40, "tech04_2", "tech04_2", joint=True)
    solid((-304, 1184, 96, -272, 1280, 128), "tech10_1")
    solid((-416, 1392, -128, -208, 1424, -80), {"south": "comp1_2", "up": "comp1_8", "default": "tech01_1"})
    solid((-224, 1232, -128, -160, 1296, -64), CRATE)
    solid((-208, 1312, -128, -160, 1360, -80), CRATE)
    solid((-576, 1056, -128, -512, 1104, -64), CRATE)

    # Underworks: crates and a cable run.
    solid((272, 1456, -448, 336, 1520, -384), CRATE)
    solid((496, 1680, -448, 560, 1744, -384), CRATE)
    solid((272, 1680, -448, 320, 1728, -400), CRATE)
    solid((576, 1648, -352, 1472, 1680, -336), "tech10_1")
    for x in (704, 1024, 1344):
        solid((x, 1504, -448, x + 32, 1520, -320), "tech04_3")

    # Yard: relay mast landmark, lamp posts, terrace rail segments.
    solid((1952, 2176, -256, 2080, 2304, -128), {"up": "tech01_1", "default": "tech03_1"})
    solid((1984, 2208, -128, 2048, 2272, 80), "tech09_3")
    solid((1976, 2200, 80, 2056, 2280, 96), "tech04_6")
    solid((2000, 2224, 96, 2032, 2256, 168), {"up": "tlight10", "default": "tech10_3"})
    for y in (2240, 2496, 2752):
        solid((1472, y, -64, 1488, y + 96, -32), "twall2_5")
    for x, y in ((1600, 2112), (1600, 2752), (1984, 2496)):
        solid((x, y, -256, x + 16, y + 16, -64), "tech04_3")
        solid((x - 8, y - 8, -64, x + 24, y + 24, -56), {"down": "tlight01", "default": "tech04_2"})
    solid((1648, 2352, -264, 1664, 2704, -256), "tech04_1")  # pool lip

    # Tower: support pillars, mezzanine rail, consoles.
    for x in (768, 960):
        solid((x, 2144, -64, x + 32, 2176, 112), "tech04_3")
    solid((704, 2176, 128, 1088, 2184, 160), rail)
    solid((800, 1984, 128, 1056, 2016, 176), {"north": "comp1_3", "up": "comp1_8", "default": "tech01_1"})
    solid((1024, 2048, -64, 1088, 2112, 0), CRATE)
    solid((640, 1984, -64, 704, 2048, -16), CRATE)

    # Arena: balcony rails, relay cap, slipgate frame.
    solid((-640, 2096, 128, -384, 2104, 160), rail)
    solid((-392, 1968, 128, -384, 2096, 160), rail)
    solid((-744, 2392, 96, -664, 2472, 112), "tech01_1")
    solid((-768, 2832, -128, -752, 2864, 48), "slipside")
    solid((-656, 2832, -128, -640, 2864, 48), "slipside")
    solid((-768, 2832, 32, -640, 2864, 64), {"down": "+0sliptop", "default": "sliptopsd"}, joint=True)
    prism(-512, 2704, 24, -128, 384, "tech09_3", "tech01_1")
    solid((-736, 2736, 80, -672, 2744, 112), "z_exit")
    solid((-744, 2736, 72, -664, 2744, 80), "tech04_2")
    # Arena: columns flanking the moat and a high gantry over the relay.
    for x in (-1024, -416):
        for y in (2176, 2672):
            solid((x - 24, y - 24, -128, x + 24, y + 24, 384), {"default": "tech06_2", "up": "tech04_2"})
            solid((x - 32, y - 32, -128, x + 32, y + 32, -96), "tech04_5", joint=True)
    solid((-1088, 2416, 288, -320, 2448, 320), {"down": "tech04_6", "default": "tech04_5"})
    solid((-720, 2416, 112, -688, 2448, 288), "tech10_3")   # cable from gantry to relay

    # Hall: sluice gates over each end of the canal, outflow spouts, gate gear housings.
    for x1, x2, face in ((-128, -104, "east"), (872, 896, "west")):
        solid((x1, 1280, -224, x2, 1440, 64), {face: "tech02_7", "default": "tech04_2"})
        solid((x1, 1264, 64, x2, 1456, 96), "tech04_6", joint=True)
        solid((x1, 1264, -224, x2, 1280, 64), "tech04_3")
        solid((x1, 1440, -224, x2, 1456, 64), "tech04_3")
    for y in (1312, 1376):
        solid((-104, y, -192, -72, y + 32, -160), "tech10_1")
    solid((-128, 1312, 128, -96, 1408, 192), {"east": "comp1_6", "default": "tech01_1"})

    # Curbs along the canal and moat edges, leaving the bridges and escape steps open.
    curb = {"up": "tech04_2", "default": "tech04_1"}
    for xa, xb in ((-128, 320), (448, 736)):
        solid((xa, 1216, -128, xb, 1232, -120), curb)
    for xa, xb in ((-128, 320), (448, 896)):
        solid((xa, 1488, -128, xb, 1504, -120), curb)
    for xa, xb in ((-928, -512), (-416, -320)):
        solid((xa, 2240, -128, xb, 2256, -120), curb)
    for xa, xb in ((-1088, -512), (-416, -320)):
        solid((xa, 2608, -128, xb, 2624, -120), curb)

    # Yard: loading-dock canopy along the south wall.
    solid((1488, 1984, -48, 1936, 2112, -32), {"up": "ecop1_4", "down": "tech04_6", "default": "tech04_5"})
    for x in (1504, 1712, 1904):
        solid((x, 2080, -256, x + 16, 2096, -48), "tech04_3")
    solid((1488, 1984, -64, 1936, 2000, -48), "tech04_2")

    # Tower: ceiling beams; pump room: outflow pipes to the west wall; landing: riser pipes.
    for y in (2048, 2208):
        solid((640, y, 288, 1088, y + 32, 320), {"down": "tech04_6", "default": "tech04_5"})
    for cy in (1136, 1328):
        solid((-576, cy - 16, 64, -336, cy + 16, 96), {"default": "tech10_1", "up": "tech04_2"})
        solid((-560, cy - 24, 56, -544, cy + 24, 104), "tech04_2", joint=True)
    for x, y in ((272, 1632), (528, 1472), (544, 1600)):
        prism(x, y, 16, -448, -288, "tech10_3", "tech04_2")

    # Wall lamps: a recessed strip light with a housing, on walls that read flat.
    def wall_lamp(x, y, z, facing, tex="tlight09"):
        if facing in ("north", "south"):
            sgn = 1 if facing == "north" else -1
            y0, y1 = (y, y + 6) if sgn > 0 else (y - 6, y)
            solid((x - 16, y0, z, x + 16, y1, z + 64), {facing: tex, "default": "tech04_2"})
            solid((x - 24, y0, z - 8, x + 24, y1 + sgn * 2 if sgn > 0 else y1, z), "tech04_2", joint=True)
        else:
            sgn = 1 if facing == "east" else -1
            x0, x1 = (x, x + 6) if sgn > 0 else (x - 6, x)
            solid((x0, y - 16, z, x1, y + 16, z + 64), {facing: tex, "default": "tech04_2"})
            solid((x0, y - 24, z - 8, x1, y + 24, z), "tech04_2", joint=True)
        global stage
        prev, stage = stage, POPULATE
        off = {"north": (0, 24), "south": (0, -24), "east": (24, 0), "west": (-24, 0)}[facing]
        light(x + off[0], y + off[1], z + 32, 160)
        stage = prev

    for y in (192, 304):
        wall_lamp(-256, y, 64, "east")
    wall_lamp(-128, 448, 80, "south")
    wall_lamp(128, 448, 80, "south")
    for x in (224, 448, 672):
        wall_lamp(x, 768, 32, "north", "tlight07")
    for y in (544, 672):
        wall_lamp(-64, y, 32, "east", "tlight07")
    for x in (-480, -256):
        wall_lamp(x, 1584, 160, "north", "tlight07")
    for y in (1760, 1856):
        wall_lamp(-448, y, 160, "west", "tlight07")
    for y in (1760, 1904):
        wall_lamp(768, y, 160, "west", "tlight07")
    wall_lamp(1408, 2880, -32, "south")
    for y in (2112, 2624):
        wall_lamp(-1088, y, 64, "east")
    wall_lamp(-320, 2400, 64, "west")

    # Arrival: wall consoles flanking the inactive gate.
    for x in (-192, 128):
        solid((x, 0, 0, x + 64, 24, 64), {"north": "comp1_5", "up": "comp1_8", "default": "tech01_1"})
        solid((x + 8, 0, 64, x + 56, 8, 128), {"north": "comp1_7", "default": "tech01_1"})

    # Guard post: lockers along the east wall and a desk.
    for y in (2256, 2320, 2384):
        solid((2336, y, -256, 2368, y + 56, -144), {"west": "comp1_4", "default": "tech01_1"})
    solid((2192, 2448, -256, 2304, 2496, -224), {"up": "comp1_8", "default": "tech04_1"})
    solid((2128, 2256, -256, 2192, 2304, -192), CRATE)

    # Yard: railing posts around the pool (gaps left for the stepping stones).
    for x in (1664, 1728, 1856, 1904):
        for y in (2352, 2688):
            solid((x, y, -256, x + 16, y + 16, -216), "tech04_3")
    for y in (2416, 2480, 2544, 2608):
        for x in (1648, 1920):
            solid((x, y, -256, x + 16, y + 16, -216), "tech04_3")

    # Arena: sniper ledge above the north shore (enforcer perch, reached from nowhere but seen from the balcony).
    solid((-1088, 2704, 96, -832, 2736, 112), {"up": "sfloor4_8", "default": "tech04_5"})
    solid((-1088, 2696, 96, -832, 2704, 136), "tech04_1")
    for x in (-1056, -864):
        solid((x, 2704, -128, x + 16, 2720, 96), "tech04_3")

    # Corridor frames and door frames.
    door_frames()
    for name, spacing in (("intake", 128), ("gallery", 160), ("high_corr", 128), ("exit_corr", 128),
                          ("exit_corr2", 128), ("channel", 192), ("yard_stair", 128)):
        ribs(name, spacing)

    pilasters()


# ---------------------------------------------------------------------------
# Population: lights, monsters, items, triggers (stage 4)

def populate():
    global stage
    sp = spaces

    # Fixtures + lights per room.
    for name, spacing, lit in (("arrival", 192, 250), ("intake", 160, 200), ("gallery", 224, 200),
                               ("pump", 192, 250), ("landing", 160, 200), ("channel", 224, 180),
                               ("yard_stair", 160, 200), ("guard", 160, 200), ("tower", 224, 280),
                               ("high_corr", 160, 200), ("exit_corr", 192, 200), ("exit_corr2", 160, 200)):
        ceiling_fixtures(sp[name], spacing, lit=lit, margin=48 if sp[name].box[4] - sp[name].box[1] <= 160 else 96)
    ceiling_fixtures(sp["hall"], 256, lit=300)
    ceiling_fixtures(sp["arena"], 256, lit=300)

    stage = POPULATE
    # Low fill for the tall rooms and pits.
    for x in (64, 384, 704):
        light(x, 1040, 0, 220)
        light(x, 1520, 0, 220)
        light(x, 1632, 192, 180)
    for x in (0, 448, 832):
        light(x, 1360, -200, 160)
    light(416, 1600, -320, 200)   # shaft
    for x in (-960, -704, -448):
        light(x, 2160, 0, 220)
        light(x, 2672, 0, 220)
        light(x, 2432, -200, 160)
    light(-704, 2432, 160, 250)   # relay core
    light(-704, 2800, 0, 250)     # exit gate
    light(-704, 2848, -64, 200)
    for x, y in ((1608, 2120), (1608, 2760), (1992, 2504)):
        light(x, y, -80, 300)
    light(2096, 2240, 120, 300)
    light(1344, 2432, 64, 250)
    light(1792, 2528, -160, 250)
    light(1792, 2208, 0, 200)
    light(1344, 1840, -192, 180)
    for x in (704, 1024, 1344):
        light(x + 16, 1536, -360, 150)
    light(1008, 1360, -448, 120)
    light(328, 232, 64, 150)
    light(-368, 984, -72, 150)
    light(952, 1472, 224, 150)
    light(-640, 1840, 192, 150)
    light(2176, 2840, 0, 120)
    light(1600, 2936, -176, 120)
    for x in (-224, 224):
        light(x, 1264, -64, 160)   # pump room floor fill
    light(-288, 1232, 120, 200)
    light(-512, 1232, 0, 200)

    # Worldspawn-independent markers.
    ent("info_player_start", (0, 128, 24), angle=90)
    ent("info_player_coop", (-64, 160, 24), angle=90)
    ent("info_player_coop", (64, 160, 24), angle=90)
    ent("info_player_deathmatch", (0, 640, 24), angle=90)
    ent("info_player_deathmatch", (384, 1080, -104), angle=0)
    ent("info_player_deathmatch", (1600, 2432, -232), angle=180)
    ent("info_player_deathmatch", (864, 2080, 152), angle=180)
    ent("info_intermission", (704, 1000, 300), mangle="25 120 0")

    # --- Arrival / gallery
    item("item_shells", -160, 200, 0)
    item("item_health", 160, 200, 0, flags=1)
    monster("monster_army", 384, 832, 0, 180)
    monster("monster_army", 832, 832, 0, 180, flags=NOT_EASY | AMBUSH)

    # --- Sluice Hall (south side before the bridge)
    monster("monster_army", 64, 1040, -128, 0)
    monster("monster_army", 576, 1168, -128, 270, target="hall_p1")
    ent("path_corner", (576, 1168, -104), targetname="hall_p1", target="hall_p2")
    ent("path_corner", (64, 1168, -104), targetname="hall_p2", target="hall_p1")
    monster("monster_army", 320, 992, -128, 90, flags=NOT_EASY)
    monster("monster_dog", 704, 1168, -128, 180)
    monster("monster_dog", 128, 1168, -128, 0, flags=NOT_EASY)
    # Catwalk sentries that shoot down early.
    monster("monster_army", 160, 1632, 128, 270)
    monster("monster_army", 848, 1408, 128, 180, flags=NOT_EASY | NOT_MEDIUM)
    # North side, past the bridge.
    monster("monster_army", 640, 1568, -128, 270, flags=AMBUSH)
    monster("monster_army", 96, 1552, -128, 270, flags=NOT_EASY | NOT_MEDIUM)
    item("item_health", 640, 1056, -128)
    item("item_spikes", 512, 1600, -128)

    # --- Pump room
    monster("monster_army", -320, 1232, -128, 0, flags=AMBUSH)
    monster("monster_army", -512, 1312, -64, 0)
    monster("monster_army", -480, 1392, -128, 270, flags=NOT_EASY)
    monster("monster_army", -192, 1392, -128, 270, flags=NOT_EASY | NOT_MEDIUM)
    monster("monster_dog", -400, 1104, -128, 0)
    item("item_armor1", -512, 1184, -64)
    item("item_shells", -192, 1104, -128)

    # --- Underworks
    monster("monster_dog", 400, 1488, -448, 90)
    monster("monster_dog", 432, 1696, -448, 270, flags=NOT_EASY | NOT_MEDIUM)
    monster("monster_army", 896, 1528, -448, 180)
    monster("monster_army", 1152, 1656, -448, 180, flags=AMBUSH)
    monster("monster_army", 1408, 1528, -448, 180, flags=NOT_EASY | NOT_MEDIUM)
    monster("monster_army", 1408, 2064, -256, 270)
    item("item_health", 320, 1600, -448)
    item("item_shells", 1088, 1528, -448)
    item("item_health", 976, 1328, -504, flags=2)          # S2 megahealth
    item("item_shells", 1040, 1328, -504)

    # --- Yard
    monster("monster_army", 1280, 2560, -64, 0)
    monster("monster_army", 1344, 2816, -64, 270, flags=NOT_EASY)
    monster("monster_army", 1856, 2080, -256, 180)
    monster("monster_army", 2048, 2624, -256, 180, flags=NOT_EASY | NOT_MEDIUM)
    monster("monster_dog", 1600, 2304, -256, 270)
    monster("monster_dog", 1920, 2784, -256, 180)
    monster("monster_dog", 1568, 2624, -256, 270, flags=NOT_EASY | NOT_MEDIUM)
    monster("monster_enforcer", 1984, 2400, -256, 180, flags=NOT_EASY)
    item("item_key1", 1792, 2528, -240, target="key_ambush")
    monster("monster_army", 1568, 2944, -256, 270, flags=AMBUSH)
    monster("monster_army", 1632, 2928, -256, 270, flags=AMBUSH | NOT_EASY | NOT_MEDIUM)
    item("item_health", 1408, 2112, -256)
    item("item_spikes", 1376, 2640, -64)
    # Guard post.
    monster("monster_army", 2272, 2304, -256, 180)
    monster("monster_enforcer", 2304, 2432, -256, 180, flags=NOT_EASY | AMBUSH)
    ent("weapon_supershotgun", (2240, 2368, -248))
    item("item_shells", 2320, 2280, -256)
    # S5 crate climb.
    ent("weapon_grenadelauncher", (2176, 2840, -56))
    item("item_rockets", 2144, 2816, -64)

    # --- Tower
    monster("monster_army", 864, 2240, -64, 180)
    monster("monster_army", 704, 2112, -64, 0, flags=NOT_EASY | NOT_MEDIUM)
    monster("monster_army", 960, 2080, 128, 270)
    monster("monster_enforcer", 704, 2048, 128, 0, flags=NOT_EASY)
    ent("weapon_nailgun", (896, 2080, 136))
    item("item_spikes", 1024, 2096, 128)
    item("item_health", 896, 2224, -64)
    monster("monster_dog", 704, 1824, 128, 270)

    # Hall catwalk rewards and S4 quad.
    item("item_shells", 848, 1296, 128)
    ent("item_artifact_super_damage", (952, 1472, 168))

    # --- Exit wing
    monster("monster_army", -448, 1632, 128, 0)
    monster("monster_army", -512, 1904, 128, 270, flags=NOT_EASY | NOT_MEDIUM)
    item("item_health", -544, 1632, 128)
    ent("item_armor2", (-640, 1840, 136))
    item("item_spikes", -672, 1808, 128)
    item("item_shells", -672, 1872, 128)

    # --- Arena
    monster("monster_army", -896, 2672, -128, 270)
    monster("monster_army", -352, 2656, -128, 270)
    monster("monster_army", -576, 2672, -128, 270, flags=NOT_EASY | NOT_MEDIUM)
    monster("monster_enforcer", -832, 2688, -128, 270, flags=NOT_EASY)
    monster("monster_enforcer", -960, 2720, 112, 270, flags=NOT_EASY | NOT_MEDIUM)
    monster("monster_dog", -896, 2192, -128, 0)
    monster("monster_dog", -480, 2192, -128, 180, flags=NOT_EASY | NOT_MEDIUM)
    monster("monster_ogre", -256, 2176, -128, 180, flags=NOT_EASY | AMBUSH)
    monster("monster_enforcer", -1152, 2144, -128, 0, flags=AMBUSH)
    monster("monster_enforcer", -1152, 2208, -128, 0, flags=NOT_EASY | AMBUSH)
    item("item_health", -944, 2176, -128)
    item("item_health", -512, 2160, -128, flags=1)
    item("item_rockets", -544, 2032, 128)

    # Triggers, messages and secrets.
    trig = "trigger"
    ent("trigger_once", brushes=[make_box((-1088, 2112, -128, -320, 2256, -64), trig)], target="arena_ambush")
    ent("trigger_once", brushes=[make_box((-640, 1968, 128, -384, 2096, 192), trig)],
        message="The relay core powers the north gate.")
    ent("trigger_once", brushes=[make_box((-128, 1120, -128, 896, 1232, -64), trig)],
        message="The sluice bridge is retracted.")
    for b in ((272, 192, 0, 384, 272, 64),          # S1 arrival closet
              (944, 1312, -504, 1072, 1344, -440),  # S2 drowned cache
              (-400, 944, -128, -336, 1008, -64),   # S3 pump closet
              (912, 1440, 160, 1000, 1504, 224),    # S4 hall alcove
              (2128, 2816, -64, 2224, 2864, 0),     # S5 crate climb
              (-688, 1808, 128, -592, 1872, 192)):  # S6 exit closet
        ent("trigger_secret", brushes=[make_box(b, trig)])
    item("item_armor1", 336, 232, 0)
    item("item_shells", 288, 256, 0)
    ent("weapon_nailgun", (-368, 976, -120))
    item("item_spikes", -400, 976, -128)

    # Ambient sound.
    ent("ambient_comp_hum", (-232, 368, 64))
    ent("ambient_comp_hum", (-312, 1408, -64))
    ent("ambient_comp_hum", (928, 2000, 176))
    ent("ambient_drone", (384, 1360, -200))
    ent("ambient_drone", (-704, 2432, -100))
    ent("ambient_drip", (1024, 1592, -400))


# ---------------------------------------------------------------------------
# Output and checks

def build(max_stage: int, cutaway: bool):
    layout()
    structure()
    details()
    populate()
    shell = build_shell(cutaway, max_stage >= DETAIL)
    world = shell + [b for b in detail if b.stage <= max_stage]
    ents = [e for e in entities if e["stage"] <= max_stage]
    if max_stage < POPULATE:
        ents.append({"props": {"classname": "info_player_start", "origin": "0 128 24", "angle": "90"}, "brushes": [], "stage": 0})
    return world, ents


def write_map(path: Path, world, ents):
    out = ["// Game: Quake", "// Format: Standard", "// entity 0", "{",
           '"classname" "worldspawn"', '"message" "The Sluice Works"', f'"wad" "{WAD}"',
           '"worldtype" "2"', '"sounds" "4"', '"_sunlight" "180"', '"_sun_mangle" "60 -55 0"']
    for i, b in enumerate(world):
        out.append(f"// brush {i}")
        out.append(b.text())
    out.append("}")
    for i, e in enumerate(ents, start=1):
        out.append(f"// entity {i}")
        out.append("{")
        for k, v in e["props"].items():
            out.append(f'"{k}" "{v}"')
        for j, b in enumerate(e["brushes"]):
            out.append(f"// brush {j}")
            out.append(b.text())
        out.append("}")
    path.write_text("\n".join(out) + "\n", encoding="ascii")


HULLS = {"monster_army": (16, 64), "monster_dog": (32, 64), "monster_enforcer": (16, 64), "monster_ogre": (32, 88),
         "info_player_start": (16, 56), "info_player_coop": (16, 56), "info_player_deathmatch": (16, 56)}


def checks(world, ents) -> dict:
    airs = {n: s.box for n, s in spaces.items()}
    solids = [b.aabb for b in world if b.textures[0] not in ("clip",) and not b.textures[0].startswith("*")]
    movers = [b.aabb for e in ents for b in e["brushes"] if e["props"]["classname"].startswith("func_")]
    problems = []

    def in_air(p):
        return any(contains_point(a, p) for a in airs.values())

    for e in ents:
        p = e["props"]
        cls = p["classname"]
        if "origin" not in p:
            continue
        x, y, z = map(float, p["origin"].split())
        if cls.startswith(("monster_", "info_player")):
            r, h = HULLS[cls]
            bb = (x - r, y - r, z - 24, x + r, y + r, z - 24 + h)
            corners = [(bb[i], bb[j], bb[k]) for i in (0, 3) for j in (1, 4) for k in (2, 5)]
            if not all(in_air(c) for c in corners):
                problems.append(f"{cls} at {p['origin']} not fully inside air")
            if any(overlap(bb, s, 0.5) for s in solids + movers):
                problems.append(f"{cls} at {p['origin']} intersects solid")
            below = (x, y, z - 25)
            if not any(contains_point(s, below) for s in solids + movers) and not any(
                    contains_point(b.aabb, below) and b.textures[0].startswith("*") for b in world):
                problems.append(f"{cls} at {p['origin']} has no floor beneath")
        elif cls.startswith(("item_", "weapon_")):
            bb = (x - 16, y - 16, z, x + 16, y + 16, z + 24)
            if not in_air((x, y, z)) or any(overlap(bb, s, 0.5) for s in solids):
                problems.append(f"{cls} at {p['origin']} blocked or outside air")
        elif cls == "light" and not in_air((x, y, z)):
            problems.append(f"light at {p['origin']} outside air")
    targets = {p for e in ents for k, p in e["props"].items() if k == "target"}
    names = {p for e in ents for k, p in e["props"].items() if k == "targetname"}
    for t in sorted(targets - names):
        problems.append(f"target '{t}' has no targetname")
    for n in sorted(names - targets):
        problems.append(f"targetname '{n}' is never targeted")
    # Detail brushes that interpenetrate (excluding deliberate attachments: caps, frames, liquids).
    det = [b for b in world if b in detail and not b.joint and not b.textures[0].startswith("*") and b.textures[0] != "clip"]
    for i, a1 in enumerate(det):
        for a2 in det[i + 1:]:
            if overlap(a1.aabb, a2.aabb, 0.5):
                vol = 1
                for k in range(3):
                    vol *= min(a1.aabb[k + 3], a2.aabb[k + 3]) - max(a1.aabb[k], a2.aabb[k])
                if vol > 16 * 16 * 16:
                    problems.append(f"detail overlap {a1.aabb} / {a2.aabb}")
    xs = [b.aabb for b in world]
    bounds = [min(b[i] for b in xs) for i in range(3)] + [max(b[i + 3] for b in xs) for i in range(3)]
    cls_counts = Counter(e["props"]["classname"] for e in ents)

    def skill_count(bit):
        return sum(1 for e in ents if e["props"]["classname"].startswith("monster_")
                   and not int(e["props"].get("spawnflags", 0)) & bit)

    return {
        "world_brushes": len(world),
        "entity_brushes": sum(len(e["brushes"]) for e in ents),
        "total_brushes": len(world) + sum(len(e["brushes"]) for e in ents),
        "entities": len(ents) + 1,
        "bounds": bounds,
        "size": [bounds[i + 3] - bounds[i] for i in range(3)],
        "monsters": {"easy": skill_count(NOT_EASY), "normal": skill_count(NOT_MEDIUM), "hard": skill_count(NOT_HARD)},
        "secrets": cls_counts["trigger_secret"],
        "lights": cls_counts["light"],
        "classes": dict(sorted(cls_counts.items())),
        "textures": sorted({t for b in world for t in b.textures}),
        "problems": problems,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", type=int, default=POPULATE)
    ap.add_argument("--cutaway", action="store_true")
    ap.add_argument("--out")
    a = ap.parse_args()
    world, ents = build(a.stage, a.cutaway)
    out = Path(a.out) if a.out else ROOT / "e1m10.map"
    write_map(out, world, ents)
    report = checks(world, ents)
    if a.stage == POPULATE and not a.cutaway and not a.out:
        (ROOT / "qa").mkdir(exist_ok=True)
        (ROOT / "qa" / "source_checks.json").write_text(json.dumps(report, indent=2))
    summary = {k: report[k] for k in ("total_brushes", "entities", "size", "monsters", "secrets", "lights")}
    print(out.name, json.dumps(summary))
    for p in report["problems"]:
        print("  PROBLEM:", p)


if __name__ == "__main__":
    main()
