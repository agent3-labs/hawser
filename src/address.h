// IPv4 addresses and the network they are handed out from.

#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hawser {

struct Ipv4Address {
    // Host byte order, so addresses compare and count like numbers.
    uint32_t value = 0;

    auto operator<=>(const Ipv4Address&) const = default;
};

std::optional<Ipv4Address> parse_address(std::string_view text);
std::string format_address(Ipv4Address address);

struct Ipv4Network {
    Ipv4Address base;
    int prefix_length = 0;

    uint32_t mask() const;
    bool contains(Ipv4Address address) const;
    // The addresses a machine may hold: everything but the network's own
    // address and its broadcast address.
    Ipv4Address first_host() const;
    Ipv4Address last_host() const;

    bool operator==(const Ipv4Network&) const = default;
};

// "10.77.0.0/24". Refuses a base with host bits set ("10.77.0.5/24") rather
// than guessing which network was meant.
std::optional<Ipv4Network> parse_network(std::string_view text);
std::string format_network(const Ipv4Network& network);

// The single-address network that routes exactly one machine.
Ipv4Network network_of_one(Ipv4Address address);

}  // namespace hawser
