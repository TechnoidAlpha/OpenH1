"""The tag validator leaves out the passes an element has nothing to do in, and walks, corrects and refuses as before."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, run, function, enum_with, structure, constant, mutated, CHECK_FAILED

SOURCE = 'port/linux/game/tag_validate.c'
SCHEMA = 'port/linux/game/tag_schema.h'


def generated(fault=None):
    source, schema = read(SOURCE), read(SCHEMA)
    summaries = source[source.index('struct definition_summary\n{'):source.index('/* one pass over an element')]
    walk = '\n'.join(function(source, name) for name in ('validate_fields', 'validate_element'))
    walk = walk.replace('definition_summary_get(', 'summary_of(')
    code = summaries + walk
    if fault == 'checks':
        code = mutated(code, '\t\t\tsummary->flags |= element->flags & FLAG(_summary_checks_bit);\n', '')
    if fault == 'depth':
        code = mutated(code, 'validation->depth + summary->depth <= MAXIMUM_VALIDATION_DEPTH &&', '')
    if fault == 'data':
        code = mutated(code, '\t\tcase _tag_schema_file_data:\n\t\t\tsummary->flags |= FLAG(_summary_extents_bit);',
                       '\t\tcase _tag_schema_file_data:')
    enums = '\n'.join((enum_with(schema, '_tag_schema_block,'), enum_with(source, '_pass_extents'),
                       enum_with(source, '_summary_extents_bit')))
    enums += '\n#define MAXIMUM_VALIDATION_DEPTH %d\n' % constant(source, 'MAXIMUM_VALIDATION_DEPTH')
    structures = '\n'.join((structure(schema, 'tag_schema_field'), structure(schema, 'tag_schema_definition'),
                            structure(source, 'tag_validation_frame'), structure(source, 'tag_validation')))
    return (('enums.inc', enums), ('structures.inc', structures), ('under_test.inc', code))


@pytest.mark.parametrize('case', ['elements', 'refused', 'deep'])
def test_skip_passes(case):
    status, output = run(build('validate_skip_passes', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [('checks', 'elements'), ('depth', 'deep'), ('data', 'elements')])
def test_negative(fault, case):
    status, output = run(build('validate_skip_passes', generated(fault)), case)
    assert status == CHECK_FAILED, output
