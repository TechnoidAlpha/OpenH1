"""KCP's round-trip estimate stays bounded (port/third_party/kcp/ikcp.c's ikcp_update_ack, #725)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

SOURCE = "port/third_party/kcp/ikcp.c"
CASES = ["ordinary", "zero", "maximum", "far_past_first", "far_past_later", "wrapped"]
BOUND = "\tif (rtt > (IINT32)IKCP_RTO_MAX) rtt = (IINT32)IKCP_RTO_MAX;\n"


def generated(fault=None):
    source = read(SOURCE)
    code = "\n".join([f"static const IUINT32 IKCP_RTO_MAX = {constant(source, 'IKCP_RTO_MAX')};"] +
                     [function(source, name) for name in ("_imin_", "_imax_", "_ibound_", "ikcp_update_ack")])
    if fault:
        code = mutated(code, *fault)
    return (("ikcp.h", read("port/third_party/kcp/ikcp.h")), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_kcp_rtt_bound(case):
    status, output = run(build("kcp_rtt_bound", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("case", ["far_past_first", "far_past_later"])
def test_negative_control(case):
    # the time unbounded, as upstream's: the estimator's sums overflow
    status, output = run(build("kcp_rtt_bound", generated((BOUND, ""))), case)
    assert status == CHECK_FAILED, output
