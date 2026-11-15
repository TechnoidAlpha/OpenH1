"""A map's preparation's record: read back as recorded, malformed or stale records refused, one record for every
settings."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, run, function, enum_with, structure, constant, mutated, CHECK_FAILED

DEVICE = 'port/linux/src/d3d8_device.c'
PREPARED = 'port/linux/game/render_prepared.c'


def generated(fault=None):
    device, prepared = read(DEVICE), read(PREPARED)
    types = '\n'.join((enum_with(device, 'JOURNAL_DRAW = 1'), structure(device, 'journal_object'),
                       structure(device, 'journal_state'), structure(device, 'journal_reader')))
    types += '\n#define JOURNAL_STATE_WORDS (sizeof(struct journal_state) / sizeof(uint32_t))\n'
    types += '#define JOURNAL_SEEN_SLOTS %d\n' % constant(device, 'JOURNAL_SEEN_SLOTS')
    types += '#define JOURNAL_PAGE_UNIT %#x\n' % constant(device, 'JOURNAL_PAGE_UNIT')
    code = '\n'.join(function(device, name) for name in ('journal_reserve', 'journal_append', 'journal_runs',
                                                          'journal_read',
                                                          'journal_settings_blind_of'))
    checks = '\n'.join((structure(prepared, 'prepared_header'),
                        *(re.search(r'^#define %s .*$' % name, prepared, re.M).group(0)
                          for name in ('PREPARED_MAGIC', 'PREPARED_VERSION', 'PREPARED_MAXIMUM_WORDS')),
                        function(prepared, 'prepared_hash_string'),
                        function(prepared, 'prepared_key_of'),
                        function(prepared, 'prepared_key'),
                        function(prepared, 'render_prepared_played'),
                        function(prepared, 'prepared_header_fits'),
                        function(prepared, 'render_prepared_draws_missing')))
    if fault == 'repeats':
        code = mutated(code, '\thash = xgpu_manifest_hash(state, sizeof(*state), hash);\n', '')
    if fault == 'delta':
        code = mutated(code, '\tjournal.last = *state;\n', '')
    if fault == 'type':
        code = mutated(code, '\t\telse\n\t\t{\n\t\t\treturn 0;\n\t\t}\n\t}\n\treturn 1;', '\t}\n\treturn 1;')
    if fault == 'numbers':
        # (the old form: the writer's own pages' numbers)
        code = mutated(code, '(uint32_t)(page * (page_size / JOURNAL_PAGE_UNIT))', '(uint32_t)page')
        code = mutated(code, '(uint32_t)(end * (page_size / JOURNAL_PAGE_UNIT))', '(uint32_t)end')
    if fault == 'own_pages':
        # (the old reader: the words as the reader's own pages, up to its own count)
        code = mutated(code, 'words[position + 1] > window_size / JOURNAL_PAGE_UNIT',
                       'words[position + 1] > window_size / page_size')
        code = mutated(code, 'first = words[position] * JOURNAL_PAGE_UNIT / page_size;', 'first = words[position];')
        code = mutated(code, 'end = (words[position + 1] * JOURNAL_PAGE_UNIT + page_size - 1) / page_size;',
                       'end = words[position + 1];')
    if fault == 'floor':
        code = mutated(code, '(words[position + 1] * JOURNAL_PAGE_UNIT + page_size - 1) / page_size',
                       'words[position + 1] * JOURNAL_PAGE_UNIT / page_size')
    if fault == 'shared':
        code = mutated(code, '\t\t\t\tif (first < given)\n\t\t\t\t\tfirst = given;\n', '')
    if fault == 'missing':
        checks = mutated(checks, 'if (other == made_count || made[other] != recorded[index])',
                         'if (other == made_count)')
    if fault == 'key':
        checks = mutated(checks, 'header->key == key &&', '')
    if fault == 'blind':
        code = mutated(code, '\twords[2] = (unsigned long)(hdr_view != 0) | (unsigned long)(lights > 0) << 1 |\n\t\t(unsigned long)(environment != 0) << 2 | (unsigned long)world_span << 3;\n', '\twords[2] = 0;\n')
    if fault == 'span':
        # (a record made with the setting off: a found window's draw and a mirror's merged)
        code = mutated(code, ' | (unsigned long)world_span << 3;', ';')
    if fault == 'tags':
        code = mutated(code, '\twords[1] = (unsigned long)hdr_kind;\n', '\twords[1] = 0;\n')
    if fault == 'derivation':
        checks = mutated(checks, 'return formed && !unknown && (!missing || !same_derivation);',
                         'return formed && !unknown;')
    if fault == 'scenario':
        checks = mutated(checks, 'return prepared_hash_string(scenario, xgpu_manifest_hash(words, sizeof(words), XGPU_MANIFEST_HASH_START));',
                         'return xgpu_manifest_hash(words, sizeof(words), XGPU_MANIFEST_HASH_START);')
    return (('journal.inc', types), ('journal_code.inc', code), ('prepared.inc', checks))


@pytest.mark.parametrize('case', ['roundtrip', 'malformed', 'pages', 'missing', 'stale', 'settings', 'key'])
def test_prepared_journal(case):
    status, output = run(build('prepared_journal', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [('repeats', 'roundtrip'), ('delta', 'roundtrip'), ('type', 'malformed'),
                                        ('missing', 'missing'), ('key', 'stale'), ('blind', 'settings'),
                                        ('tags', 'settings'), ('span', 'settings'), ('derivation', 'key'),
                                        ('scenario', 'key'), ('numbers', 'pages'), ('own_pages', 'pages'),
                                        ('floor', 'pages'), ('shared', 'pages')])
def test_negative(fault, case):
    status, output = run(build('prepared_journal', generated(fault)), case)
    assert status == CHECK_FAILED, output
