#!/bin/sh
# master.goath1.com's master server (README.md, "The master server"), on the
# server setup.sh made, as root, from this folder's copy on the server. Safe
# to run again: the admin token and the master's key are kept.
set -eu
cd "$(dirname "$0")"
export DEBIAN_FRONTEND=noninteractive

apt-get install -y -q caddy curl openssl python3

# the settings, with a new admin token the first time (root's alone)
install -d -m 700 /etc/opence-master
if [ ! -e /etc/opence-master/env ]; then
	umask 077
	sed "s/^MASTER_ADMIN_TOKEN=.*/MASTER_ADMIN_TOKEN=$(openssl rand -hex 32)/" master.env.example \
		> /etc/opence-master/env
	umask 022
	echo "setup-master: the admin token is in /etc/opence-master/env"
fi
chmod 600 /etc/opence-master/env

# the program, its signature checked, and its unit
[ -x /usr/local/bin/opence-master ] || sh ./install-master.sh v0.1.10801
install -m 755 opence-master-cli /usr/local/sbin/opence-master-cli
install -m 644 opence-master.service /etc/systemd/system/opence-master.service
systemctl daemon-reload
systemctl enable opence-master
systemctl restart opence-master

# HTTPS
install -m 644 Caddyfile /etc/caddy/Caddyfile
caddy validate --config /etc/caddy/Caddyfile --adapter caddyfile
systemctl enable caddy
systemctl reload-or-restart caddy

echo "setup-master: the master's public key, for masters.txt:"
sleep 2
journalctl -u opence-master --no-pager | grep -i "public key" | tail -1
