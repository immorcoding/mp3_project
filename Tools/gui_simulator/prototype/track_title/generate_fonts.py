#!/usr/bin/env python3
"""生成一次性曲名原型所需的 Noto Sans CJK SC 子集字体。"""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess

SIZES = (10, 12)
SYMBOLS = "晴天不能说的秘密蒲公英的约定回到过去周杰伦最长的电影（）…"


def main() -> None:
    """按命令行给定的 OTF 路径生成 SIZES 中各字号的 LVGL C 数组。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path, help="NotoSansCJKsc-Regular.otf 路径")
    args = parser.parse_args()
    output_dir = Path(__file__).resolve().parent

    for size in SIZES:
        name = f"lv_font_track_title_{size}"
        command = [
            "npx.cmd" if __import__("os").name == "nt" else "npx",
            "--yes",
            "lv_font_conv@1.5.3",
            "--font",
            str(args.font),
            "-r",
            "0x20-0x7E",
            "--symbols",
            SYMBOLS,
            "--size",
            str(size),
            "--bpp",
            "4",
            "--no-compress",
            "--format",
            "lvgl",
            "--lv-include",
            "lvgl.h",
            "--lv-font-name",
            name,
            "-o",
            str(output_dir / f"{name}.c"),
        ]
        subprocess.run(command, check=True)
        output_path = output_dir / f"{name}.c"
        output_path.write_text(
            output_path.read_text(encoding="utf-8").rstrip() + "\n",
            encoding="utf-8",
        )


if __name__ == "__main__":
    main()
