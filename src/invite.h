// The one line a new machine is given to join the network.

#pragma once

#include <cstdint>
#include <string>

#include "address.h"
#include "key.h"

namespace hawser {

struct Invite {
    std::string network_name;
    Ipv4Network network;
    // The new machine's name and address, chosen by the coordinator.
    std::string name;
    Ipv4Address address;
    // Lets the new machine reach the coordinator once, to hand over a key of
    // its own. The coordinator stops accepting it the moment that happens.
    Key one_time_private_key;
    std::string coordinator_endpoint;
    Ipv4Address coordinator_address;
    Key coordinator_public_key;
    int api_port = 0;
    int mtu_bytes = 0;
    int64_t expires_unix_seconds = 0;
};

// "hawser1:" followed by Base64. It carries a private key: treat it as a
// password until it has been used.
std::string encode_invite(const Invite& invite);
// Throws HawserError when the text is not an invite.
Invite decode_invite(const std::string& text);

}  // namespace hawser
