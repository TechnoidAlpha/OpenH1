# net.goath1.com: the project's broker, relay and master server

One small server carries what internet play needs from the project:

- **an MQTT broker** on TCP 1883 (Mosquitto 2.0), the first in
  `port/assets/network/brokers.txt` and the game's `DEFAULT_BROKERS`
  (`port/linux/src/p2p_signal.c`): invites' signalling, parties, friends and
  the server browser's listings. Everything the game sends through it is
  sealed end to end, to topics that are hashes, so the broker learns
  nothing and needs no secrets;
- **a relay** on UDP 2302 (`halo --relay`, `docs/dedicated_server.md`,
  "Relays"), in `port/assets/network/relays.txt` and the game's
  `DEFAULT_RELAYS` (`port/linux/src/master_list.c`), for two machines whose
  NATs keep them apart;
- **a master server** at `https://master.goath1.com` (opence-master, behind
  Caddy on TCP 80 and 443), first in `port/assets/network/masters.txt` and
  `master_list_read_defaults` (`port/linux/src/master_list.c`): it lists
  dedicated servers of approved operators ("The master server", below).

The other brokers in the list (OpenCE's `opence.milenko.org`, which OpenCE's
players use, and a public one) carry the same messages, so the game goes on
if this server is down.

The server: a Hetzner Cloud CPX11 (2 vCPU, 2 GB) in Ashburn, Ubuntu 24.04,
`goath1-net-ash`; `net.goath1.com` has its A and AAAA records (DNS only, not
proxied), and so has `master.goath1.com`. Its Hetzner Cloud firewall
(`goath1-net`) lets in only TCP 1883, UDP 2302, TCP 80 and 443, and ICMP
from the internet.

**ssh is over Tailscale only**: the server is on the maintainer's tailnet as
`goath1-net-ash` (`ssh root@goath1-net-ash`), with key-only logins; port 22
is closed to the internet by the Hetzner firewall and dropped by the
server's own (`nftables-goath1.nft`), which accepts everything on
`tailscale0` and Tailscale's UDP 41641. If Tailscale is down, use the Hetzner
Cloud console (`hcloud server request-console goath1-net-ash`, or the web
console: a root password, `hcloud server reset-password`, then
`tailscale up` or a look at `journalctl -u tailscaled`), or boot the rescue
system (`hcloud server enable-rescue goath1-net-ash --ssh-key <key>`, then
`hcloud server reboot`) and mount the disk. Opening 22 in the Hetzner
firewall alone is not enough: the server's nftables drops it too (`nft
delete table inet goath1` from the console lets it in until the next
boot).

| File | Installed as | What it is |
| --- | --- | --- |
| `mosquitto.conf` | `/etc/mosquitto/mosquitto.conf` | The broker: anonymous, MQTT 5 and 3.1.1, the limits, retained listings kept through restarts |
| `goath1.acl` | `/etc/mosquitto/goath1.acl` | The game's topics, the only ones carried |
| `logrotate-mosquitto` | `/etc/logrotate.d/mosquitto` | The broker's log, daily, a week kept |
| `nftables-goath1.nft` | `/etc/nftables.d/goath1.nft` | The firewall: the four ports, new connections limited per address |
| `openh1-relay.service` | `/etc/systemd/system/` | `halo --relay`, as the user `openh1-relay` |
| `update-relay.sh` | `/usr/local/sbin/update-relay.sh` | The latest release installed as the relay, its signature checked |
| `openh1-relay-update.service`, `.timer` | `/etc/systemd/system/` | `update-relay.sh` weekly |
| `nftables.conf` | `/etc/nftables.conf` | Loads `/etc/nftables.d`, without flushing Tailscale's tables |
| `sshd-goath1.conf` | `/etc/ssh/sshd_config.d/10-goath1.conf` | ssh by key only. Already on the server: a copy of it, which `setup.sh` installs only on a new server that has none |
| `unattended-goath1.conf` | `/etc/apt/apt.conf.d/51goath1-reboot` | Security updates daily, rebooting at 09:30 UTC when they need to. Already on the server: `setup.sh` installs it only where it is missing |
| `opence-master.service` | `/etc/systemd/system/` | The master server, as a dynamic user, on 127.0.0.1:8090 |
| `master.env.example` | `/etc/opence-master/env` | Its settings; the real file (root's, mode 0600) holds the admin token |
| `Caddyfile` | `/etc/caddy/Caddyfile` | HTTPS for `master.goath1.com`; the admin and HTML pages for the tailnet only |
| `install-master.sh` | | The fork's master release installed, its signature checked, the old binary kept |
| `opence-master-cli` | `/usr/local/sbin/opence-master-cli` | The master's administration commands, as its own user |
| `setup.sh` | | The broker and relay on a fresh Ubuntu 24.04, as root |
| `setup-master.sh` | | The master server, after `setup.sh` |

## The broker

The game needs of a broker: retained messages, wildcard subscriptions
(`hceu/3/lobby/s/+`), wills (each connection's clears its slot's listing
when the connection dies), MQTT 5's message expiry (a listing lasts 90
seconds), QoS 1, and anonymous clients. Mosquitto has all of them.

- **Topics** (`goath1.acl`): `hceu/3/+` (signalling and parties),
  `hceu/3/f/+` (friends), `hceu/3/lobby/s/+` (the listings) and
  `hceu/3/lobby/q` (the browsers' queries). Nothing else can be published
  or subscribed to, `#` and `$SYS` included.
