"""The determinism log's hash of the objects (determinism_log.c) leaves out the indices of an object's attachments that
are datums of the frames' allocations (a light, a looping sound, a contrail, a particle system: the frames free their
slots, so the index a new one takes follows the frames), and compares an effect's, the simulation's. The offsets
tools/determinism_compare.py zeroes in a dump are the object's own."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure
from harness import skip_without_32_bit_abi

CASES = ['frames', 'past-the-count', 'effect', 'simulation', 'offsets']


def tool_constant(name):
    return int(re.search(r'^' + name + r' = (0x[0-9a-f]+|\d+)$', read('tools/determinism_compare.py'), re.M).group(1), 0)


def generated(fault=None):
    objects = read('source/objects/objects.h')
    definitions = read('source/objects/object_definitions.h')
    config = '\n'.join([
        enum_with(definitions, 'MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT'),
        enum_with(objects, '_object_attachment_type_light'),
        enum_with(objects, '_object_change_color_a'),
        structure(read('source/memory/memory_pool.h'), 'memory_pool_block'),
        structure(read('source/memory/memory_pool.h'), 'memory_pool'),
        structure(objects, 'location'),
        structure(objects, 'animation_state'),
        structure(objects, 'object_animation_state'),
        structure(objects, 'object_header_block_reference'),
        structure(objects, '_object_datum'),
        structure(objects, 'object_datum'),
    ]) + '\n'
    # (determinism_compare.py's offsets in a block, to check against the object's)
    for name in ('OBJECT_MAGIC_NUMBER', 'OBJECT_ATTACHMENT_TYPES', 'OBJECT_ATTACHMENT_INDICES',
                 'OBJECT_CACHED_RENDER_STATE_INDEX', 'MAXIMUM_ATTACHMENTS_PER_OBJECT'):
        config += f'#define TOOL_{name} {tool_constant(name)}\n'
    frame_types = re.search(r'^FRAME_ATTACHMENT_TYPES = \(([\d, ]+)\)$', read('tools/determinism_compare.py'),
                            re.M).group(1)
    config += f'static int const tool_frame_attachment_types[] = {{ {frame_types} }};\n'
    log = read('port/linux/game/determinism_log.c')
    code = '\n'.join([
        function(log, 'determinism_hash'),
        function(log, 'determinism_frames_attachment'),
        function(log, 'determinism_hash_objects'),
    ])
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


# what only the game's 32-bit ABI shows (harness.NATIVE_64_BIT)
ABI_DEPENDENT = {
    'offsets': "the object's fields are at the game's offsets, with its 32-bit pointers and longs",
}


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    skip_without_32_bit_abi(ABI_DEPENDENT, case)
    status, output = run(build('frames_attachments', generated()), case)
    assert status == 0, output


FAULTS = [
    # (the frames' indices hashed, as before: a contrail's slot parts two runs)
    ('\treturn type == _object_attachment_type_light ||',
     '\tif (type != NONE)\n\t\treturn FALSE;\n\treturn type == _object_attachment_type_light ||', 'frames'),
    # (an effect's left out too: the simulation's own index no longer compared)
    ('type == _object_attachment_type_particle_system;',
     'type == _object_attachment_type_particle_system || type == _object_attachment_type_effect;', 'effect'),
    # (the rest of the object left out after the attachments)
    ('start = skipped[index] + sizeof(long);', 'start = skipped[index] + 2 * sizeof(long);', 'simulation'),
    # (every slot's taken, the definition's count or not)
    ('for (index = 0; index < attachment_count; index++)',
     'for (index = 0; index < MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT + 0 * attachment_count; index++)',
     'past-the-count'),
]


@pytest.mark.parametrize('fault', FAULTS, ids=['frames-hashed', 'effect-left-out', 'too-much-left-out', 'past-the-count'])
def test_negative_control(fault):
    skip_without_32_bit_abi(ABI_DEPENDENT, fault[2])
    status, output = run(build('frames_attachments', generated(fault[:2])), fault[2])
    assert status == CHECK_FAILED, output
