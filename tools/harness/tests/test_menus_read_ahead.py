"""The PC menus' files, read on a thread from the start: the first load waits for it, and they are read once."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, run, function, mutated, CHECK_FAILED

SOURCE = 'port/linux/src/menu_files.c'


def generated(fault=None):
    source = read(SOURCE)
    state = re.search(r"^static struct halo_menus menus;\n.*?^static int menus_reading;\n", source, re.M | re.S).group(0)
    code = '\n'.join(function(source, name) for name in ('menus_files_reader', 'halo_menus_read_ahead', 'halo_menus_load'))
    if fault == 'join':
        code = mutated(code, '\t\tpthread_join(menus_reader, NULL);\n', '')
    if fault == 'once':
        code = mutated(code, 'if (!menus_read && !menus_reading && ', 'if (!menus_reading && ')
    return (('state.inc', state), ('under_test.inc', code))


def built(fault=None):
    return build('menus_read_ahead', generated(fault))


@pytest.mark.parametrize('case', ['ahead', 'again', 'none'])
def test_read_ahead(case):
    status, output = run(built(), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [('join', 'ahead'), ('once', 'again')])
def test_negative(fault, case):
    status, output = run(built(fault), case)
    assert status == CHECK_FAILED, output


def test_read_ahead_after_renderer():
    """the files are read once the renderer's capabilities and the VR mode are set: their platform="hdr" and "vr"
    rows are kept or left out as they are read (Brightness lost its HDR rows when the reading began at the
    platform's start, before the renderer's)"""
    ready = function(read('port/linux/src/d3d8_device.c'), 'device_backend_ready')
    ahead = ready.index('halo_menus_read_ahead();')
    assert ready.index('xgpu_backend_initialize();') < ahead
    assert ready.index('vr_initialize();') < ahead
    for path in ('port/linux/src/sdl_platform.c', 'port/linux/game/menu_tags.c'):
        assert 'halo_menus_read_ahead()' not in read(path), path
