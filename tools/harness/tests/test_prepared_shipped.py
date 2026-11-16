"""The stock maps' records shipped with the game (port/assets/prepared, tools/prepared_records.py) are of the form
the code makes and plays records in: else the game passes every one over and walks each map as if none were shipped.
"""
import json
import re
import struct
import sys
import zlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ROOT, build, read, run, structure, constant, mutated

DEVICE = 'port/linux/src/d3d8_device.c'
PREPARED = 'port/linux/game/render_prepared.c'
FOLDER = ROOT / 'port/assets/prepared'
MANIFEST = json.loads((FOLDER / 'manifest.json').read_text())
RERUN = ('run tools/prepared_records.py again on a Linux machine with the stock maps (one command: see its '
         'header) and commit port/assets/prepared')


def generated(fault=None):
    device, prepared = read(DEVICE), read(PREPARED)
    xdk = read('port/include/xdk/xdk_d3d8.h') + read('port/include/xdk/xdk_pdb.h')
    lines = ['#define %s %d' % (name, constant(xdk, name)) for name in ('D3DTSS_MAXSTAGES', 'D3DRS_MAX', 'D3DTSS_MAX')]
    lines += [structure(device, 'journal_object'), structure(device, 'journal_state'),
              '#define JOURNAL_FORM %d' % constant(device, 'JOURNAL_FORM'),
              re.search(r'^#define PREPARED_VERSION .*$', prepared, re.M).group(0)]
    text = '\n'.join(lines) + '\n'
    if fault == 'version':
        text = mutated(text, '#define PREPARED_VERSION ', '#define PREPARED_VERSION 1 + ')
    if fault == 'state':
        text = mutated(text, '\tuint32_t viewport[6];', '\tuint32_t viewport[7];')
    return (('form.inc', text),)


def code_form(fault=None):
    status, output = run(build('prepared_shipped', generated(fault)), '')
    assert status == 0, output
    version, form = output.split()
    return int(version), int(form)


def test_records_are_of_the_codes_form():
    version, form = code_form()
    assert (MANIFEST['prepared_version'], MANIFEST['journal_form']) == (version, form), (
        f"the shipped records are of form {MANIFEST['prepared_version']}, journal {MANIFEST['journal_form']:#x}, "
        f"and the code makes form {version}, journal {form:#x}: every player would walk every map again. "
        f"The records' form has changed: {RERUN}.")


def test_negative():
    """a changed PREPARED_VERSION or journal state is seen"""
    form = code_form()
    assert code_form('version') != form
    assert code_form('state') != form


def test_records_are_whole():
    """the shipped file (prepared_shipped.h's form) holds the manifest's records, each whole and of its map and key"""
    records = MANIFEST['records']
    assert len(records) == 24, f"not every stock map's record is shipped: {RERUN}"
    assert {path.name for path in FOLDER.iterdir()} == {'manifest.json', MANIFEST['file']}, f"files not in the manifest: {RERUN}"
    pack = (FOLDER / MANIFEST['file']).read_bytes()
    assert struct.unpack_from('<3I', pack) == (0x5331484F, 1, len(records)), RERUN
    for at, record in enumerate(records):
        checksum, high, low, offset, compressed, size = struct.unpack_from('<6I', pack, 12 + 24 * at)
        key = int(record['key'], 16)
        assert (checksum, high, low) == (int(record['checksum'], 16), key >> 32, key & 0xFFFFFFFF), record['map']
        assert (offset, compressed, size) == (record['offset'], record['compressed_size'], record['size']), record['map']
        data = zlib.decompress(pack[offset:offset + compressed])
        magic, version, header_size, header_checksum, header_key = struct.unpack_from('<4IQ', data)
        assert len(data) == size, record['map']
        assert (magic, header_size) == (0x50314F48, 48), record['map']
        assert version == MANIFEST['prepared_version'], f"{record['map']}: {RERUN}"
        assert (header_checksum, header_key) == (checksum, key), record['map']
