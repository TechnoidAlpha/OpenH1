"""The supplied artwork and its normal-build embedding need no image library."""
import hashlib
import re
import struct
import shutil
import subprocess
import sys
import zlib
from pathlib import Path

import pytest

import embed_assets


def test_committed_loading_payload():
    directory = Path(__file__).resolve().parents[1] / "port/assets/loading"
    assert hashlib.sha256((directory / "LOADING.jpg").read_bytes()).hexdigest() == (
        "5062ff5c83664c6b2f692657ae864e9ad74d71b0af8af1e4305bd9a26dc3fa0f")
    rgba = zlib.decompress((directory / "loading.rgba.zlib").read_bytes())
    assert len(rgba) == 640 * 480 * 4
    assert rgba[3::4] == bytes([255]) * (640 * 480)


@pytest.mark.parametrize("present", [False, True])
def test_loading_embedding(tmp_path, monkeypatch, present):
    payload = b"\x12\x34\x56\x78\x9a"
    monkeypatch.setattr(embed_assets, "ROOT", tmp_path)
    asset = tmp_path / embed_assets.LOADING_PICTURE
    if present:
        asset.parent.mkdir(parents=True)
        asset.write_bytes(payload)
    assert (embed_assets.LOADING_PICTURE in embed_assets.hud_asset_inputs()) == present
    output = tmp_path / "embedded.c"
    monkeypatch.setattr(sys, "argv", ["embed_assets.py", str(output)])
    embed_assets.main()
    code = output.read_text()
    assert f"platform_loading_picture_size = {len(payload) if present else 0};" in code
    body = code.split("const unsigned int platform_loading_picture[] = {", 1)[1].split("};", 1)[0]
    if present:
        data = b"".join(struct.pack("<I", int(word, 16)) for word in re.findall(r"0x([0-9a-fA-F]+)", body))
        assert data[:len(payload)] == payload
    else:
        assert body.strip() == "0,"
    compiler = shutil.which("cc")
    if not compiler:
        pytest.skip("needs a C preprocessor")
    # Use the generated declarations after preprocessing; a source-text
    # search alone misses guard mistakes.
    for name in ("hud_hires.h", "text_hires.h", "menu_files.h", "menu_pictures.h"):
        (tmp_path / name).write_text("")
    result = subprocess.run([compiler, "-E", "-P", "-I", str(tmp_path), str(output)],
                            check=True, capture_output=True, text=True).stdout
    assert "const unsigned int platform_loading_picture[]" in result
    assert f"platform_loading_picture_size = {len(payload) if present else 0};" in result
