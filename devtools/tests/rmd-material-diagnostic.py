#!/usr/bin/env python3
import math
import os
import re
import struct
import subprocess
import sys

W, H = 640, 480
BG = (32, 32, 40, 255)
TEXTURES = {
    "bc002_body_t02": (128, 128), "bc002_face_t02": (64, 128),
    "bc002_foot_t02": (64, 128), "bc002_hair": (64, 32),
    "bc002_eye": (64, 128), "bc002_mauth": (32, 64),
}
IDS = {name: ((i * 67 + 47) & 255, (i * 131 + 83) & 255, (i * 197 + 19) & 255, 255)
       for i, name in enumerate(TEXTURES)}

def write_ppm(path, pixels):
    with open(path, "wb") as f:
        f.write(f"P6\n{W} {H}\n255\n".encode())
        f.write(bytes(c for p in pixels for c in p[:3]))

def load_rgba(path, size):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) != size[0] * size[1] * 4:
        raise ValueError(f"{path}: expected {size[0] * size[1] * 4}, got {len(data)}")
    return data

def edge(a, b, p):
    return (p[0] - a[0]) * (b[1] - a[1]) - (p[1] - a[1]) * (b[0] - a[0])

def raster(triangles, mode, texture_dir):
    pixels: list[tuple[int, int, int, int]] = [tuple(BG)] * (W * H)
    depth = [float("inf")] * (W * H)
    raw = {n: load_rgba(os.path.join(texture_dir, f"{n}.rgba"), s) for n, s in TEXTURES.items()}
    for tri in triangles:
        v = tri["v"]
        area = edge(v[0], v[1], v[2])
        if abs(area) < 1e-6:
            continue
        lo_x = max(0, int(math.floor(min(p[0] for p in v))))
        hi_x = min(W - 1, int(math.ceil(max(p[0] for p in v))))
        lo_y = max(0, int(math.floor(min(p[1] for p in v))))
        hi_y = min(H - 1, int(math.ceil(max(p[1] for p in v))))
        for y in range(lo_y, hi_y + 1):
            for x in range(lo_x, hi_x + 1):
                p = (x + .5, y + .5)
                w0 = edge(v[1], v[2], p) / area
                w1 = edge(v[2], v[0], p) / area
                w2 = edge(v[0], v[1], p) / area
                if min(w0, w1, w2) < 0:
                    continue
                z = w0 * v[0][2] + w1 * v[1][2] + w2 * v[2][2]
                at = y * W + x
                if z >= depth[at]:
                    continue
                depth[at] = z
                u = w0 * v[0][3] + w1 * v[1][3] + w2 * v[2][3]
                tv = w0 * v[0][4] + w1 * v[1][4] + w2 * v[2][4]
                if mode == "id":
                    color = IDS.get(tri["texture"], (255, 255, 255, 255))
                elif mode == "uv":
                    color = (round(max(0, min(1, u)) * 255), round(max(0, min(1, tv)) * 255), 128, 255)
                elif mode == "atlas-slot":
                    name = tri["texture"]
                    base = IDS.get(name, (255, 255, 255, 255))
                    color = (round(base[0] * (0.5 + 0.5 * max(0, min(1, u)))),
                             round(base[1] * (0.5 + 0.5 * max(0, min(1, tv)))),
                             tri["slot"] * 25 % 256, 255)
                else:
                    name = tri["texture"]
                    if name not in raw:
                        color = (255, 255, 255, 255)
                    else:
                        tw, th = TEXTURES[name]
                        tx = int(math.floor((u % 1) * tw)) % tw
                        ty = int(math.floor((tv % 1) * th)) % th
                        o = (ty * tw + tx) * 4
                        color = tuple(raw[name][o:o + 4])
                if mode == "final" and color[3] < 255:
                    a = color[3] / 255
                    color = tuple(round(color[i] * a + BG[i] * (1 - a)) for i in range(3)) + (255,)
                pixels[at] = color
    return pixels

def project(points):
    minx = min(p[0] for p in points); maxx = max(p[0] for p in points)
    miny = min(p[1] for p in points); maxy = max(p[1] for p in points)
    minz = min(p[2] for p in points); maxz = max(p[2] for p in points)
    cx, cy = (minx + maxx) / 2, (miny + maxy) / 2
    scale = min(H * .85 / max(maxy - miny, .0001), W * .95 / max(max(maxx - minx, maxz - minz), .0001))
    return lambda p: (W / 2 + (p[0] - cx) * scale, H / 2 - (p[1] - cy) * scale, p[2])

def amicitia_obj(path):
    verts, uvs, out = [], [], []
    material = ""
    with open(path) as f:
        for line in f:
            fields = line.split()
            if not fields: continue
            if fields[0] == "v": verts.append(tuple(map(float, fields[1:4])))
            elif fields[0] == "vt": uvs.append(tuple(map(float, fields[1:3])))
            elif fields[0] == "usemtl": material = fields[1]
            elif fields[0] == "f":
                q = []
                for item in fields[1:4]:
                    vi, ti = (int(x) for x in item.split("/")[:2])
                    q.append((verts[vi - 1], uvs[ti - 1]))
                out.append((q, material))
    project_fn = project([p for tri, _ in out for p, _ in tri])
    material_textures = {}
    with open(path.replace(".obj", ".mtl"), encoding="utf-8-sig") as mtl:
        slot = None
        for line in mtl:
            fields = line.split()
            if fields and fields[0] == "newmtl":
                slot = int(fields[1].split("-")[-1])
            elif fields and fields[0] == "map_Kd" and slot is not None:
                material_textures[slot] = os.path.basename(fields[1]).rsplit(".", 1)[0]
    return [{"v": [(*project_fn(p), uv[0], uv[1]) for p, uv in tri],
              "texture": material_textures[int(material.split("-")[-1])],
              "slot": int(material.split("-")[-1])}
            for tri, material in out]

def novelvm_dump(path):
    out = []
    rx = re.compile(r"^renderTri=.*?texture=([^ ]+).*?v0=([^,]+),([^,]+),([^,]+),([^,]+),([^,]+),[^ ]+ v1=([^,]+),([^,]+),([^,]+),([^,]+),([^,]+),[^ ]+ v2=([^,]+),([^,]+),([^,]+),([^,]+),([^,]+),")
    with open(path) as f:
        for line in f:
            m = rx.match(line)
            if not m: continue
            vals = list(map(float, m.groups()[1:]))
            v = []
            for i in range(3):
                x, y, z, u, tv = vals[i * 5:i * 5 + 5]
                v.append((x * W / 100, y * H / 100, z, u, tv))
            out.append({"v": v, "texture": m.group(1), "slot": 0})
    return out

def main(obj, dump, texture_dir, out):
    a = amicitia_obj(obj)
    n = novelvm_dump(dump)
    os.makedirs(out, exist_ok=True)
    for label, tris in (("amicitia", a), ("novelvm", n)):
        for mode in ("final", "id", "uv", "atlas-slot"):
            ppm = os.path.join(out, f"{label}-{mode}.ppm")
            write_ppm(ppm, raster(tris, mode, texture_dir))
            subprocess.run(["convert", ppm, os.path.join(out, f"{label}-{mode}.png")], check=True)

if __name__ == "__main__":
    main(*sys.argv[1:])
