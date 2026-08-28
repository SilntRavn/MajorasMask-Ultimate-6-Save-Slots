from pathlib import Path
import json

from PIL import Image, ImageDraw, ImageFont


SIZE = 64
FONT_SIZE = 56
CHARS = (
    " .?123456ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "一个了吗保制到删哪个存完成开据数是否择请退出选除复确定"
)
def pack_glyph(font: ImageFont.FreeTypeFont, char: str) -> bytes:
    image = Image.new("L", (SIZE, SIZE), 0)
    draw = ImageDraw.Draw(image)
    reference_bbox = font.getbbox("国")
    advance = round(font.getlength(char))
    draw.text(((SIZE - advance) // 2, (SIZE // 16) - reference_bbox[1]), char, fill=255, font=font)
    pixels = list(image.getdata())
    return bytes(((pixels[i] >> 4) << 4) | (pixels[i + 1] >> 4) for i in range(0, len(pixels), 2))


def format_values(values, formatter, per_line: int) -> str:
    rows = []
    for i in range(0, len(values), per_line):
        rows.append("    " + ", ".join(formatter(value) for value in values[i : i + per_line]) + ",")
    return "\n".join(rows)


def main() -> None:
    project = Path(__file__).resolve().parents[1]
    workspace = project.parent
    old_project = workspace / "Z64RE_CN_Mod" / "CNmod"
    font_path = old_project / "fonts" / "华康新综艺体.ttf"
    output = project / "src" / "generated" / "ui_glyphs.inc"
    font = ImageFont.truetype(str(font_path), FONT_SIZE)
    codepoints = sorted({ord(char) for char in CHARS})
    glyphs = b"".join(pack_glyph(font, chr(codepoint)) for codepoint in codepoints)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        "\n".join(
            (
                f"#define MS_UI_GLYPH_COUNT {len(codepoints)}",
                f"#define MS_UI_GLYPH_SIZE {SIZE}",
                f"#define MS_UI_GLYPH_BYTES {SIZE * SIZE // 2}",
                "",
                "static const u16 sMsUiGlyphCodepoints[MS_UI_GLYPH_COUNT] = {",
                format_values(codepoints, lambda value: f"0x{value:04X}", 12),
                "};",
                "",
                "static const u8 sMsUiGlyphBitmaps[MS_UI_GLYPH_COUNT * MS_UI_GLYPH_BYTES] = {",
                format_values(glyphs, lambda value: f"0x{value:02X}", 16),
                "};",
                "",
            )
        ),
        encoding="ascii",
    )

    texture_dir = old_project / "texture-pack-current" / "CN" / "File Select Buttons"
    action_dir = project / "assets" / "action-buttons"
    action_sources = {
        "sMsCopyButtonTexture": action_dir / "sMsCopyButtonTexture.png",
        "sMsEraseButtonTexture": action_dir / "sMsEraseButtonTexture.png",
        "sMsQuitButtonTexture": action_dir / "sMsQuitButtonTexture.png",
        "sMsYesButtonTexture": action_dir / "sMsYesButtonTexture.png",
    }
    textures = tuple(
        (symbol, (action_dir / f"{symbol}.ia16").read_bytes(), 64, 16)
        for symbol in action_sources
    )
    texture_lines = []
    for symbol, source, width, height in textures:
        if isinstance(source, bytes):
            if len(source) != width * height * 2:
                raise ValueError(f"Invalid IA16 texture size for {symbol}: {len(source)}")
            source = bytearray(source)
            if source[1] != 0:
                raise ValueError(f"Expected a transparent first pixel in {symbol}")
            source[0] ^= 1
            values = [int.from_bytes(source[i : i + 2], "big") for i in range(0, len(source), 2)]
        else:
            image = source.resize((width, height), Image.Resampling.LANCZOS)
            values = []
            for red, green, blue, alpha in image.getdata():
                intensity = round((red + green + blue) / 3)
                values.append((intensity << 8) | alpha)
        texture_lines.extend((
            f"static const u16 {symbol}[{width * height}] = {{",
            format_values(values, lambda value: f"0x{value:04X}", 12),
            "};",
            "",
        ))
    (output.parent / "ui_textures.inc").write_text("\n".join(texture_lines), encoding="ascii")

    high_res_dir = project / "build" / "rt64"
    high_res_dir.mkdir(parents=True, exist_ok=True)
    for symbol, filename in {
        "sMsFile1ButtonTexture": "sMsFile1ButtonTexture.png",
        "sMsFile2ButtonTexture": "sMsFile2ButtonTexture.png",
    }.items():
        Image.open(action_dir / filename).convert("RGBA").save(high_res_dir / filename)
    for symbol, path in action_sources.items():
        Image.open(path).convert("RGBA").save(high_res_dir / f"{symbol}.png")
    hashes = {
        "sMsFile1ButtonTexture": "e3c11d70d29a39b8",
        "sMsFile2ButtonTexture": "4c66fc548d665de5",
        "sMsCopyButtonTexture": "ef9f661b8432fd25",
        "sMsEraseButtonTexture": "4542ad3cb340cae2",
        "sMsQuitButtonTexture": "fa228ec5d9a65182",
        "sMsYesButtonTexture": "d574fc764a0c5213",
    }
    (high_res_dir / "rt64.json").write_text(
        json.dumps(
            {
                "configuration": {
                    "autoPath": "rt64",
                    "configurationVersion": 3,
                    "hashVersion": 5,
                    "defaultOperation": "stream",
                    "defaultShift": "none",
                },
                "textures": [
                    {
                        "hashes": {"rice": "", "rt64": rt64_hash},
                        "path": symbol,
                        "operation": "preload",
                    }
                    for symbol, rt64_hash in hashes.items()
                ],
            },
            indent=4,
        )
        + "\n",
        encoding="ascii",
    )


if __name__ == "__main__":
    main()
