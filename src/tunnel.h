// The WireGuard interface in the kernel: its keys, its address and its peers.
//
// Everything that needs root is in here. The rest of hawser decides what the
// interface should look like; this makes it so.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "address.h"
#include "key.h"

namespace hawser {

struct TunnelPeer {
    Key public_key;
    // "address:port" the peer is dialled at. Empty when it is not known, in
    // which case the kernel waits for the peer to call first.
    std::string endpoint;
    // The addresses routed to this peer, and accepted from it.
    std::vector<Ipv4Network> allowed_networks;
    // How often to send an empty packet so a NAT in between keeps the path
    // open. Zero sends none.
    int keepalive_seconds = 0;
    // Give the kernel `endpoint` even though it already has this peer. Without
    // it the kernel keeps the endpoint it learned, which follows a peer that
    // moves.
    bool replaces_endpoint = false;

    // Filled in by read_tunnel only.
    int64_t last_handshake_unix_seconds = 0;
    uint64_t received_bytes = 0;
};

struct TunnelState {
    Key public_key;
    int listen_port = 0;
    std::vector<TunnelPeer> peers;
};

// Creates the interface, or leaves the one already there alone.
void create_tunnel(const std::string& interface_name);
void remove_tunnel(const std::string& interface_name);
bool tunnel_exists(const std::string& interface_name);

// Sets the private key and the UDP port. Peers and their sessions are kept.
void set_tunnel_identity(const std::string& interface_name, const Key& private_key, int listen_port);

// Gives the interface its address inside the network and brings it up. The
// kernel then routes the whole network to the interface.
void configure_tunnel_address(const std::string& interface_name, Ipv4Address address, const Ipv4Network& network, int mtu_bytes);

TunnelState read_tunnel(const std::string& interface_name);

// Makes the kernel's peers exactly `wanted`: adds the new, updates the rest,
// removes the others. A peer that stays keeps its session.
void apply_peers(const std::string& interface_name, const std::vector<TunnelPeer>& wanted);

// Lets this machine pass packets between two others, which is how machines
// that cannot reach each other talk through the coordinator.
void enable_forwarding(const std::string& interface_name);

// "host:port" to "address:port", looking the host up if it is a name.
std::string resolve_endpoint(const std::string& host_and_port);

}  // namespace hawser
