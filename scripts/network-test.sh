#!/bin/sh
# Builds hawser and runs a real network of four containers through it.
#
# Usage: scripts/network-test.sh
#
# Needs Docker, and WireGuard in the host's kernel (Linux 5.6 or later).
# Nothing on the host's own network is changed.
set -eu

cd "$(dirname "$0")/.."

compose() { docker compose -f docker-compose.test.yml "$@"; }
on() { machine="$1"; shift; compose exec -T "$machine" "$@"; }

trap 'compose down --volumes --timeout 2 >/dev/null 2>&1' EXIT

fail() {
  echo "FAILED: $1"
  for machine in coordinator north-a north-b south-a; do
    echo "--- $machine"
    on "$machine" sh -c 'hawser status 2>&1; tail -n 15 /var/log/hawser.log 2>/dev/null' || true
  done
  exit 1
}

# wait_until <seconds> <what it proves> <command...>
wait_until() {
  seconds="$1"; what="$2"; shift 2
  waited=0
  until "$@" >/dev/null 2>&1; do
    waited=$((waited + 1))
    [ "$waited" -lt "$seconds" ] || fail "$what"
    sleep 1
  done
  echo "ok: $what"
}

never() {
  what="$1"; shift
  if "$@" >/dev/null 2>&1; then fail "$what"; fi
  echo "ok: $what"
}

start() { compose exec -d "$1" sh -c 'hawser up >> /var/log/hawser.log 2>&1'; }
ping_from() { on "$1" ping -c 1 -W 2 "$2"; }
path_is() { on "$1" hawser status | grep -E "^$2 +[0-9.]+ +$3"; }

compose up --detach --build --quiet-pull

echo "== a coordinator and three machines"
on coordinator hawser init --network-name test --network 10.77.0.0/24 --name hub --endpoint coordinator:51820 >/dev/null
start coordinator
for machine in north-a north-b south-a; do
  invite="$(on coordinator hawser invite "$machine" 2>/dev/null)"
  on "$machine" hawser join "$invite" >/dev/null
  start "$machine"
done

echo "== every machine has an address and finds the others by name"
wait_until 30 "north-a reaches the coordinator by name" ping_from north-a hub
wait_until 30 "north-a reaches north-b by name" ping_from north-a north-b
wait_until 30 "north-b reaches north-a by its full name" ping_from north-b north-a.test
wait_until 30 "the coordinator reaches south-a by name" ping_from coordinator south-a
[ "$(on north-a sh -c 'ip -4 -o addr show hawser0' | awk '{print $4}')" = "10.77.0.2/24" ] || fail "north-a holds 10.77.0.2"
echo "ok: north-a holds the address its invite gave it"

echo "== machines that can reach each other connect directly"
wait_until 60 "north-a talks to north-b directly" path_is north-a north-b direct
wait_until 60 "north-b talks to north-a directly" path_is north-b north-a direct
wait_until 10 "and still reaches it" ping_from north-a north-b

echo "== machines that cannot reach each other go through the coordinator"
wait_until 30 "north-a reaches south-a" ping_from north-a south-a
wait_until 30 "south-a reaches north-b" ping_from south-a north-b
# Longer than it takes a direct path to be found, to show none is claimed.
sleep 20
path_is north-a south-a "through hub" >/dev/null || fail "north-a reaches south-a through the coordinator, not directly"
echo "ok: that path stays through the coordinator"

echo "== the same is there for a program to read"
json="$(on north-a hawser status --json)"
echo "$json" | grep -q '"name": "north-b", "address": "10.77.0.3", "is_coordinator": false, "state": "joined", "path": "direct"' || fail "status --json says north-b is direct"
echo "$json" | grep -q '"name": "south-a", "address": "10.77.0.4", "is_coordinator": false, "state": "joined", "path": "relayed"' || fail "status --json says south-a is relayed"
echo "$json" | grep -q '"name": "hub", "address": "10.77.0.1", "is_coordinator": true' || fail "status --json names the coordinator"
on coordinator hawser status --json | grep -q '"role": "coordinator".*"name": "south-a".*"endpoint": "[0-9.]*:[0-9]*"' || fail "the coordinator's status --json gives each machine's endpoint"
echo "ok: status --json agrees with the table"
wait_until 10 "and still works" ping_from south-a north-a

echo "== an invite works once"
never "a used invite is refused" on south-a sh -c "HAWSER_CONFIG_DIR=/tmp/second hawser join '$invite' --interface hawser1"

echo "== direct paths outlive the coordinator"
on coordinator pkill hawser
sleep 3
wait_until 10 "north-a still reaches north-b with the coordinator stopped" ping_from north-a north-b
start coordinator
wait_until 40 "south-a is back through the restarted coordinator" ping_from north-a south-a

echo "== a removed machine is cut off"
on coordinator hawser remove south-a >/dev/null
never "south-a is cut off the moment it is removed" ping_from south-a hub
never "and cannot reach the other machines either" ping_from south-a north-a
wait_until 30 "north-a no longer knows south-a by name" sh -c "! docker compose -f docker-compose.test.yml exec -T north-a grep -q south-a /etc/hosts"

echo "== leaving puts the machine back as it was"
on north-b pkill hawser
on north-b hawser down >/dev/null
never "north-b has no interface left" on north-b ip link show hawser0
never "north-b's hosts file has no hawser lines left" on north-b grep -q hawser /etc/hosts

echo
echo "network test passed"
