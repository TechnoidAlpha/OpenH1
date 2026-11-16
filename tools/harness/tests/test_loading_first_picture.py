"""The first loading picture is drawn once the guest no longer waits for the loading's record, or by the deferral."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, run, function, mutated, CHECK_FAILED

SOURCE = 'port/android/host/host_render_stream.c'


def generated(fault=None):
    source = read(SOURCE)
    state = re.search(r"^static struct\n\{\n\tconst struct host_render_backend \*backend;.*?^\} loading;", source,
                      re.M | re.S).group(0)
    code = state + '\n' + '\n'.join(function(source, name) for name in (
        'loading_stream_waits', 'host_render_stream_loading_service', 'loading_set'))
    if fault == 'drawn':
        code = mutated(code, '\t\tloading.drawn = loading.frame.started_ns;\n', '')
    if fault == 'waits':
        code = mutated(code, 'now - loading.drawn < LOADING_DEFERRED_MOST_NS', 'FALSE')
    deferral = re.search(r"#define LOADING_DEFERRED_MOST_NS (\d+)ull", source).group(1)
    enums = '#define LOADING_DEFERRED_MOST_NS %sull\n' % deferral
    return (('enums.inc', enums), ('under_test.inc', code))


@pytest.mark.parametrize('case', ['waits', 'deadline', 'idle'])
def test_first_picture(case):
    status, output = run(build('loading_first_picture', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [('drawn', 'waits'), ('waits', 'waits'), ('waits', 'deadline')])
def test_negative(fault, case):
    status, output = run(build('loading_first_picture', generated(fault)), case)
    assert status == CHECK_FAILED, output
