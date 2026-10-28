"""生成唱盘页曲名使用的中文字体 bin。

输入固定为 Noto Sans CJK SC Regular 2.004 OTF（校验 SHA-256）与
``lv_font_conv`` 1.5.3；输出为未压缩的 lv_font_conv bin，入库后由
``Tools/package_maker`` 直接读取。日常打包与 CI 不运行本脚本，也不依赖
Node 或 OTF。

字集（本脚本是唯一来源）：ASCII 0x20–0x7E、Latin-1 补充区 U+00A0–U+00FF、U+2013、U+2122，
与 CPython 的 ``gb2312`` 编解码器按区位确定性生成的 GB2312 第 1–9 区 682 个符号、
第 16–87 区 6763 个汉字取并集去重。

用法::

    python generate_font.py --otf <NotoSansCJKsc-Regular.otf>            # 生成
    python generate_font.py --otf <NotoSansCJKsc-Regular.otf> --check    # 重新生成并与已入库 bin 比对
"""

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


OTF_SHA256 = "2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b"
LV_FONT_CONV_VERSION = "1.5.3"
FONT_PIXEL_SIZE = 12
FONT_BITS_PER_PIXEL = 4

ASCII_FIRST = 0x20
ASCII_LAST = 0x7E
LATIN1_FIRST = 0xA0
LATIN1_LAST = 0xFF
EXTRA_PUNCTUATION = (0x2013, 0x2122)
GB2312_SYMBOL_COUNT = 682
GB2312_HANZI_COUNT = 6763
GB2312_FIRST_BYTE_RANGE = range(0xA1, 0xF8)
GB2312_SECOND_BYTE_RANGE = range(0xA1, 0xFF)
GB2312_SYMBOL_LAST_FIRST_BYTE = 0xA9
GB2312_HANZI_FIRST_FIRST_BYTE = 0xB0

OUTPUT_PATH = Path(__file__).resolve().parent / "noto_sans_cjk_sc_12_4bpp.bin"


def gb2312_characters() -> tuple[str, str]:
    """按区位顺序返回 (符号, 汉字)，字符均经 gb2312 编解码器得到。"""

    symbols: list[str] = []
    hanzi: list[str] = []

    for first in GB2312_FIRST_BYTE_RANGE:
        for second in GB2312_SECOND_BYTE_RANGE:
            try:
                character = bytes([first, second]).decode("gb2312")
            except UnicodeDecodeError:
                continue

            if first <= GB2312_SYMBOL_LAST_FIRST_BYTE:
                symbols.append(character)
            elif first >= GB2312_HANZI_FIRST_FIRST_BYTE:
                hanzi.append(character)

    if len(symbols) != GB2312_SYMBOL_COUNT or len(hanzi) != GB2312_HANZI_COUNT:
        raise RuntimeError(
            f"GB2312 字表数量异常：符号 {len(symbols)}、汉字 {len(hanzi)}"
        )

    return "".join(symbols), "".join(hanzi)


def charset_codepoints() -> list[int]:
    """返回去重后的全部码点（升序）：ASCII、Latin-1 补充区、两个标点与 GB2312 符号和汉字。"""

    symbols, hanzi = gb2312_characters()
    codepoints = set(range(ASCII_FIRST, ASCII_LAST + 1))
    codepoints.update(range(LATIN1_FIRST, LATIN1_LAST + 1))
    codepoints.update(EXTRA_PUNCTUATION)
    codepoints.update(ord(character) for character in symbols + hanzi)
    return sorted(codepoints)


def charset_symbols() -> str:
    """返回传给 ``--symbols`` 的全部非 ASCII 字符（升序）。"""

    return "".join(chr(code) for code in charset_codepoints() if code > ASCII_LAST)


def sha256_of(path: Path) -> str:
    """计算文件的 SHA-256 十六进制摘要。"""

    return hashlib.sha256(path.read_bytes()).hexdigest()


def install_lv_font_conv(prefix: Path) -> Path:
    """把固定版本的 lv_font_conv 安装到 prefix，返回其入口脚本路径。"""

    npm = shutil.which("npm")
    if npm is None:
        raise RuntimeError("找不到 npm；生成字体需要 Node.js")

    subprocess.run(
        [
            npm, "install", "--prefix", str(prefix), "--no-audit", "--no-fund",
            "--no-package-lock", f"lv_font_conv@{LV_FONT_CONV_VERSION}",
        ],
        check=True,
    )

    package_directory = prefix / "node_modules" / "lv_font_conv"
    package = json.loads((package_directory / "package.json").read_text(encoding="utf-8"))
    if package["version"] != LV_FONT_CONV_VERSION:
        raise RuntimeError(f"lv_font_conv 版本不符：{package['version']}")

    return package_directory / "lv_font_conv.js"


def generate(otf: Path, output: Path) -> None:
    """校验 OTF 后调用 lv_font_conv 生成未压缩 bin。"""

    actual = sha256_of(otf)
    if actual != OTF_SHA256:
        raise RuntimeError(f"OTF 的 SHA-256 不符：{actual}，预期 {OTF_SHA256}")

    node = shutil.which("node")
    if node is None:
        raise RuntimeError("找不到 node；生成字体需要 Node.js")

    with tempfile.TemporaryDirectory() as temporary_directory:
        # 直接调用 node，而不是 npx.cmd：Windows 的 cmd.exe 命令行上限为 8191 字符，
        # 且按系统代码页传参，7000 余个汉字会被截断或转码损坏。
        entry = install_lv_font_conv(Path(temporary_directory))
        subprocess.run(
            [
                node, str(entry),
                "--font", str(otf),
                "--size", str(FONT_PIXEL_SIZE),
                "--bpp", str(FONT_BITS_PER_PIXEL),
                "--format", "bin",
                "--no-compress",
                "-r", f"0x{ASCII_FIRST:X}-0x{ASCII_LAST:X}",
                "--symbols", charset_symbols(),
                "-o", str(output),
            ],
            check=True,
        )


def main() -> int:
    """命令行入口。"""

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--otf", type=Path, required=True, help="Noto Sans CJK SC Regular 2.004 OTF")
    parser.add_argument("--output", type=Path, default=OUTPUT_PATH, help="输出 bin 路径")
    parser.add_argument("--check", action="store_true", help="重新生成并与 --output 比对，不覆盖")
    arguments = parser.parse_args()

    if arguments.check:
        with tempfile.TemporaryDirectory() as temporary_directory:
            regenerated = Path(temporary_directory) / "font.bin"
            generate(arguments.otf, regenerated)
            if regenerated.read_bytes() != arguments.output.read_bytes():
                print("字体 bin 与重新生成的结果不一致", file=sys.stderr)
                return 1
        print("字体 bin 与重新生成的结果一致")
        return 0

    generate(arguments.otf, arguments.output)
    print(f"字集 {len(charset_codepoints())} 个字符")
    print(f"已生成 {arguments.output}（{arguments.output.stat().st_size} B，SHA-256 {sha256_of(arguments.output)}）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
