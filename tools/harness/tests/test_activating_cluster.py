"""The objects' activating cluster across a structure bsp switch: the co-op
host's let go, a stale index none, a script's kept (objects.c)."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run


def generated(fault=None):
    source = read("source/objects/objects.c")
    flag = re.search(r"static boolean objects_port_activation_from_host = FALSE;", source).group(0)
    code = flag + "\n" + "\n".join(function(source, name) for name in (
        "objects_port_set_activating_cluster", "objects_port_structure_bsp_switched",
        "objects_get_activating_cluster_index"))
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", ["host-switch", "stale-index", "script-kept"])
def test_activating_cluster(case):
    status, output = run(build("activating_cluster", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [
    (("\t\tobject_globals->pvs_activation_type = _pvs_activation_normal;\n\t}\n\tobjects_port_activation_from_host = FALSE;",
      "\t}\n\tobjects_port_activation_from_host = FALSE;"), "host-switch"),
    (("object_globals->pvs_activation.cluster_index >= global_structure_bsp_get()->clusters.count",
      "object_globals->pvs_activation.cluster_index >= 0x7fff"), "stale-index"),
    (("\tif (objects_port_activation_from_host &&\n", "\tif (TRUE &&\n"), "script-kept"),
])
def test_negative_control(fault, case):
    status, output = run(build("activating_cluster", generated(fault)), case)
    assert status == CHECK_FAILED, output
