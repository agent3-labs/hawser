# Hawser

A private network between your own machines. Each machine gets a fixed
address, and every machine knows how to find the others by address and by
name, wherever they are.

One machine with a public address is the **coordinator**. It hands out the
addresses and tells each machine where the rest are. Every other machine runs
an **agent**.

Hawser does not carry your traffic itself. WireGuard, in the Linux kernel,
does that: it is fast, and it is the encryption. Hawser is the part WireGuard
leaves to you: who is in the network, which address each machine has, and how
they reach each other. There is no account and no third party's server.

## What you need

- Linux 5.6 or later on every machine (WireGuard is part of the kernel).
- One machine that the others can dial: a public address and one open UDP port.
- Root on each machine.

## Install from a download

The [releases page](https://github.com/agent3-labs/hawser/releases) has, for
each release, a package and a tarball for x86-64, arm64 and armv7 (Raspberry
Pi). Nothing else is needed: the binary is static.

| File | For | Install with |
|---|---|---|
| `hawser_<version>_<arch>.deb` | Debian, Ubuntu, Raspberry Pi OS | `sudo apt install ./hawser_*.deb` |
| `hawser-<version>-1.<arch>.rpm` | Fedora, RHEL, openSUSE | `sudo dnf install ./hawser-*.rpm` |
| `hawser_<version>_<arch>.apk` | Alpine | `sudo apk add --allow-untrusted ./hawser_*.apk` |
| `hawser-v<version>-linux-<arch>.tar.gz` | Any other distribution | unpack, then `sudo ./install.sh ./hawser` |

A package puts `hawser` in `/usr/bin` with its service; the tarball's
`install.sh` puts it in `/usr/local/bin`. Either way nothing is set up and
nothing is started: carry on at "Create a network".

## Build

```sh
docker build --target binary --output dist .
```

writes `dist/hawser`, one static file with no dependencies. The same file runs
on any Linux distribution. The build runs the unit tests and fails if one does.

Without Docker, on a machine with a C++20 compiler and CMake:

```sh
cmake -S . -B build && cmake --build build -j && ./build/hawser_tests
```

## Install from a checkout

On each machine, as root, from a checkout of this repository:

```sh
scripts/install.sh dist/hawser
```

It puts the binary in `/usr/local/bin` and adds a `hawser` service. It sets up
nothing and starts nothing.

## Create a network

On the coordinator:

```sh
hawser init --network-name home --network 10.77.0.0/24 \
            --name vps --endpoint vpn.example.com:51820
systemctl enable --now hawser
```

| Option | Meaning |
|---|---|
| `--network-name` | A short name for the network. Machines are reachable as `<machine>.<network-name>` |
| `--network` | The range addresses are handed out from. Pick one nothing else on your machines uses |
| `--name` | This machine's name in the network |
| `--endpoint` | The `host:port` other machines dial to reach this one. The UDP port must be open from outside |
| `--address` | This machine's address. Default: the first in the range |
| `--listen-port` | The UDP port to listen on. Default: the endpoint's port |
| `--api-port` | The TCP port agents call, inside the network only. Default: 51821 |
| `--mtu` | The largest packet the network carries. Default: 1420, which fits a standard 1500-byte path |
| `--interface` | The interface's name. Default: `hawser0` |

Every required option must be given. Nothing is guessed.

## Add a machine

On the coordinator:

```sh
hawser invite workstation
```

prints one line. On the new machine:

```sh
hawser join <that line>
systemctl enable --now hawser
```

The machine now has its address and can reach every other machine.

The invite carries a one-time key, so send it the way you would send a
password. It works once, and for 24 hours unless `--valid-hours` says
otherwise. While joining, the machine makes a key of its own and hands the
public half to the coordinator; from then on the invite's key is refused.
`--address` on `invite` picks the machine's address instead of taking the
next free one.

## Day to day

| Command | Where | What it does |
|---|---|---|
| `hawser status` | any machine | Lists the machines, their addresses, and how each is reached. `--json` prints the same as one line of JSON, for a program |
| `hawser invite <name>` | coordinator | Prints the line that lets a new machine join |
| `hawser remove <name>` | coordinator | Cuts a machine off at once; the others forget it within seconds |
| `hawser up` | any machine | Runs the network. This is what the service runs |
| `hawser down` | any machine | Removes the interface and hawser's lines in the hosts file |

`hawser status` on an agent:

```
Network home, 10.77.0.0/24. This machine: workstation, 10.77.0.2.

NAME         ADDRESS    PATH                  LAST HEARD
vps          10.77.0.1  direct (coordinator)  12s ago
workstation  10.77.0.2  this machine          -
laptop       10.77.0.3  direct                40s ago
build-box    10.77.0.4  through vps           -
```

## How machines find each other

**By address.** The coordinator gives each machine one address for good.

**By name.** Each machine keeps a block in `/etc/hosts` listing every machine
as `<machine>.<network-name>`, so `ssh laptop.home` works everywhere and always
goes through the network. The bare name `laptop` is left to the local network.
Only that block is ever touched. Set `manages_hosts=no` in
`/etc/hawser/hawser.conf` to leave the file alone.

**By the best path available.** Every machine calls the coordinator, so the
coordinator sees the public address and port each one is calling from, and
passes those on. Then, for each pair of machines:

1. Both sides start trying to reach each other directly, in the background.
   Meanwhile their traffic goes through the coordinator, so the pair can talk
   from the first second.
2. Once the direct path has actually carried something, traffic moves to it.
3. If the direct path goes quiet for 75 seconds, traffic moves back to the
   coordinator, and the background attempts carry on.

Traffic is never sent down a path that has not been shown to work. Two
machines that cannot reach each other at all, for example each behind its own
strict NAT, simply stay on the coordinator.

If the coordinator stops, machines connected directly keep talking. Machines
that relied on it wait until it is back. Restarting the `hawser` service does
not cut anything: the interface stays up.

## Why the pieces can trust each other

- An agent talks to the coordinator **through the tunnel**. WireGuard has
  already encrypted the conversation, and it only accepts a packet from an
  address if it was sealed with that address's key. So the coordinator knows
  which machine is calling by its address, and no passwords cross the wire.
- The coordinator's API listens only on its address inside the network. It is
  not reachable from outside.
- A machine's private key is made on that machine and never leaves it.

## Files

| File | On | Holds |
|---|---|---|
| `/etc/hawser/hawser.conf` | every machine | This machine's place in the network. Readable; edit and restart to change the MTU or the hosts setting |
| `/etc/hawser/private.key` | every machine | The machine's WireGuard key. Readable by root only |
| `/var/lib/hawser/machines` | coordinator | Every machine: name, address, public key. Back this file up; it is the network |

`HAWSER_CONFIG_DIR` and `HAWSER_STATE_DIR` move the two folders.

## Firewall

- The coordinator's UDP port (51820 unless you chose another) must accept
  packets from anywhere.
- Other machines need no open port. They call out.
- A coordinator passes packets between machines that cannot reach each other,
  so it turns on `net.ipv4.ip_forward`. If its firewall drops forwarded
  packets, which Docker's does by default, allow the ones that stay on the
  hawser interface:

  ```sh
  iptables -I DOCKER-USER -i hawser0 -o hawser0 -j ACCEPT
  ```

  and make that rule survive a reboot the way your system keeps firewall
  rules. Machines that reach each other directly do not need it.

## Limits of this version

- **IPv4 only**, inside the network and for endpoints.
- **One coordinator.** It is not a single point of failure for machines
  connected directly, but nobody can join and nothing can be relayed while it
  is down.
- **Two machines behind the same router** are given each other's public
  endpoint, not the local one. Unless the router loops such packets back, they
  talk through the coordinator.
- **No access rules.** Every machine in the network can reach every other.
- **No DNS server.** Names come from the hosts file.

## Testing

```sh
./build/hawser_tests          # the unit tests
scripts/network-test.sh       # a real network of four containers
```

The network test builds hawser, starts a coordinator and three machines in
containers, and checks the things this README promises: addresses, names,
direct paths, relaying, single-use invites, a coordinator restart, removal,
and a clean exit. One machine is put where only the coordinator can reach it,
so the relayed path is exercised too. The containers have their own network
namespaces; the host's network is not touched.

## Branches

`dev` takes pushes. `prod` moves only when `dev` is merged into it, and each
commit on `prod` publishes the binary to this project's package registry under
the version in the `VERSION` file.

## Releasing

`make package` builds the downloads for every architecture into `release/`
(see `scripts/package.sh` for what it needs), and a GitHub release under the
tag `v<version>` carries them:

```sh
make package
gh release create "v$(cat VERSION)" --repo agent3-labs/hawser release/*
```

## Third-party code

`third_party/wireguard` holds WireGuard's own small library for configuring
the kernel, unchanged, under LGPL-2.1+. See the README in that folder.

## Contributing

Issues and pull requests are welcome. [CONTRIBUTING.md](CONTRIBUTING.md)
says how a change gets in, and [SECURITY.md](SECURITY.md) how to report a
vulnerability privately.

## License

Hawser is licensed under the [Apache License 2.0](LICENSE).
Contributions are accepted under the same license.
