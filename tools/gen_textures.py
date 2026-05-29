"""
gen_textures.py
成员 C - 程序化生成占位纹理贴图

在没有美术资源的情况下，用 Pillow 生成具有真实感噪声的纹理，
保证程序可以正常运行并展示材质差异。
真实项目中替换为 Poly Haven 等网站下载的高质量贴图即可。
"""

import os
import random
import math
from PIL import Image, ImageDraw, ImageFilter

OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "..", "textures")
os.makedirs(OUTPUT_DIR, exist_ok=True)

SIZE = 512  # 纹理分辨率

random.seed(42)  # 固定随机种子，保证每次生成结果一致


# ─────────────────────────────────────────────
# 工具函数
# ─────────────────────────────────────────────

def noise2d(x, y, scale=1.0, octaves=4):
    """简单分形噪声（不依赖 numpy）"""
    val = 0.0
    amp = 1.0
    freq = scale
    max_val = 0.0
    for _ in range(octaves):
        # 用 sin/cos 叠加模拟噪声
        val += amp * (math.sin(x * freq * 0.1 + 1.3) *
                      math.cos(y * freq * 0.1 + 0.7) +
                      math.sin(x * freq * 0.07 - 0.5) *
                      math.sin(y * freq * 0.13 + 2.1))
        max_val += amp
        amp *= 0.5
        freq *= 2.0
    return val / max_val  # 归一化到 [-1, 1]


def clamp(v, lo=0, hi=255):
    return max(lo, min(hi, int(v)))


def save(img: Image.Image, name: str):
    path = os.path.join(OUTPUT_DIR, name)
    img.save(path)
    print(f"  Generated: {path}")


# ─────────────────────────────────────────────
# 木头纹理
# ─────────────────────────────────────────────

def gen_wood():
    print("[Wood]")
    # 漫反射：木纹条纹
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            # 木纹：沿 x 方向的正弦条纹 + 噪声扰动
            grain = math.sin((x + noise2d(x, y, 2.0) * 30) * 0.15) * 0.5 + 0.5
            r = clamp(160 + grain * 60 + noise2d(x, y, 4) * 15)
            g = clamp(100 + grain * 40 + noise2d(x, y, 4) * 10)
            b = clamp(50  + grain * 20 + noise2d(x, y, 4) * 8)
            pixels[x, y] = (r, g, b)
    img = img.filter(ImageFilter.SMOOTH)
    save(img, "wood_diffuse.png")

    # 高光：低反射（暗灰色）
    spec = Image.new("RGB", (SIZE, SIZE))
    sp = spec.load()
    for y in range(SIZE):
        for x in range(SIZE):
            v = clamp(30 + noise2d(x, y, 3) * 15)
            sp[x, y] = (v, v, v)
    save(spec, "wood_specular.png")


# ─────────────────────────────────────────────
# 石料纹理
# ─────────────────────────────────────────────

def gen_stone():
    print("[Stone]")
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            n = noise2d(x, y, 3.0, octaves=5)
            v = clamp(120 + n * 60)
            r = clamp(v + noise2d(x, y, 6) * 10)
            g = clamp(v + noise2d(x, y, 7) * 8)
            b = clamp(v + noise2d(x, y, 8) * 6)
            pixels[x, y] = (r, g, b)
    img = img.filter(ImageFilter.SMOOTH_MORE)
    save(img, "stone_diffuse.png")

    spec = Image.new("RGB", (SIZE, SIZE))
    sp = spec.load()
    for y in range(SIZE):
        for x in range(SIZE):
            v = clamp(15 + noise2d(x, y, 4) * 10)
            sp[x, y] = (v, v, v)
    save(spec, "stone_specular.png")


# ─────────────────────────────────────────────
# 金属纹理
# ─────────────────────────────────────────────

def gen_metal():
    print("[Metal]")
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            # 拉丝金属：沿 y 方向的细条纹
            streak = math.sin(y * 1.5 + noise2d(x, y, 1.0) * 3) * 0.5 + 0.5
            base = 160 + streak * 50
            r = clamp(base + noise2d(x, y, 8) * 10)
            g = clamp(base + noise2d(x, y, 9) * 10)
            b = clamp(base + 10 + noise2d(x, y, 10) * 10)  # 略偏冷
            pixels[x, y] = (r, g, b)
    img = img.filter(ImageFilter.SMOOTH)
    save(img, "metal_diffuse.png")

    # 高光：高反射（亮灰色）
    spec = Image.new("RGB", (SIZE, SIZE))
    sp = spec.load()
    for y in range(SIZE):
        for x in range(SIZE):
            streak = math.sin(y * 1.5 + noise2d(x, y, 1.0) * 3) * 0.5 + 0.5
            v = clamp(180 + streak * 60 + noise2d(x, y, 5) * 15)
            sp[x, y] = (v, v, v)
    save(spec, "metal_specular.png")


