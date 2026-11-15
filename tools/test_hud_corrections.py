"""Supplied reticle/zoom artwork fidelity and CE sprite selection."""
import json
from pathlib import Path

import pytest

np = pytest.importorskip("numpy")
Image = pytest.importorskip("PIL.Image")

from hud_assets import build_asset

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'port/assets/hud'


def test_packed_corrections_use_supplied_pixels():
    for filename in ['layout.json', 'ce_layout.json']:
        for entry in json.loads((ASSETS / filename).read_text())['assets']:
            if not any('source_png' in cell for cell in entry['cells']):
                continue
            actual = np.asarray(Image.open(ASSETS / (entry['name']+'.png')))
            assert np.array_equal(actual, build_asset(entry, {}))
            # These assets are monochrome premultiplied coverage, not opaque
            # white pictures with black backgrounds or black letter holes.
            assert np.array_equal(actual[..., 0], actual[..., 3])
            assert np.array_equal(actual[..., 1], actual[..., 3])
            assert np.array_equal(actual[..., 2], actual[..., 3])
            for cell in entry['cells']:
                with Image.open(ASSETS / cell['source_png']) as supplied:
                    source = supplied.convert('L').crop(cell['crop'])
                x0,y0,x1,y1 = [round(v*entry['scale']) for v in cell['xbox']]
                ratio = min((x1-x0)/source.width, (y1-y0)/source.height)
                size = tuple(round(n*ratio) for n in source.size)
                x0 += (x1-x0-size[0])//2
                y0 += (y1-y0-size[1])//2
                expected = np.asarray(source.resize(size, Image.Resampling.LANCZOS))
                assert np.array_equal(actual[y0:y0+size[1], x0:x0+size[0], 3], expected)
