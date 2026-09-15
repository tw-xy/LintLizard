#!/usr/bin/env python3
"""
生成 App/app_glyphs.h：给 0.96" SSD1306(128x64) 用的位图字形

  * 中文 16x16：无网络 / 已连接（用系统黑体渲染，1 = 亮点）
  * 方向箭头 40x40：8 个方向 + 1 个"停"图标

数据格式与 bsp_oled.c 的 Glyph 结构一致：行优先、每字节 8 个水平像素、MSB 在左、1 = 点亮。

用法（Windows）：
    python tools/gen_glyphs.py
输出：
    App/app_glyphs.h      （提交进仓库）
    tools/glyph_preview.png（预览图，方便肉眼确认字形没错）
"""

import os
import math
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_H = os.path.join(ROOT, "App", "app_glyphs.h")
OUT_PNG = os.path.join(ROOT, "tools", "glyph_preview.png")

CN_FONT_CANDIDATES = [
    r"C:\Windows\Fonts\simhei.ttf",   # 黑体
    r"C:\Windows\Fonts\msyh.ttc",     # 微软雅黑
    r"C:\Windows\Fonts\simsun.ttc",   # 宋体
]
CN_CHARS = "无网络已连接停前后左右"   # 需要的汉字（左右是给方向文字用的）


def load_font(size):
    for p in CN_FONT_CANDIDATES:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except Exception:
                continue
    raise RuntimeError("找不到中文字体")


def glyph_bytes(img, w, h, threshold=128):
    """把 PIL 灰度图转成行优先、MSB 在左的位图数组"""
    wbytes = (w + 7) // 8
    out = []
    px = img.load()
    for y in range(h):
        row = [0] * wbytes
        for x in range(w):
            v = px[x, y]
            if v >= threshold:
                row[x >> 3] |= 0x80 >> (x & 7)
        out.extend(row)
    return out


def render_cn(ch, w=16, h=16):
    """渲染单个汉字到 w*h 位图（居中、二值化）"""
    img = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(img)
    font = load_font(h)
    # 用 bbox 精确定位，让字在方框里居中
    bbox = d.textbbox((0, 0), ch, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    d.text(((w - tw) / 2 - bbox[0], (h - th) / 2 - bbox[1]), ch, fill=255, font=font)
    return glyph_bytes(img, w, h)


def render_arrow(angle_deg, size=40):
    """画一个方向箭头：angle 0 = 向上，顺时针为正"""
    S = size
    img = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(img)
    cx = cy = (S - 1) / 2
    a = math.radians(angle_deg)

    def rot(x, y):
        # 局部坐标(向上为 -y)按角度旋转
        return (cx + x * math.cos(a) - y * math.sin(a),
                cy + x * math.sin(a) + y * math.cos(a))

    L = S * 0.40          # 杆长
    HW = S * 0.20         # 箭头头部半宽
    SW = S * 0.10         # 杆半宽
    d.polygon([rot(0, -L), rot(-HW, -L + HW * 1.4), rot(-SW, -L + HW * 1.4),
               rot(-SW, L * 0.75), rot(SW, L * 0.75), rot(SW, -L + HW * 1.4),
               rot(HW, -L + HW * 1.4)], fill=255)
    return glyph_bytes(img, S, S)


def render_stop(size=40):
    """停车图标：圆环 + 中间方块"""
    S = size
    img = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(img)
    m = S * 0.08
    d.ellipse([m, m, S - 1 - m, S - 1 - m], outline=255, width=max(2, int(S * 0.06)))
    c = S * 0.28
    d.rectangle([c, c, S - 1 - c, S - 1 - c], fill=255)
    return glyph_bytes(img, S, S)


def emit_c_array(name, data, per_line=12):
    lines = [f"static const uint8_t {name}[] = {{"]
    for i in range(0, len(data), per_line):
        chunk = ", ".join(f"0x{b:02X}" for b in data[i:i + per_line])
        lines.append("    " + chunk + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    entries = []      # (name, w, h, data)

    for ch in CN_CHARS:
        entries.append((f"cn_{ord(ch):04X}", 16, 16, render_cn(ch)))

    for name, ang in [("arrow_up", 0), ("arrow_upright", 45), ("arrow_right", 90),
                      ("arrow_downright", 135), ("arrow_down", 180),
                      ("arrow_downleft", 225), ("arrow_left", 270),
                      ("arrow_upleft", 315)]:
        entries.append((name, 40, 40, render_arrow(ang)))

    entries.append(("icon_stop", 40, 40, render_stop()))

    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write("/* 自动生成：tools/gen_glyphs.py（不要手改）\n")
        f.write(" * 格式：行优先，每字节 8 个水平像素，MSB 在左，1 = 点亮\n */\n")
        f.write("#ifndef __APP_GLYPHS_H\n#define __APP_GLYPHS_H\n\n#include <stdint.h>\n\n")
        for name, w, h, data in entries:
            f.write(f"/* {name}  {w}x{h} */\n")
            f.write(emit_c_array(name, data))
            f.write("\n\n")
        f.write("typedef struct\n{\n    uint8_t        w;\n    uint8_t        h;\n"
                "    const uint8_t *data;\n} Glyph;\n\n")
        f.write("#endif /* __APP_GLYPHS_H */\n")

    # 预览图：把所有字形横排画出来，方便肉眼确认
    pad = 4
    total_w = sum(e[1] for e in entries) + pad * (len(entries) + 1)
    max_h = max(e[2] for e in entries)
    prev = Image.new("L", (total_w, max_h + pad * 2), 0)
    x = pad
    for name, w, h, data in entries:
        wb = (w + 7) // 8
        for yy in range(h):
            for xx in range(w):
                if data[yy * wb + (xx >> 3)] & (0x80 >> (xx & 7)):
                    prev.putpixel((x + xx, pad + yy), 255)
        x += w + pad
    prev.save(OUT_PNG)

    print(f"生成 {OUT_H}（{len(entries)} 个字形）")
    print(f"预览 {OUT_PNG}")


if __name__ == "__main__":
    main()