- **Sizes**: the largest packet the game sends is about 620 bytes (a sealed
  512-byte message, its 64-byte topic and the headers); `max_packet_size`
  is 2048. Each client may have 20 messages in flight and 100 (64 KB)
  queued; the broker uses at most 768 MB.
- **Persistence**: the retained listings are saved every minute and on
  shutdown, with their expiry, which the broker honours after a restart.
- **Connections**: 4000 at most; per source address, 30 new a minute (60 at
  once) and 64 open (`nftables-goath1.nft`). Players behind one carrier's
  NAT share an address; each game holds one connection.

Check it from anywhere with Mosquitto's clients:

```sh
mosquitto_sub -h net.goath1.com -V mqttv5 -t 'hceu/3/lobby/s/+' -v -W 5   # the listings now (sealed)
mosquitto_sub -h net.goath1.com -t '#' -W 3                                # refused by the ACL: nothing
```

On the server: `systemctl status mosquitto`, `/var/log/mosquitto/mosquitto.log`.

## The relay

`halo --relay` from OpenH1's Linux release (`OpenH1-Linux.zip`, CI's release
build), in `/opt/openh1-relay/<tag>`, with `/opt/openh1-relay/current`
pointing at the one in use. It runs as `openh1-relay` (no shell, no home but
`/var/lib/openh1-relay`), restarts on failure, and logs its counts each
minute they change: `journalctl -u openh1-relay`. Its limits are the
defaults (`server.relay_pairs` 64 pairs, `server.relay_upload` 8 MB/s): set
others in `/var/lib/openh1-relay/config.toml`.

**Updates.** `openh1-relay-update.timer` runs `update-relay.sh` on Tuesdays
at 4 am New York time (within 10 minutes), before the security updates'
reboot at 09:30 UTC: it downloads the latest release's `SHA256SUMS`, its
signature and `OpenH1-Linux.zip`, checks the signature with the release key
(`port/assets/release-key.pub`, as the game's self-updater does) and the
zip's sum, installs it beside the others and restarts the relay if the
build changed (pairs being relayed then bind again within seconds). The
three newest builds are kept. By hand:

```sh
ssh root@goath1-net-ash update-relay.sh            # the latest, if it is new
ssh root@goath1-net-ash update-relay.sh --force    # again
# back to an older build:
ssh root@goath1-net-ash 'ln -sfn /opt/openh1-relay/build-2077 /opt/openh1-relay/current && systemctl restart openh1-relay'
```

The relay's protocol is versioned on its own (`docs/dedicated_server.md`,
"Relays"), so a relay of an older release keeps serving newer games; an
update is needed only when that version changes.

## The master server

