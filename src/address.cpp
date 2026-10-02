#include "address.h"

#include <charconv>

#include <arpa/inet.h>

namespace hawser {

namespace {

// Small enough to leave room for hosts, large enough to be a private range.
const int PREFIX_LENGTH_MIN = 8;
const int PREFIX_LENGTH_MAX = 30;

}  // namespace

std::optional<Ipv4Address> parse_address(std::string_view text) {
    in_addr parsed{};
    if (inet_pton(AF_INET, std::string(text).c_str(), &parsed) != 1) return std::nullopt;
    return Ipv4Address{ntohl(parsed.s_addr)};
}

std::string format_address(Ipv4Address address) {
    in_addr raw{};
    raw.s_addr = htonl(address.value);
    char text[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &raw, text, sizeof(text));
    return text;
}

uint32_t Ipv4Network::mask() const {
    return prefix_length == 0 ? 0 : ~uint32_t{0} << (32 - prefix_length);
}

bool Ipv4Network::contains(Ipv4Address address) const {
    return (address.value & mask()) == base.value;
}

Ipv4Address Ipv4Network::first_host() const {
    return prefix_length == 32 ? base : Ipv4Address{base.value + 1};
}

Ipv4Address Ipv4Network::last_host() const {
    return prefix_length == 32 ? base : Ipv4Address{(base.value | ~mask()) - 1};
}

std::optional<Ipv4Network> parse_network(std::string_view text) {
    const size_t slash = text.find('/');
    if (slash == std::string_view::npos) return std::nullopt;
    const auto base = parse_address(text.substr(0, slash));
    if (!base) return std::nullopt;

    const std::string_view prefix_text = text.substr(slash + 1);
    int prefix_length = 0;
    const auto [end, error] = std::from_chars(prefix_text.data(), prefix_text.data() + prefix_text.size(), prefix_length);
    if (error != std::errc{} || end != prefix_text.data() + prefix_text.size()) return std::nullopt;
    if (prefix_length < PREFIX_LENGTH_MIN || prefix_length > PREFIX_LENGTH_MAX) return std::nullopt;

    const Ipv4Network network{*base, prefix_length};
    if ((base->value & ~network.mask()) != 0) return std::nullopt;
    return network;
}

std::string format_network(const Ipv4Network& network) {
    return format_address(network.base) + "/" + std::to_string(network.prefix_length);
}

Ipv4Network network_of_one(Ipv4Address address) {
    return Ipv4Network{address, 32};
}

}  // namespace hawser