# ─────────────────────────────────────────────
# 发光体纹理
# ─────────────────────────────────────────────

def gen_emissive():
    print("[Emissive]")
    # 漫反射：深色底 + 发光纹路
    img = Image.new("RGB", (SIZE, SIZE))
    pixels = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            # 魔法阵风格：同心圆 + 噪声
            cx, cy = SIZE // 2, SIZE // 2
            dist = math.sqrt((x - cx) ** 2 + (y - cy) ** 2)
            ring = math.sin(dist * 0.15) * 0.5 + 0.5
            n = noise2d(x, y, 2.0) * 0.3
            glow = max(0.0, ring + n)
            r = clamp(20  + glow * 40)
            g = clamp(10  + glow * 80)
            b = clamp(40  + glow * 180)
            pixels[x, y] = (r, g, b)
    save(img, "emissive_diffuse.png")

    # 高光
    spec = Image.new("RGB", (SIZE, SIZE))
    sp = spec.load()
    for y in range(SIZE):
        for x in range(SIZE):
            v = clamp(60 + noise2d(x, y, 3) * 20)
            sp[x, y] = (v, v, v)
    save(spec, "emissive_specular.png")

    # 自发光贴图：蓝紫色光晕
    glow_img = Image.new("RGB", (SIZE, SIZE))
    gp = glow_img.load()
    cx, cy = SIZE // 2, SIZE // 2
    for y in range(SIZE):
        for x in range(SIZE):
            dist = math.sqrt((x - cx) ** 2 + (y - cy) ** 2)
            ring = (math.sin(dist * 0.15) * 0.5 + 0.5) ** 2
            n = noise2d(x, y, 3.0) * 0.2
            intensity = max(0.0, min(1.0, ring + n))
            r = clamp(intensity * 80)
            g = clamp(intensity * 40)
            b = clamp(intensity * 255)
            gp[x, y] = (r, g, b)
    glow_img = glow_img.filter(ImageFilter.GaussianBlur(radius=2))
    save(glow_img, "emissive_glow.png")


# ─────────────────────────────────────────────
# 地板纹理（石砖）
# ─────────────────────────────────────────────

def gen_floor():
    print("[Floor]")
    img = Image.new("RGB", (SIZE, SIZE))
    draw = ImageDraw.Draw(img)

    TILE = 64  # 砖块大小（像素）
    GROUT = 4  # 砖缝宽度

    pixels = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            tx = x % TILE
            ty = y % TILE
            # 砖缝
            if tx < GROUT or ty < GROUT:
                v = clamp(80 + noise2d(x, y, 5) * 10)
                pixels[x, y] = (v, v - 5, v - 10)
            else:
                # 砖块本体：灰棕色 + 噪声
                n = noise2d(x, y, 4.0, octaves=3)
                # 每块砖颜色略有差异
                tile_id = (x // TILE) * 100 + (y // TILE)
                random.seed(tile_id)
                offset = random.randint(-15, 15)
                base = 140 + offset
                r = clamp(base + n * 20)
                g = clamp(base - 10 + n * 15)
                b = clamp(base - 20 + n * 10)
                pixels[x, y] = (r, g, b)

    img = img.filter(ImageFilter.SMOOTH)
    save(img, "floor_diffuse.png")

    spec = Image.new("RGB", (SIZE, SIZE))
    sp = spec.load()
    for y in range(SIZE):
        for x in range(SIZE):
            tx = x % TILE
            ty = y % TILE
            if tx < GROUT or ty < GROUT:
                sp[x, y] = (10, 10, 10)
            else:
                v = clamp(25 + noise2d(x, y, 4) * 10)
                sp[x, y] = (v, v, v)
    save(spec, "floor_specular.png")


# ─────────────────────────────────────────────
# 主入口
# ─────────────────────────────────────────────

if __name__ == "__main__":
    print(f"Generating textures → {os.path.abspath(OUTPUT_DIR)}")
    gen_wood()
    gen_stone()
    gen_metal()
    gen_emissive()
    gen_floor()
    print("Done. All textures generated.")
