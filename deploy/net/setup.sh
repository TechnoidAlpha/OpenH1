#!/bin/sh
# net.goath1.com from a fresh Ubuntu 24.04, as root, from this folder's copy
# on the server (README.md, "Rebuilding the server from scratch"). Safe to
# run again: it puts the files back and restarts what they belong to.
set -eu
cd "$(dirname "$0")"
export DEBIAN_FRONTEND=noninteractive

apt-get update -q
apt-get install -y -q mosquitto mosquitto-clients nftables unattended-upgrades curl unzip openssl python3
apt-get -y -q upgrade

# Before this script (README.md): Tailscale, and ssh by key over it alone.
# Kept if present, else put in place:
# - ssh by key only
if [ ! -e /etc/ssh/sshd_config.d/10-goath1.conf ]; then
	install -m 644 sshd-goath1.conf /etc/ssh/sshd_config.d/10-goath1.conf
	sshd -t
	systemctl reload ssh
fi
# - security updates by themselves every day, rebooting at 09:30 UTC when
#   they need to
if [ ! -e /etc/apt/apt.conf.d/51goath1-reboot ]; then
	printf 'APT::Periodic::Update-Package-Lists "1";\nAPT::Periodic::Unattended-Upgrade "1";\n' \
		> /etc/apt/apt.conf.d/20auto-upgrades
	install -m 644 unattended-goath1.conf /etc/apt/apt.conf.d/51goath1-reboot
fi
systemctl enable --now unattended-upgrades.service
# - the time (systemd-timesyncd, Ubuntu's)
timedatectl set-ntp true

# 1 GB of swap, for the odd peak beside the 2 GB
if [ ! -e /swapfile ]; then
	fallocate -l 1G /swapfile
	chmod 600 /swapfile
	mkswap /swapfile
	echo '/swapfile none swap sw 0 0' >> /etc/fstab
fi
swapon /swapfile 2> /dev/null || true
echo 'vm.swappiness = 10' > /etc/sysctl.d/90-goath1.conf
sysctl -q -p /etc/sysctl.d/90-goath1.conf

# the broker
install -m 644 mosquitto.conf /etc/mosquitto/mosquitto.conf
install -o mosquitto -g mosquitto -m 600 goath1.acl /etc/mosquitto/goath1.acl
install -m 644 logrotate-mosquitto /etc/logrotate.d/mosquitto
install -d -o mosquitto -g mosquitto /var/lib/mosquitto /var/log/mosquitto
systemctl enable mosquitto
systemctl restart mosquitto

# the relay
id openh1-relay > /dev/null 2>&1 ||
	useradd --system --home-dir /var/lib/openh1-relay --shell /usr/sbin/nologin openh1-relay
install -d -o openh1-relay -g openh1-relay -m 750 /var/lib/openh1-relay
install -d -m 755 /opt/openh1-relay
install -m 755 update-relay.sh /usr/local/sbin/update-relay.sh
install -m 644 openh1-relay.service openh1-relay-update.service openh1-relay-update.timer /etc/systemd/system/
systemctl daemon-reload
systemctl enable openh1-relay.service openh1-relay-update.timer
systemctl start openh1-relay-update.timer
/usr/local/sbin/update-relay.sh
systemctl restart openh1-relay.service

# the firewall last, with a way back: the rules are removed in three minutes
# unless that is cancelled from a new ssh connection
install -d /etc/nftables.d
install -m 644 nftables-goath1.nft /etc/nftables.d/goath1.nft
install -m 644 nftables.conf /etc/nftables.conf
nft -c -f /etc/nftables.conf
systemctl stop goath1-nft-rollback.timer 2> /dev/null || true
systemd-run --quiet --unit goath1-nft-rollback --on-active=180 /usr/sbin/nft delete table inet goath1
systemctl enable nftables
nft -f /etc/nftables.conf
echo
echo "The firewall is on. From a NEW ssh connection (over Tailscale), within three minutes, run:"
echo "    systemctl stop goath1-nft-rollback.timer"
echo "(else its rules are removed, until the next boot)."
