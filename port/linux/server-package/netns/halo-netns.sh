#!/bin/bash
# halo-netns.sh <n> [port]: a network namespace of its own, hs<n>, for the
# dedicated server number <n> (systemd/halo-netns@.service runs it as root at
# boot; halo-server-ns@.service runs the server in it as an unprivileged user).
#
# Each server needs the game's network ports, 5150 and 5151. A build with
# sv_game_port gives each server two others (halo-server@.service needs no
# namespace); one without it needs a namespace per server. The namespace
# has the address 10.200.<n>.2 behind a veth pair, its traffic leaves
# through this machine's public interface (NAT), and the UDP port <port>
# (default 2301 + n: 2302 for server 1, 2303 for server 2, ...) on this
# machine's public address is forwarded to it, and only that port. Its
# sv_port must be the same port.
#
# Needs iproute2 and iptables, as root. Idempotent: a second run changes
# nothing.
set -e
usage() { echo "usage: $0 <n 1-250> [port 1024-65535]" >&2; exit 2; }
n=$1
case "$n" in ''|*[!0-9]*) usage ;; esac
[ "$n" -ge 1 ] && [ "$n" -le 250 ] || usage
port=${2:-$((2301 + n))}
case "$port" in ''|*[!0-9]*) usage ;; esac
[ "$port" -ge 1024 ] && [ "$port" -le 65535 ] || usage
ns=hs$n; h=10.200.$n.1; g=10.200.$n.2
ip netns list | grep -qw $ns || ip netns add $ns
ip link show vh$n >/dev/null 2>&1 || { ip link add vh$n type veth peer name vg$n; ip link set vg$n netns $ns; }
ip addr replace $h/30 dev vh$n; ip link set vh$n up
ip netns exec $ns ip addr replace $g/30 dev vg$n
ip netns exec $ns ip link set vg$n up
ip netns exec $ns ip link set lo up
ip netns exec $ns ip route replace default via $h
# (the namespace's name servers: systemd-resolved's upstream ones, else public ones)
mkdir -p /etc/netns/$ns; grep -E "^nameserver" /run/systemd/resolve/resolv.conf > /etc/netns/$ns/resolv.conf 2>/dev/null; [ -s /etc/netns/$ns/resolv.conf ] || printf "nameserver 1.1.1.1\nnameserver 8.8.8.8\n" > /etc/netns/$ns/resolv.conf
sysctl -qw net.ipv4.ip_forward=1
pub=$(ip -4 route get 1.1.1.1 | awk '{for(i=1;i<=NF;i++) if($i=="dev") print $(i+1)}')
iptables -t nat -C POSTROUTING -s $g/32 -o $pub -j MASQUERADE 2>/dev/null || iptables -t nat -A POSTROUTING -s $g/32 -o $pub -j MASQUERADE
iptables -t nat -C PREROUTING -i $pub -p udp --dport $port -j DNAT --to-destination $g:$port 2>/dev/null || iptables -t nat -A PREROUTING -i $pub -p udp --dport $port -j DNAT --to-destination $g:$port
iptables -C FORWARD -d $g/32 -p udp --dport $port -j ACCEPT 2>/dev/null || iptables -I FORWARD -d $g/32 -p udp --dport $port -j ACCEPT
iptables -C FORWARD -s $g/32 -j ACCEPT 2>/dev/null || iptables -I FORWARD -s $g/32 -j ACCEPT
iptables -C FORWARD -d $g/32 -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT 2>/dev/null || iptables -I FORWARD -d $g/32 -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