[opence-master](https://github.com/OpenCommunityEdition/opence-master-server)
(AGPL-3.0; protocol 1, `docs/master_protocol.md`), as our fork releases it
([OpenH1/opence-master-server](https://github.com/OpenH1/opence-master-server),
`v0.1.10801`): upstream's v0.1.8 with `MASTER_GAME_NAME` (`GOAT H1`: the
game's name in the pages', the bot's and the mails' text, in place of
"OpenCE"), the footer's source link at the running version's tag (as the
AGPL asks; `MASTER_PROJECT_URL` would replace it), and its updates from the
fork's releases, signed by the fork's master release key. Nothing on the
wire or in the database differs from upstream's. The fork's versions are
`v<major>.<minor>.1<upstream's patch, two digits><our revision, two
digits>`: release versions to the master, after upstream's v0.1.x and never
one of its tags (`v0.1.10801` is upstream's v0.1.8, our first). The static binary
is in `/usr/local/bin/opence-master`, run by `opence-master.service` (the
upstream unit) on 127.0.0.1:8090. Caddy (Ubuntu's package) serves it at
`https://master.goath1.com` with a Let's Encrypt certificate that it gets and
renews itself (it needs TCP 80 and 443). Its state is
`/var/lib/opence-master` (in fact `/var/lib/private/opence-master`, the
dynamic user's): `master.db` and `master.key`, the list-signing key, made on
the server at the first start (mode 0600) and backed up nowhere public. Its
public half is in `masters.txt`:

    https://master.goath1.com ed25519:120f7635fe754e327cae95480d847aa57b8b23a9bb362b3d6080bfdd21399684

A new key would refuse every list to every game that pins this one: back up
`master.key` and `master.db` together, privately (`sqlite3 master.db
".backup copy.db"`), if at all.

- **What the public reaches** (`Caddyfile`): the protocol's endpoints alone:
  `/v1/servers` (the list, a server, the publishes), `/v1/netlist` (which
  dedicated servers' `server.vpn_policy` checks players against),
  `/v1/pass`, `/v1/me`, `/v1/keys`, `/v1/master`, `/v1/publishers`,
  `/v1/applications` (which answers 410) and `/healthz`. Everything else, the
  admin pages, `/metrics`, the list page, `/link` and `/apply`, answers 404
  but to the tailnet's addresses (100.64.0.0/10, fd7a:115c:a1e0::/48).
- **The admin pages**, over Tailscale: point `master.goath1.com` at the
  server's Tailscale address, so that the request comes in on `tailscale0`
  with the real certificate: a line `100.101.9.65 master.goath1.com` in
  `/etc/hosts` (remove it after), then `https://master.goath1.com/admin/`;
  or `curl --resolve master.goath1.com:443:100.101.9.65 -H "Authorization:
  Bearer <token>" ...`. The token is `MASTER_ADMIN_TOKEN` in
  `/etc/opence-master/env` on the server (root's alone, mode 0600; never in
  the repository): `ssh root@goath1-net-ash grep TOKEN /etc/opence-master/env`.
- **No Discord bot**: an operator makes a publisher key on their server
  (`halo --server --keygen`), and sends us its public key and a proof of it:
  we make the nonce (`ssh root@goath1-net-ash opence-master-cli challenge`),
  they sign it (`halo --server --sign <nonce>`), and we take the
  application and approve it:

  ```sh
  ssh root@goath1-net-ash opence-master-cli add-application --display-name "<name>" \
      --public-key ed25519:<hex> --servers <n> --region na-east --proof-nonce <nonce> --proof-signature <hex>
  ssh root@goath1-net-ash opence-master-cli approve <application id> --quota 4
  ```

  or approve it on the admin pages (Applications). A server behind a NAT that
  the probe cannot reach is listed once an admin marks it **trusted**
  (Servers). The other commands: `opence-master-cli list-applications`,
  `list-publishers`, `suspend`, `resume`, `revoke`, `quota`.
- **No peers**: approvals are not shared with OpenCE's master
  (`MASTER_PEERS`); that is for the maintainers to decide.
- **Updates, by hand**: the master checks the fork's releases
  (`MASTER_UPDATES=notify`; it never installs one by itself) and the admin
  pages say when one is out. An upstream release is merged into the fork and
  tagged; the fork's release workflow builds it, signs its `SHA256SUMS` with
  the master release key and publishes it. Read its notes, then `ssh
  root@goath1-net-ash sh /root/goath1-net/install-master.sh v<x.y.z>`: it
  downloads the fork's release binary, `SHA256SUMS` and its signature,
  checks the signature with the master release key and the binary's sum,
  keeps the binary it replaces (`/usr/local/bin/opence-master.bak-<its
  version>`), installs the new one and restarts the master, and puts the old
  one back if the new one does not answer `/healthz`. The master backs its
  database up itself when a newer version first opens it
  (`master.db.before-<version>`). To go back: the older tag the same way,
  with that backup copied over `master.db` while the master is stopped.
- **The master release key** signs the fork's releases alone (not the
  game's, `port/assets/release-key.pub`). Its public half is
  `internal/update/key.go` in the fork, which the master's updater reads,
  and `RELEASE_KEY` in `install-master.sh`:
  `3607ccfaa3ebb0c1cbbfdb8b1578522cb281b250799f9f28b529c2bb1e6e16ca`. Its
  private half (the seed) is the maintainer's, held privately, and the
  fork's `RELEASE_SIGNING_KEY` Actions secret; never in a repository.

Check it: `curl https://master.goath1.com/healthz`, `curl
https://master.goath1.com/v1/master` (its name and key id), `journalctl -u
opence-master -u caddy`.

## Rebuilding the server from scratch

1. Create the server (Ubuntu 24.04, the maintainers' ssh key), its firewall
   and its DNS (port 22 is open only until Tailscale is up):

   ```sh
   hcloud context use goath1
   hcloud server create --name goath1-net-ash --type cpx11 --location ash --image ubuntu-24.04 --ssh-key <key>
   hcloud firewall create --name goath1-net
   hcloud firewall add-rule goath1-net --direction in --protocol tcp --port 22 --source-ips 0.0.0.0/0 --source-ips ::/0 --description ssh-until-tailscale
   hcloud firewall add-rule goath1-net --direction in --protocol tcp --port 1883 --source-ips 0.0.0.0/0 --source-ips ::/0 --description mqtt
   hcloud firewall add-rule goath1-net --direction in --protocol udp --port 2302 --source-ips 0.0.0.0/0 --source-ips ::/0 --description relay
   hcloud firewall add-rule goath1-net --direction in --protocol tcp --port 80 --source-ips 0.0.0.0/0 --source-ips ::/0 --description https-80
   hcloud firewall add-rule goath1-net --direction in --protocol tcp --port 443 --source-ips 0.0.0.0/0 --source-ips ::/0 --description https-443
   hcloud firewall add-rule goath1-net --direction in --protocol icmp --source-ips 0.0.0.0/0 --source-ips ::/0 --description ping
   hcloud firewall apply-to-resource goath1-net --type server --server goath1-net-ash
   ```

   and point `net.goath1.com`'s A and AAAA records at its addresses (DNS
   only).
2. Put it on the tailnet (`curl -fsSL https://tailscale.com/install.sh | sh`,
   `tailscale up --hostname goath1-net-ash`), check `ssh
   root@goath1-net-ash` works, then close 22 to the internet: `hcloud
   firewall delete-rule goath1-net --direction in --protocol tcp --port 22
   --source-ips 0.0.0.0/0 --source-ips ::/0 --description
   ssh-until-tailscale`.
3. Copy this folder over and run the setup:

   ```sh
   scp -r deploy/net root@goath1-net-ash:/root/goath1-net
   ssh root@goath1-net-ash sh /root/goath1-net/setup.sh
   ```

   It installs the packages (Mosquitto, nftables, unattended-upgrades), ssh's
   and the updates' settings where they are missing, a 1 GB swap file, the
   files above, and the latest
   release as the relay, and starts everything. It can be run again after
   changing a file here: it puts the files back and restarts what they
   belong to. The nftables rules are loaded last, with a three-minute
   rollback: from a new ssh connection over Tailscale, run `systemctl stop
   goath1-nft-rollback.timer` to keep them. A mistake in them cannot lock
   the server out.
4. Check: `ssh root@goath1-net-ash 'systemctl is-active mosquitto
   openh1-relay nftables; journalctl -u openh1-relay -n 3'`.
5. The master server: `master.goath1.com`'s A and AAAA records, then `ssh
   root@goath1-net-ash sh /root/goath1-net/setup-master.sh`. It makes the
   admin token (in `/etc/opence-master/env`) and, at the master's first
   start, a new key: put its public half (the script prints it) in
   `masters.txt` and `master_list_read_defaults`, or restore the old
   `master.key` into `/var/lib/private/opence-master/` before the first start
   to keep the one the games pin. It installs the fork's release
   (`install-master.sh`).

The server holds no secrets but its ssh host keys; nothing here needs any.
