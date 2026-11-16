# KCP

A reliable ARQ protocol over unreliable datagrams, in C, by Lin Wei
(skywind3000), MIT licensed (see `LICENSE`).

Upstream: https://github.com/skywind3000/kcp, commit
b1a7a2101dcbb96017681a500d6b82bbe5a88766 (after release 2.1.1).
`ikcp.h` and `LICENSE` are copied unchanged. `ikcp.c` has one local change,
marked `OpenH1:`: `ikcp_update_ack` bounds the round-trip time it is given
to `0..IKCP_RTO_MAX` before its arithmetic. The time comes from a timestamp
the peer echoes back, and one far in the past (up to 2^31 ms) overflowed
the estimator's signed sums and left its state negative (#725); upstream
bounds only the timeout it computes.

Internet play carries the game's TCP connections over its UDP tunnel with
it (`port/linux/src/p2p.c`); it builds into the Linux, Windows and Android
platform layers.
