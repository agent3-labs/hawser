// What an agent and the coordinator say to each other.
//
// It is plain text over TCP, and it only ever travels inside the WireGuard
// tunnel. WireGuard has already encrypted it and proved which machine is
// calling: a packet from an address can only have come from the machine
// holding that address's key. So the protocol itself carries no passwords.
//
//   agent:        peers
//   coordinator:  hawser 1
//                 network <name> <network>
//                 machine <name> <address> <public key> <endpoint or -> <seconds since seen or ->
//                 ...
//                 end
//
//   agent:        redeem <public key>
//   coordinator:  ok                      or: error <why>

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "address.h"
#include "key.h"

namespace hawser {

struct MachineView {
    std::string name;
    Ipv4Address address;
    Key public_key;
    // Where the coordinator last heard this machine from, as "address:port".
    // Empty when it has not been heard from.
    std::string endpoint;
    // How long ago the coordinator last heard from it. Nothing when never.
    std::optional<int64_t> seen_seconds_ago;

    bool operator==(const MachineView&) const = default;
};

struct NetworkView {
    std::string network_name;
    Ipv4Network network;
    std::vector<MachineView> machines;

    bool operator==(const NetworkView&) const = default;
};

std::string format_network_view(const NetworkView& view);
// Throws HawserError when the text is cut short or is not a network view.
NetworkView parse_network_view(const std::string& text);

// Sends one line to the coordinator and returns everything it answers.
// Throws HawserError when it cannot be reached within the timeout.
std::string call_coordinator(Ipv4Address coordinator_address, int api_port, const std::string& request_line, int timeout_seconds);

}  // namespace hawser
