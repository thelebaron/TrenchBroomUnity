"""Build the Astral Fortress Quake source map and source-level QA artifacts.

This writes Valve-format MAP only. It deliberately does not run a BSP compiler.
"""
from __future__ import annotations

import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent
GFX = Path(r"C:\Users\thele\Downloads\quake_map_source-master\gfx")
WAD_NAMES = ("base.wad", "jr_med.wad", "medieval.wad", "wizard.wad", "metal.wad")
WALL = "city4_2"
FLOOR = "afloor1_4"
CEILING = "wceiling5"
TRIM = "cop1_1"
BRASS = "cop3_4"
METAL = "metal1_6"
DARK = "wbrick1_5"
WOOD = "wood1_1"

world: list[str] = []
world_bounds: list[tuple[tuple[float, float, float], tuple[float, float, float]]] = []
entities: list[tuple[dict[str, str], list[str]]] = []

# Air volumes make the rooms and their connections. Solid cells around their union
# become a sealed, stepped masonry shell. Narrow connectors represent actual doors,
# arches, and stair passages rather than long filler corridors.
AIR = [
    (256, -640, 0, 768, 0, 384),          # south gatehouse
    (0, 0, 0, 1024, 1024, 768),           # the Orrery Court
    (-832, 128, 0, -64, 896, 384),        # west tomb cloister
    (-832, 960, 256, -64, 1376, 640),     # upper choir and silver reliquary
    (-576, 928, 96, -320, 1088, 224),     # visible optional stair-side shelf
    (-736, 736, 0, -512, 1408, 576),      # broad west stair
    (128, 1088, 256, 896, 1792, 960),     # north planetarium
    (1088, 256, 0, 1792, 1024, 640),      # eastern mechanism hall
    (1088, 1088, 256, 1792, 1792, 960),   # east star tower
    (1088, -768, 0, 1920, 192, 768),      # gold-sealed final vault
    (352, 352, -320, 672, 672, 0),        # open well beneath the court
    (208, 560, -256, 352, 656, -96),      # flooded route under the well
    (64, 544, -256, 208, 704, -96),       # dry offering niche
    (-96, 224, 0, 32, 448, 256),          # lower west arch
    (-96, 944, 256, 32, 1040, 480),       # upper return arch
    (384, 800, 0, 640, 1280, 576),        # silver stair and gate
    (992, 640, 256, 1120, 800, 512),      # high return to the east hall
    (992, 768, 0, 1120, 896, 256),        # controlled east entrance
    (992, 64, 0, 1120, 192, 256),         # gold gate to the final vault
    (1216, 832, 0, 1664, 1408, 576),      # stair to the east tower
    (1232, 432, -128, 1648, 816, 0),      # sunken channel in the east hall
    (1280, -384, -192, 1664, 0, 0),       # low heart-pit in the final vault
    (128, 992, 0, 384, 1056, 640),        # silver door's wall-side pocket
    (1008, 240, 0, 1056, 368, 288),       # gold door's wall-side pocket
    (1008, 896, 0, 1056, 1024, 224),      # lower shutter pocket
    (1008, 768, 256, 1056, 896, 448),     # high shutter pocket
    (-1152, 320, 0, -832, 576, 224),      # rune-cache alcove beyond the west wall
    (1984, -704, 224, 2176, -512, 512),   # optional vault cache
    (1856, -672, 224, 2016, -544, 512),   # cache doorway
]


def fmt_point(p: tuple[float, float, float] | list[float]) -> str:
    return "( " + " ".join(f"{v:g}" for v in p) + " )"


def texture_axes(normal: tuple[float, float, float]) -> tuple[str, str]:
    ax, ay, az = (abs(v) for v in normal)
    if az >= ax and az >= ay:
        return "[ 1 0 0 0 ]", "[ 0 1 0 0 ]"
    if ax >= ay:
        return "[ 0 1 0 0 ]", "[ 0 0 1 0 ]"
    return "[ 1 0 0 0 ]", "[ 0 0 1 0 ]"


