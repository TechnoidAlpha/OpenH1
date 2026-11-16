"""The vertex mirror's pages on hosts whose pages are larger than 4 KB (16 KB: Android's and ARM Linux's kernels
that use them; 64 KB: some ARM Linux ones), and 4 KB: the mirror's page is the write tracking's, rewrites and
volatile pages are tracked at it, and a record's pages play back as its pages."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, run, function, enum_with, structure, constant, mutated, CHECK_FAILED

DEVICE = 'port/linux/src/d3d8_device.c'


def generated(fault=None):
    device = read(DEVICE)
    # (the mirror's constants, states and arrays, and how its page is chosen)
    mirror = re.search(r'^#define MIRROR_SEGMENT_SIZE .*?^\} mirror;\n', device, re.M | re.S).group(0)
    mirror += function(device, 'mirror_page_size_set') + '\n'
    types = '\n'.join((enum_with(device, 'JOURNAL_DRAW = 1'), structure(device, 'journal_object'),
                       structure(device, 'journal_state'), structure(device, 'journal_reader')))
    types += '\n#define JOURNAL_STATE_WORDS (sizeof(struct journal_state) / sizeof(uint32_t))\n'
    types += '#define JOURNAL_PAGE_UNIT %#x\n' % constant(device, 'JOURNAL_PAGE_UNIT')
    journal = ('static struct { uint32_t *words; unsigned long count, capacity; BOOL failed; '
               'unsigned char pages[MIRROR_PAGE_SLOTS / 8]; } journal;\n')
    code = '\n'.join(function(device, name) for name in (
        'mirror_refresh', 'd3d8_mirror_rewritten', 'mirror_range', 'journal_page', 'journal_reserve',
        'journal_runs', 'journal_read', 'journal_replay_pages'))
    if fault == 'page_size':
        # (the mirror's page fixed at 4 KB, as everywhere but Apple's hardware before)
        mirror = mutated(mirror, '\t\tmirror_page_size = size;\n', '\t\tmirror_page_size = MIRROR_PAGE_UNIT;\n')
    if fault == 'rewrite':
        # (a page the game wrote taken for current)
        code = mutated(code, '\t\tstale[page - first] = mirror.state[page] != _mirror_page_present || written;\n',
                       '\t\tstale[page - first] = mirror.state[page] != _mirror_page_present;\n')
    if fault == 'doubling':
        code = mutated(code, '(MIRROR_VOLATILE_FRAMES << (mirror.volatile_times[page] ? mirror.volatile_times[page] - 1 : 0))',
                       'MIRROR_VOLATILE_FRAMES')
    if fault == 'floor':
        code = mutated(code, '(words[position + 1] * JOURNAL_PAGE_UNIT + page_size - 1) / page_size',
                       'words[position + 1] * JOURNAL_PAGE_UNIT / page_size')
    return (('mirror.inc', '\n'.join((mirror, types, journal, code))),)


SIZES = ['4096', '16384', '65536']


@pytest.mark.parametrize('size', SIZES)
@pytest.mark.parametrize('case', ['size', 'grouping', 'steady', 'volatile', 'rewritten', 'replay'])
def test_mirror_pages(case, size):
    status, output = run(build('mirror_pages', generated()), f'{case}@{size}')
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [('page_size', 'size@16384'), ('page_size', 'grouping@16384'),
                                        ('page_size', 'grouping@65536'),
                                        ('rewrite', 'steady@16384'), ('doubling', 'volatile@16384'),
                                        ('floor', 'replay@16384')])
def test_mirror_pages_negative(fault, case):
    status, output = run(build('mirror_pages', generated(fault)), case)
    assert status == CHECK_FAILED, f'{fault}: {case} passes without the rule\n{output}'
