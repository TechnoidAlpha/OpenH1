/* KCP's round-trip estimate stays bounded (port/third_party/kcp/ikcp.c's
ikcp_update_ack): the time it is given comes from a timestamp the peer
echoes, so one far in the past (up to 2^31 ms) must not overflow the
estimator's signed sums. Ordinary times give upstream's estimate exactly,
a time past IKCP_RTO_MAX counts as IKCP_RTO_MAX, and the estimate comes
back down once the times are ordinary again. */
#include "harness.h"
#include "ikcp.h"

#include "under_test.inc"

static ikcpcb kcp;

static void fresh(void)
{
	memset(&kcp, 0, sizeof(kcp));
	kcp.interval = 100;
	kcp.rx_minrto = 100;
}

/* the estimator's state is what it can be: nothing negative, nothing past the maximum */
static void bounded(const char *after)
{
	CHECK(kcp.rx_srtt >= 0 && kcp.rx_srtt <= (IINT32)IKCP_RTO_MAX, "after %s, the smoothed time %d", after,
		(int)kcp.rx_srtt);
	CHECK(kcp.rx_rttval >= 0 && kcp.rx_rttval <= (IINT32)IKCP_RTO_MAX, "after %s, the variation %d", after,
		(int)kcp.rx_rttval);
	CHECK(kcp.rx_rto >= kcp.rx_minrto && kcp.rx_rto <= (IINT32)IKCP_RTO_MAX, "after %s, the timeout %d", after,
		(int)kcp.rx_rto);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	int index;

	fresh();
	CASE("ordinary")
	{
		ikcp_update_ack(&kcp, 50);
		CHECK(kcp.rx_srtt == 50 && kcp.rx_rttval == 25 && kcp.rx_rto == 150, "%d %d %d", (int)kcp.rx_srtt,
			(int)kcp.rx_rttval, (int)kcp.rx_rto);
		ikcp_update_ack(&kcp, 80);
		CHECK(kcp.rx_srtt == 53 && kcp.rx_rttval == 26 && kcp.rx_rto == 157, "%d %d %d", (int)kcp.rx_srtt,
			(int)kcp.rx_rttval, (int)kcp.rx_rto);
	}
	else CASE("zero")
	{
		ikcp_update_ack(&kcp, 0);
		bounded("0 ms");
		ikcp_update_ack(&kcp, 0);
		bounded("0 ms again");
	}
	else CASE("maximum")
	{
		ikcp_update_ack(&kcp, (IINT32)IKCP_RTO_MAX);
		bounded("the maximum");
		CHECK(kcp.rx_srtt == (IINT32)IKCP_RTO_MAX && kcp.rx_rto == (IINT32)IKCP_RTO_MAX, "%d %d", (int)kcp.rx_srtt,
			(int)kcp.rx_rto);
	}
	else CASE("far_past_first")
	{
		/* the first time ever given, from a timestamp 2^31 - 1 ms old */
		ikcp_update_ack(&kcp, 0x7fffffff);
		bounded("2^31 - 1 ms");
		CHECK(kcp.rx_srtt == (IINT32)IKCP_RTO_MAX, "the smoothed time %d, not the maximum", (int)kcp.rx_srtt);
		for (index = 0; index < 200; index++)
		{
			ikcp_update_ack(&kcp, 50);
			bounded("ordinary times after it");
		}
		CHECK(kcp.rx_srtt < 100 && kcp.rx_rto < 1000, "not back down: %d, %d", (int)kcp.rx_srtt, (int)kcp.rx_rto);
	}
	else CASE("far_past_later")
	{
		/* one far-past time among ordinary ones */
		for (index = 0; index < 10; index++)
			ikcp_update_ack(&kcp, 40 + index);
		ikcp_update_ack(&kcp, 0x7fffffff);
		bounded("2^31 - 1 ms");
		for (index = 0; index < 3; index++)
		{
			ikcp_update_ack(&kcp, 0x40000000);
			bounded("2^30 ms");
		}
		ikcp_update_ack(&kcp, 50);
		bounded("an ordinary time after them");
	}
	else CASE("wrapped")
	{
		/* the clock wrapped between the send and the acknowledgement:
		ikcp_input's _itimediff gives the time across the wrap */
		ikcp_update_ack(&kcp, (IINT32)(0x00000010u - 0xffffffe0u));
		CHECK(kcp.rx_srtt == 48, "the smoothed time %d across the wrap, not 48", (int)kcp.rx_srtt);
		bounded("a wrapped clock");
	}
	else
	{
		fprintf(stderr, "unknown case %s\n", case_name);
		return 2;
	}
	return 0;
}