def poly(
    points: list[tuple[float, float, float]],
    faces: list[tuple[int, int, int]],
    texture: str | list[str],
    dest: list[str] | None = None,
) -> None:
    out = world if dest is None else dest
    center = [sum(p[a] for p in points) / len(points) for a in range(3)]
    lines = []
    for face_index, face in enumerate(faces):
        a, b, c = [points[i] for i in face]
        u = [b[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        normal = (
            u[1] * v[2] - u[2] * v[1],
            u[2] * v[0] - u[0] * v[2],
            u[0] * v[1] - u[1] * v[0],
        )
        # Quake MAP winding points toward the interior of the brush.
        if sum(normal[i] * (center[i] - a[i]) for i in range(3)) < 0:
            b, c = c, b
            normal = tuple(-v for v in normal)
        u_axis, v_axis = texture_axes(normal)
        tex = texture[face_index] if isinstance(texture, list) else texture
        lines.append(
            f"{fmt_point(a)} {fmt_point(b)} {fmt_point(c)} {tex} "
            f"{u_axis} {v_axis} 0 1 1"
        )
    out.append("{\n" + "\n".join(lines) + "\n}")
    if out is world:
        world_bounds.append(
            (
                tuple(min(p[i] for p in points) for i in range(3)),
                tuple(max(p[i] for p in points) for i in range(3)),
            )
        )


FACE_NAMES = ("down", "up", "ymin", "xmax", "ymax", "xmin")
BOX_FACES = [(0, 1, 2), (4, 7, 6), (0, 4, 5), (1, 5, 6), (2, 6, 7), (3, 7, 4)]


def box(
    x1: float,
    y1: float,
    z1: float,
    x2: float,
    y2: float,
    z2: float,
    texture: str | dict[str, str] = WALL,
    dest: list[str] | None = None,
) -> None:
    assert x2 > x1 and y2 > y1 and z2 > z1, (x1, y1, z1, x2, y2, z2)
    points = [
        (x1, y1, z1), (x2, y1, z1), (x2, y2, z1), (x1, y2, z1),
        (x1, y1, z2), (x2, y1, z2), (x2, y2, z2), (x1, y2, z2),
    ]
    if isinstance(texture, dict):
        face_textures = [texture.get(face, texture.get("default", WALL)) for face in FACE_NAMES]
    else:
        face_textures = texture
    poly(points, BOX_FACES, face_textures, dest)


def prism_xy(
    points: list[tuple[float, float]], z1: float, z2: float,
    texture: str = WALL, dest: list[str] | None = None,
) -> None:
    n = len(points)
    verts = [(x, y, z) for z in (z1, z2) for x, y in points]
    faces = [(0, 1, 2), (n, n + 2, n + 1)]
    faces += [(i, (i + 1) % n, (i + 1) % n + n) for i in range(n)]
    poly(verts, faces, texture, dest)


def prism_xz(
    points: list[tuple[float, float]], y1: float, y2: float,
    texture: str = WALL, dest: list[str] | None = None,
) -> None:
    n = len(points)
    verts = [(x, y, z) for y in (y1, y2) for x, z in points]
    faces = [(0, 1, 2), (n, n + 2, n + 1)]
    faces += [(i, (i + 1) % n, (i + 1) % n + n) for i in range(n)]
    poly(verts, faces, texture, dest)


def prism_yz(
    points: list[tuple[float, float]], x1: float, x2: float,
    texture: str = WALL, dest: list[str] | None = None,
) -> None:
    n = len(points)
    verts = [(x, y, z) for x in (x1, x2) for y, z in points]
    faces = [(0, 1, 2), (n, n + 2, n + 1)]
    faces += [(i, (i + 1) % n, (i + 1) % n + n) for i in range(n)]
    poly(verts, faces, texture, dest)


def add_entity(classname: str, origin: tuple[float, float, float] | None = None,
               brushes: list[str] | None = None, **props: object) -> dict[str, str]:
    values = {"classname": classname}
    if origin is not None:
        values["origin"] = " ".join(f"{v:g}" for v in origin)
    values.update({key: str(value) for key, value in props.items() if value is not None})
    entities.append((values, [] if brushes is None else brushes))
    return values


def brush_entity(classname: str, bounds: tuple[float, float, float, float, float, float],
                 texture: str, **props: object) -> dict[str, str]:
    brushes: list[str] = []
    box(*bounds, texture, brushes)
    return add_entity(classname, brushes=brushes, **props)


def light(x: float, y: float, z: float, power: int = 200, style: int | None = None) -> None:
    add_entity("light", (x, y, z), light=power, style=style)


def torch(x: float, y: float, z: float) -> None:
    add_entity("light_flame_small_yellow", (x, y, z))
    light(x, y, z + 16, 180)
    box(x - 10, y - 10, z - 40, x + 10, y + 10, z - 24, TRIM)
    box(x - 6, y - 6, z - 24, x + 6, y + 6, z - 4, METAL)


def column(x: float, y: float, z0: float, z1: float, radius: float = 28,
           texture: str = "column1_2") -> None:
    pts = [(x + round(radius * math.cos(math.pi / 8 + i * math.pi / 4)),
            y + round(radius * math.sin(math.pi / 8 + i * math.pi / 4))) for i in range(8)]
    prism_xy(pts, z0, z1, texture)
    box(x - radius - 8, y - radius - 8, z0, x + radius + 8, y + radius + 8, z0 + 24, TRIM)
    box(x - radius - 8, y - radius - 8, z1 - 24, x + radius + 8, y + radius + 8, z1, TRIM)


def arch_y(cx: float, y: float, base: float, width: float = 192,
           height: float = 256, thick: float = 24, depth: float = 32,
           texture: str = TRIM) -> None:
    radius = width / 2
    spring = base + height - radius
    box(cx - radius - thick, y, base, cx - radius, y + depth, spring, texture)
    box(cx + radius, y, base, cx + radius + thick, y + depth, spring, texture)
    for i in range(8):
        a, b = i * math.pi / 8, (i + 1) * math.pi / 8
        pts = [
            (round(cx + radius * math.cos(a)), round(spring + radius * math.sin(a))),
            (round(cx + (radius + thick) * math.cos(a)), round(spring + (radius + thick) * math.sin(a))),
            (round(cx + (radius + thick) * math.cos(b)), round(spring + (radius + thick) * math.sin(b))),
            (round(cx + radius * math.cos(b)), round(spring + radius * math.sin(b))),
        ]
        prism_xz(pts, y, y + depth, texture)


def arch_x(cy: float, x: float, base: float, width: float = 192,
           height: float = 256, thick: float = 24, depth: float = 32,
           texture: str = TRIM) -> None:
    radius = width / 2
    spring = base + height - radius
    box(x, cy - radius - thick, base, x + depth, cy - radius, spring, texture)
    box(x, cy + radius, base, x + depth, cy + radius + thick, spring, texture)
    for i in range(8):
        a, b = i * math.pi / 8, (i + 1) * math.pi / 8
        pts = [
            (round(cy + radius * math.cos(a)), round(spring + radius * math.sin(a))),
            (round(cy + (radius + thick) * math.cos(a)), round(spring + (radius + thick) * math.sin(a))),
            (round(cy + (radius + thick) * math.cos(b)), round(spring + (radius + thick) * math.sin(b))),
            (round(cy + radius * math.cos(b)), round(spring + radius * math.sin(b))),
        ]
        prism_yz(pts, x, x + depth, texture)


def stairs_y(x1: float, x2: float, y1: float, count: int, z0: float,
             step: float = 16, run: float = 32, direction: int = 1,
             texture: str = FLOOR) -> None:
    for i in range(count):
        ya, yb = y1 + direction * i * run, y1 + direction * (i + 1) * run
        box(x1, min(ya, yb), z0 - 24, x2, max(ya, yb), z0 + (i + 1) * step,
            {"default": texture, "up": texture, "down": WALL})


def stairs_x(y1: float, y2: float, x1: float, count: int, z0: float,
             step: float = 16, run: float = 32, direction: int = 1,
             texture: str = FLOOR) -> None:
    for i in range(count):
        xa, xb = x1 + direction * i * run, x1 + direction * (i + 1) * run
        box(min(xa, xb), y1, z0 - 24, max(xa, xb), y2, z0 + (i + 1) * step,
            {"default": texture, "up": texture, "down": WALL})


def item(kind: str, x: float, y: float, floor: float = 0, **props: object) -> None:
    offset = 32 if kind.startswith(("item_key", "item_artifact")) else 8
    add_entity(kind, (x, y, floor + offset), **props)


def monster(kind: str, x: float, y: float, floor: float = 0,
            angle: int = 180, flags: int = 1) -> None:
    add_entity("monster_" + kind, (x, y, floor + 24), angle=angle, spawnflags=flags)


def build_shell() -> None:
    xs = sorted({v for a in AIR for v in (a[0], a[3])} | {min(a[0] for a in AIR) - 64, max(a[3] for a in AIR) + 64})
    ys = sorted({v for a in AIR for v in (a[1], a[4])} | {min(a[1] for a in AIR) - 64, max(a[4] for a in AIR) + 64})
    zs = sorted({v for a in AIR for v in (a[2], a[5])} | {min(a[2] for a in AIR) - 64, max(a[5] for a in AIR) + 64})
    solid = set()
    for i in range(len(xs) - 1):
        for j in range(len(ys) - 1):
            for k in range(len(zs) - 1):
                x = (xs[i] + xs[i + 1]) / 2
                y = (ys[j] + ys[j + 1]) / 2
                z = (zs[k] + zs[k + 1]) / 2
                if not any(a <= x <= d and b <= y <= e and c <= z <= f for a, b, c, d, e, f in AIR):
                    solid.add((i, j, k))
    shell_tex = {"default": WALL, "up": FLOOR, "down": CEILING}
    while solid:
        i, j, k = min(solid)
        i2 = i + 1
        while (i2, j, k) in solid:
            i2 += 1
        j2 = j + 1
        while all((a, j2, k) in solid for a in range(i, i2)):
            j2 += 1
        k2 = k + 1
        while all((a, b, k2) in solid for a in range(i, i2) for b in range(j, j2)):
            k2 += 1
        for a in range(i, i2):
            for b in range(j, j2):
                for c in range(k, k2):
                    solid.remove((a, b, c))
        box(xs[i], ys[j], zs[k], xs[i2], ys[j2], zs[k2], shell_tex)


def decorate_entry_and_court() -> None:
    # Two nested arches compress the gatehouse and frame the first view of the court.
    arch_y(512, -512, 0, 256, 304, 32, 40, BRASS)
    arch_y(512, -96, 0, 320, 352, 32, 40, TRIM)
    for x in (320, 688):
        box(x, -592, 0, x + 24, -48, 32, TRIM)
        box(x, -592, 320, x + 24, -48, 352, TRIM)
    box(272, -624, 0, 752, -608, 48, BRASS)
    item("item_shells", 432, -384)
    item("item_health", 592, -256)
    add_entity("info_player_start", (512, -544, 24), angle=90)
    add_entity("info_intermission", (1504, -640, 432), mangle="0 90 0")
    brush_entity("trigger_once", (384, -112, 0, 640, -64, 96), "trigger",
                 message="The Orrery turns above a dark well. A silver seal bars the north stair.")

    # The court's broken square gallery circles a deep, navigable star well.
    box(256, 256, 224, 384, 768, 256, {"default": WALL, "up": FLOOR})
    box(640, 256, 224, 768, 768, 256, {"default": WALL, "up": FLOOR})
    box(384, 256, 224, 640, 384, 256, {"default": WALL, "up": FLOOR})
    box(384, 640, 224, 640, 768, 256, {"default": WALL, "up": FLOOR})
    # Outer gallery landings give the upper routes more than one way around the well.
    box(64, 176, 224, 256, 416, 256, FLOOR)
    box(768, 160, 224, 960, 384, 256, FLOOR)
    box(64, 704, 224, 256, 944, 256, FLOOR)
    box(768, 704, 224, 960, 928, 256, FLOOR)
    stairs_y(800, 896, 80, 16, 0, 16, 24)
    # Side walks connect the stair landing to the raised court gallery.
    box(768, 384, 224, 800, 464, 256, FLOOR)
    box(896, 384, 224, 960, 464, 256, FLOOR)
    box(768, 464, 224, 960, 560, 256, FLOOR)

    # Low parapets are interrupted at the intended drops and stair landings.
    for x in (240, 640):
        box(x, 272, 256, x + 16, 752, 288, TRIM)
    for y in (240, 640):
        box(272, y, 256, 368, y + 16, 288, TRIM)
        box(656, y, 256, 752, y + 16, 288, TRIM)
    for x, y in ((96, 96), (928, 96), (96, 928), (928, 928)):
        column(x, y, 0, 640, 34, "column1_2")
        box(x - 64, y - 64, 608, x + 64, y + 64, 640, DARK)

    # Faceted buttresses, clerestory bands, and colored window alcoves establish the court.
    for y in (192, 512, 832):
        arch_y(512, y, 0, 704, 672, 40, 32, BRASS)
    for x in (32, 960):
        for y in (320, 640, 832):
            box(x, y, 288, x + 32, y + 64, 640, {"default": WALL, "xmax": TRIM, "xmin": TRIM})
    for x in (224, 768):
        for y in (64, 960):
            box(x, y, 640, x + 32, y + 96, 672, DARK)
    for x, y in ((32, 432), (32, 752), (960, 272), (960, 592)):
        box(x, y, 352, x + 32, y + 96, 496, "window02_1")
        box(x - 8, y - 8, 336, x + 40, y + 104, 352, BRASS)

    # The armillary lens hangs over the lower well; its open octagonal rings read at distance.
    cx, cy = 512, 512
    for z, inner, outer in ((448, 104, 136), (496, 32, 56)):
        for i in range(8):
            a = math.pi / 8 + i * math.pi / 4
            b = a + math.pi / 4
            pts = [
                (round(cx + inner * math.cos(a)), round(cy + inner * math.sin(a))),
                (round(cx + outer * math.cos(a)), round(cy + outer * math.sin(a))),
                (round(cx + outer * math.cos(b)), round(cy + outer * math.sin(b))),
                (round(cx + inner * math.cos(b)), round(cy + inner * math.sin(b))),
            ]
            prism_xy(pts, z, z + 24, BRASS)
    for x in (416, 608):
        box(x, 504, 400, x + 16, 520, 704, METAL)
        box(504, x, 400, 520, x + 16, 704, METAL)
    prism_xy([(480, 480), (544, 480), (544, 544), (480, 544)], 416, 448, "wizmet1_1")
    box(504, 504, 560, 520, 520, 704, DARK)
    box(320, 480, 400, 384, 544, 416, WOOD)
    box(640, 480, 400, 704, 544, 416, WOOD)
    for x in (336, 688):
        box(x, 464, 384, x + 16, 464 + 96, 408, BRASS)

    # Climb back from the well on broad steps; the lit alcove is visible below the grate.
    stairs_y(368, 432, 640, 12, -192, 16, 24, -1, "stone1_5")
    box(352, 576, -208, 448, 608, -176, BRASS)
    box(448, 544, -192, 672, 672, -176, "*04water2")
    box(208, 560, -192, 560, 640, -96, "*04water2")
    brush_entity("trigger_secret", (72, 560, -248, 196, 688, -104), "trigger",
                 message="A hidden offering glimmers beneath the Orrery.")
    item("item_rockets", 144, 624, -192)
    light(144, 624, -128, 220)
    for x, y, z in ((128, 48, 160), (896, 48, 160), (128, 896, 160), (896, 896, 160),
                    (96, 496, 352), (928, 496, 352), (512, -320, 240)):
        torch(x, y, z)
    for x, y, z, power in ((512, 272, 560, 260), (512, 752, 560, 280),
                           (176, 512, 352, 160), (848, 512, 352, 180),
                           (512, 512, 400, 120)):
        light(x, y, z, power)


def decorate_west_cloister() -> None:
    # Tomb plinths break grenade lines and create a lower loop around the central crypt.
    for x, y, length in ((-736, 304, 192), (-496, 240, 224), (-288, 624, 192), (-704, 720, 176)):
        box(x, y, 0, x + 112, y + length, 40, {"default": "grave03_2", "up": "grave03_3"})
        box(x + 16, y + 16, 40, x + 96, y + length - 16, 56, BRASS)
    for x, y in ((-752, 192), (-128, 192), (-752, 832), (-128, 832), (-560, 512)):
        column(x, y, 0, 336, 26)
    for y in (192, 832):
        arch_y(-448, y, 0, 448, 352, 24, 32, TRIM)

    # A broad switchback stair rises above the tombs to the silver reliquary.
    stairs_y(-704, -512, 736, 18, 0, 16, 32, 1, "afloor1_4")
    box(-768, 1280, 256, -128, 1376, 288, {"default": WALL, "up": FLOOR})
    box(-640, 1152, 288, -256, 1280, 320, BRASS)
    box(-608, 1120, 288, -288, 1152, 304, FLOOR)
    item("item_key1", -448, 1200, 304)
    box(-560, 1280, 320, -336, 1312, 480, "window03")
    for x in (-608, -304):
        column(x, 1248, 256, 560, 20, "column01_3")
    light(-448, 1200, 432, 300)

    # The upper choir returns to the court from above, so the silver key route forms a loop.
    arch_x(336, -40, 0, 192, 256, 24, 40, BRASS)
    arch_x(992, -40, 256, 192, 224, 32, 40, BRASS)
    # The armor is visible from the stair; the short side gallery is an optional dead end.
    box(-560, 944, 96, -336, 1072, 128, {"default": WALL, "up": BRASS})
    box(-560, 944, 128, -544, 1072, 160, TRIM)
    item("item_armor2", -448, 1008, 128)
    brush_entity("trigger_secret", (-552, 952, 128, -344, 1064, 224), "trigger",
                 message="The choir stair hides a keeper's plate.")
    for x, y, z in ((-768, 384, 152), (-128, 752, 152), (-800, 1024, 408), (-128, 1216, 408)):
        torch(x, y, z)
    item("weapon_supershotgun", -688, 528)
    item("item_shells", -688, 576)
    item("weapon_nailgun", -672, 1024, 256)
    item("item_spikes", -608, 1024, 256)
    item("item_health", -224, 688)
    item("item_armor1", -784, 1088, 256)

    # One cracked rune panel is the clue to the shootable cache door.
    brush_entity("func_button", (-832, 512, 80, -816, 560, 128), "+0shoot",
                 health=1, target="rune_cache", angle=0, wait=-1,
                 message="The cracked rune answers the shot.")
    brush_entity("func_door", (-896, 384, 0, -864, 512, 224), "demc4_4",
                 targetname="rune_cache", angle=180, lip=0, wait=-1, speed=48, sounds=1)
    brush_entity("trigger_secret", (-1120, 336, 8, -928, 560, 184), "trigger",
                 message="A forgotten watcher kept the rune's power.")
    item("item_artifact_super_damage", -1040, 448)
    item("item_rockets", -1008, 496)
    light(-1040, 448, 168, 240)

    monster("knight", -640, 336, angle=0)
    monster("knight", -224, 384, angle=180)
    monster("knight", -320, 752, angle=180)
    monster("ogre", -736, 768, 0, angle=180, flags=1 | 256)
    monster("demon1", -352, 384, angle=0)
    monster("wizard", -640, 1088, 256, angle=0, flags=1 | 256)
    monster("knight", -800, 1200, 256, angle=0)
    monster("knight", -256, 1040, 256, angle=180)
    monster("hell_knight", -448, 960, 256, angle=0, flags=1 | 512)


def decorate_north_planetarium() -> None:
    # The stair goes through the silver seal and opens into a tall, round-reading chamber.
    stairs_y(400, 624, 800, 16, 0, 16, 32, 1, "afloor1_4")
    brush_entity("func_door", (384, 992, 0, 640, 1024, 576), "adoor03_3",
                 spawnflags=16, angle=180, lip=0, wait=-1, sounds=3,
                 message="The silver seal turns toward the stars.")
    arch_y(512, 1096, 256, 320, 400, 32, 48, BRASS)
    for x, y in ((256, 1168), (768, 1168), (256, 1696), (768, 1696)):
        column(x, y, 256, 768, 32, "column01_4")
        box(x - 64, y - 64, 736, x + 64, y + 64, 768, DARK)
    # A raised ring and its broken bridge show that the dome has more than one level.
    box(224, 1280, 480, 352, 1728, 512, {"default": METAL, "up": "metflor2_1"})
    box(672, 1280, 480, 800, 1728, 512, {"default": METAL, "up": "metflor2_1"})
    box(352, 1280, 480, 672, 1408, 512, {"default": METAL, "up": "metflor2_1"})
    box(352, 1600, 480, 672, 1728, 512, {"default": METAL, "up": "metflor2_1"})
    for z in (512, 544):
        for i in range(8):
            a = math.pi / 8 + i * math.pi / 4
            b = a + math.pi / 4
            pts = [(round(512 + r * math.cos(t)), round(1456 + r * math.sin(t)))
                   for r, t in ((176, a), (208, a), (208, b), (176, b))]
            prism_xy(pts, z, z + 16, BRASS)
    for x in (384, 640):
        box(x, 1432, 608, x + 16, 1480, 864, METAL)
    box(464, 1408, 592, 560, 1504, 624, "wizmet1_1")
    box(448, 1392, 576, 576, 1408, 592, BRASS)
    box(448, 1504, 576, 576, 1520, 592, BRASS)
    for x in (160, 832):
        box(x, 1312, 512, x + 32, 1440, 640, "window02_1")
        box(x - 8, 1304, 496, x + 40, 1448, 512, TRIM)
    for x, y, z in ((192, 1248, 448), (832, 1632, 448), (192, 1696, 448), (832, 1216, 448)):
        torch(x, y, z)

    # The pressed star control opens both the low entrance and the upper return shutter.
    brush_entity("func_button", (464, 1576, 320, 560, 1600, 384), "+0button",
                 target="star_shutter", wait=-1, sounds=2, angle=90,
                 message="The Meridian shutters rise. The eastern mechanism wakes.")
    brush_entity("func_door", (1008, 768, 0, 1040, 896, 224), "door03_4",
                 targetname="star_shutter", angle=90, lip=0, wait=-1, speed=64, sounds=4)
    brush_entity("func_door", (1008, 640, 256, 1040, 768, 448), "door03_4",
                 targetname="star_shutter", angle=90, lip=0, wait=-1, speed=64, sounds=4)
    light(512, 1456, 832, 340)
    item("item_health", 320, 1536, 256)
    item("item_shells", 704, 1536, 256)
    monster("hell_knight", 512, 1696, 256, angle=270)
    monster("wizard", 320, 1616, 480, angle=0, flags=1 | 256)
    monster("wizard", 704, 1328, 480, angle=180)
    monster("knight", 512, 1232, 256, angle=180)
    monster("scrag", 512, 1456, 624, angle=180, flags=1 | 256 | 512)
    add_entity("ambient_drone", (512, 1456, 704))


def decorate_east_mechanism() -> None:
    # Stairs and brass counterweights split the room into a lower wet lane and a dry gallery.
    for x in (1120, 1744):
        box(x, 256, 0, x + 48, 1024, 32, BRASS)
    for x, y in ((1200, 352), (1680, 352), (1200, 896), (1680, 896)):
        column(x, y, 0, 448, 32, "mmetal1_1")
        box(x - 56, y - 56, 424, x + 56, y + 56, 448, METAL)
    # Recessed water channel and a broad bridge create alternate combat lanes.
    stairs_y(1248, 1312, 320, 8, 0, 16, 24, 1, "stone1_5")
    stairs_y(1568, 1632, 832, 8, 0, 16, -24, -1, "stone1_5")
    box(1312, 544, -32, 1568, 640, 0, {"default": WOOD, "up": "woodflr1_5"})
    box(1280, 528, -56, 1312, 656, -32, BRASS)
    box(1568, 528, -56, 1600, 656, -32, BRASS)
    box(1360, 480, -112, 1520, 768, -64, "*04water2")
    for y in (384, 704, 928):
        arch_y(1456, y, 0, 512, 544, 32, 32, TRIM)
    # Elevated walkways look back through both activated doors into the Orrery Court.
    box(1120, 560, 224, 1248, 768, 256, {"default": METAL, "up": "metflor2_1"})
    box(1600, 704, 224, 1744, 960, 256, {"default": METAL, "up": "metflor2_1"})
    box(1248, 928, 224, 1600, 1024, 256, {"default": METAL, "up": "metflor2_1"})
    stairs_y(1264, 1456, 832, 16, 0, 16, 32, 1, "afloor1_4")
    box(1248, 1344, 256, 1600, 1440, 288, {"default": WALL, "up": FLOOR})
    arch_y(1424, 1248, 256, 448, 400, 32, 32, BRASS)
    for x, y, z in ((1168, 304, 152), (1712, 480, 152), (1168, 784, 152),
                    (1712, 944, 152), (1168, 976, 384), (1712, 1168, 384)):
        torch(x, y, z)
    light(1456, 576, 400, 280)
    light(1456, 1024, 448, 300)
    item("weapon_grenadelauncher", 1184, 480)
    item("item_rockets", 1184, 528)
    item("item_health", 1680, 832)
    item("item_spikes", 1664, 1280, 256)
    item("item_shells", 1200, 1296, 256)
    monster("ogre", 1200, 736, 0, angle=0)
    monster("ogre", 1696, 608, 0, angle=180, flags=1 | 256)
    monster("demon1", 1456, 736, -64, angle=0)
    monster("ogre", 1696, 384, 0, angle=180)
    monster("knight", 1152, 592, 0, angle=0)
    monster("knight", 1648, 880, 0, angle=180)
    monster("hell_knight", 1456, 1312, 256, angle=0, flags=1 | 512)
    monster("wizard", 1712, 1440, 512, angle=180, flags=1 | 256)


def decorate_east_tower() -> None:
    # A rising square spiral reaches the gold key while leaving a view down into the forge.
    stairs_y(1264, 1456, 1152, 16, 256, 16, 24, 1, "afloor1_4")
    box(1184, 1664, 512, 1680, 1728, 544, {"default": WALL, "up": FLOOR})
    box(1280, 1600, 512, 1584, 1680, 544, BRASS)
    box(1264, 1568, 512, 1600, 1600, 528, FLOOR)
    for x, y in ((1168, 1136), (1712, 1136), (1168, 1712), (1712, 1712)):
        column(x, y, 256, 864, 34, "column01_4")
        box(x - 56, y - 56, 832, x + 56, y + 56, 864, DARK)
    for y in (1120, 1504):
        arch_y(1456, y, 256, 512, 576, 32, 32, BRASS)
    # Gold key altar is visible from the stair and guards the lower gate back at the court.
    box(1360, 1568, 528, 1552, 1680, 560, {"default": TRIM, "up": BRASS})
    item("item_key2", 1456, 1624, 560)
    light(1456, 1624, 704, 320)
    box(1352, 1696, 544, 1560, 1728, 688, "window03")
    box(1336, 1680, 528, 1576, 1696, 544, METAL)
    for x, y, z in ((1184, 1424, 416), (1728, 1536, 416), (1456, 1760, 448)):
        torch(x, y, z)
    item("item_health", 1184, 1488, 256)
    item("item_rockets", 1712, 1248, 256)
    item("item_armor2", 1696, 1648, 512)
    monster("hell_knight", 1360, 1392, 256, angle=0)
    monster("hell_knight", 1600, 1552, 512, angle=180, flags=1 | 256)
    monster("shambler", 1456, 1664, 528, angle=270, flags=1 | 256)
    monster("knight", 1232, 1616, 512, angle=0, flags=1 | 256 | 512)
    monster("ogre", 1712, 1264, 256, angle=180)


def decorate_final_vault() -> None:
    # The gold seal opens into a tall, asymmetric chamber with a low central heart-pit.
    brush_entity("func_door", (1008, 64, 0, 1040, 192, 256), "adoor03_5",
                 spawnflags=8, angle=90, lip=0, wait=-1, sounds=3,
                 message="The gold seal releases the Meridian vault.")
    for x, y in ((1168, -704), (1840, -704), (1168, 128), (1840, 128)):
        column(x, y, 0, 640, 40, "column01_4")
        box(x - 64, y - 64, 608, x + 64, y + 64, 640, DARK)
    for y in (-576, -256, 64):
        arch_y(1504, y, 0, 704, 640, 40, 40, BRASS)
    # Broken side galleries and broad stairs frame the fight without sealing its lanes.
    box(1120, -608, 224, 1280, -448, 256, {"default": METAL, "up": "metflor2_1"})
    box(1728, -512, 224, 1888, -320, 256, {"default": METAL, "up": "metflor2_1"})
    box(1120, -128, 224, 1280, 64, 256, {"default": METAL, "up": "metflor2_1"})
    box(1728, -64, 224, 1888, 96, 256, {"default": METAL, "up": "metflor2_1"})
    stairs_y(1744, 1856, -640, 16, 0, 16, 24, 1, "afloor1_4")
    stairs_y(1424, 1584, -768, 14, 0, 16, 24, 1, "afloor1_4")
    box(1648, -224, 256, 1840, 64, 288, {"default": WALL, "up": FLOOR})
    # The exposed altar crosses the heart pit; generous width keeps the drop readable.
    box(1344, -416, 32, 1664, -288, 64, {"default": WOOD, "up": "woodflr1_5"})
    box(1328, -432, 16, 1360, -272, 32, BRASS)
    box(1648, -432, 16, 1680, -272, 32, BRASS)
    box(1408, -416, 224, 1600, -288, 256, {"default": TRIM, "up": BRASS})
    for z in (320, 352):
        for i in range(8):
            a = math.pi / 8 + i * math.pi / 4
            b = a + math.pi / 4
            pts = [(round(1504 + r * math.cos(t)), round(-352 + r * math.sin(t)))
                   for r, t in ((160, a), (192, a), (192, b), (160, b))]
            prism_xy(pts, z, z + 16, BRASS)
    box(1472, -384, 256, 1536, -320, 304, "wizmet1_1")
    # Short ceremonial stair gives a release after the final fight and makes the exit legible.
    stairs_y(1440, 1568, -704, 8, 256, 16, 24, 1, "afloor1_4")
    box(1376, -704, 384, 1632, -672, 416, BRASS)
    box(1392, -736, 384, 1616, -720, 608, METAL)
    box(1392, -736, 592, 1616, -672, 624, "exit02_3")
    for x in (1360, 1632):
        box(x, -752, 384, x + 32, -656, 624, BRASS)
    brush_entity("trigger_changelevel", (1440, -704, 416, 1568, -656, 560), "trigger", map="start")
    brush_entity("trigger_once", (1408, -688, 416, 1600, -656, 560), "trigger",
                 message="The Meridian falls silent. The gate home is open.")
    brush_entity("func_button", (1872, -672, 336, 1888, -544, 384), "+0shoot",
                 health=1, target="vault_cache", angle=0, wait=-1,
                 message="A shot rings hollow behind the eastern buttress.")
    brush_entity("func_door", (1888, -672, 224, 1920, -544, 480), "demc4_4",
                 targetname="vault_cache", angle=90, lip=0, wait=-1, speed=48, sounds=1)
    brush_entity("trigger_secret", (2000, -656, 232, 2160, -560, 496), "trigger",
                 message="The fortress kept one last charge behind its eastern buttress.")
    item("item_artifact_invulnerability", 2080, -608, 224)
    item("item_rockets", 2048, -640, 224)
    light(2080, -608, 400, 240)
    for x, y, z in ((1184, -368, 152), (1824, -368, 152), (1184, -64, 152),
                    (1824, 64, 152), (1504, -608, 464)):
        torch(x, y, z)
    light(1504, -352, 576, 340)
    light(1504, -640, 560, 280)
    item("weapon_rocketlauncher", 1184, -560)
    item("item_rockets", 1184, -496)
    item("item_health", 1840, -192)
    item("item_shells", 1200, -96)
    item("item_rockets", 1712, -176)
    add_entity("info_intermission", (1504, -624, 432), mangle="0 90 0")
    monster("ogre", 1200, -224, 0, angle=0)
    monster("hell_knight", 1760, -256, 0, angle=180)
    monster("wizard", 1504, -32, 416, angle=270)
    monster("hell_knight", 1360, -480, 0, angle=0, flags=1 | 256)
    monster("shambler", 1504, -352, 256, angle=270, flags=1 | 256)
    monster("knight", 1216, -112, 0, angle=0)
    monster("ogre", 1808, -384, 256, angle=180)
    monster("ogre", 1808, -480, 256, angle=180, flags=1 | 256 | 512)
    monster("demon1", 1504, 32, 0, angle=270, flags=1 | 256 | 512)


def serialize_entity(props: dict[str, str], brushes: list[str]) -> str:
    lines = ["{"]
    for key, value in props.items():
        safe = value.replace("\\", "\\\\").replace('"', "'")
        lines.append(f'"{key}" "{safe}"')
    lines.extend(brushes)
    lines.append("}")
    return "\n".join(lines)


def wad_texture_names() -> set[str]:
    result: set[str] = set()
    for name in WAD_NAMES:
        path = GFX / name
        data = path.read_bytes()
        magic, count, directory = struct.unpack_from("<4sii", data, 0)
        if magic != b"WAD2":
            raise ValueError(f"{path} is not a WAD2 texture archive")
        for i in range(count):
            offset, _, _, kind, _, _, _, raw_name = struct.unpack_from("<iiiBBBB16s", data, directory + i * 32)
            if kind == 0x44:
                result.add(raw_name.split(b"\0")[0].decode("ascii"))
    return result


def main() -> None:
    build_shell()
    decorate_entry_and_court()
    decorate_west_cloister()
    decorate_north_planetarium()
    decorate_east_mechanism()
    decorate_east_tower()
    decorate_final_vault()

    gfx_list = ";".join((GFX / name).as_posix() for name in WAD_NAMES)
    worldspawn = {
        "classname": "worldspawn",
        "message": "The Astral Fortress",
        "worldtype": "1",
        "sounds": "0",
        "wad": gfx_list,
        "_tb_textures": gfx_list,
        "_tb_def": "builtin:Quake.fgd",
        "light": "16",
        "_sunlight": "48",
        "_sun_mangle": "110 -65 0",
    }
    header = "// Game: Quake\n// Format: Valve\n// The Astral Fortress - an original single-player map.\n"
    text = header + serialize_entity(worldspawn, world) + "\n"
    text += "\n".join(serialize_entity(props, brushes) for props, brushes in entities) + "\n"
    target = ROOT / "astral_fortress.map"
    target.write_text(text, encoding="ascii", errors="xmlcharrefreplace")

    keys = wad_texture_names()
    used = set()
    for brush in world:
        for token in brush.splitlines():
            parts = token.split()
            if len(parts) >= 16 and parts[0] == "(":
                used.add(parts[15])
    for _, brushes in entities:
        for brush in brushes:
            for token in brush.splitlines():
                parts = token.split()
                if len(parts) >= 16 and parts[0] == "(":
                    used.add(parts[15])
    missing = sorted(tex for tex in used if not tex.startswith("*") and tex not in keys)

    monster_entities = [p for p, _ in entities if p["classname"].startswith("monster_")]
    stats = {
        "world_brushes": len(world),
        "brush_entities": sum(bool(b) for _, b in entities),
        "point_entities": sum(not b for _, b in entities),
        "secrets": sum(p["classname"] == "trigger_secret" for p, _ in entities),
        "textures_used": len(used),
        "missing_textures": missing,
        "monsters_easy": sum(not (int(p.get("spawnflags", "0")) & 256) for p in monster_entities),
        "monsters_normal": sum(not (int(p.get("spawnflags", "0")) & 512) for p in monster_entities),
        "monsters_hard": sum(not (int(p.get("spawnflags", "0")) & 1024) for p in monster_entities),
        "keys": {p["classname"]: sum(q["classname"] == p["classname"] for q, _ in entities)
                 for p, _ in entities if p["classname"] in ("item_key1", "item_key2")},
        "targetnames": sorted(p["targetname"] for p, _ in entities if "targetname" in p),
        "targets": sorted(p["target"] for p, _ in entities if "target" in p),
    }
    (ROOT / "qa").mkdir(exist_ok=True)
    (ROOT / "qa" / "design_stats.json").write_text(json.dumps(stats, indent=2), encoding="utf-8")
    checks = {
        "source_map_exists": target.exists(),
        "map_format": "Valve",
        "positive_world_brush_bounds": len(world_bounds) == len(world) and all(
            all(hi[i] > lo[i] for i in range(3)) for lo, hi in world_bounds
        ),
        "all_used_textures_in_listed_wads": not missing,
        "all_targets_have_a_receiver": all(t in set(stats["targetnames"]) for t in stats["targets"]),
        "silver_key_door_present": any(p.get("classname") == "func_door" and int(p.get("spawnflags", "0")) & 16
                                        for p, _ in entities),
        "gold_key_door_present": any(p.get("classname") == "func_door" and int(p.get("spawnflags", "0")) & 8
                                      for p, _ in entities),
        "player_start_present": any(p.get("classname") == "info_player_start" for p, _ in entities),
        "exit_present": any(p.get("classname") == "trigger_changelevel" for p, _ in entities),
    }
    (ROOT / "qa" / "source_checks.json").write_text(json.dumps(checks, indent=2), encoding="utf-8")
    print(json.dumps({"map": str(target), "stats": stats, "checks": checks}, indent=2))


if __name__ == "__main__":
    main()
