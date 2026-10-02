#include "invite.h"

#include <charconv>

#include "settings.h"
#include "text.h"

namespace hawser {

namespace {

// The number is the format's version, so a later format can be told apart.
const std::string INVITE_PREFIX = "hawser1:";

const std::string& require_value(const std::map<std::string, std::string>& values, const std::string& name) {
    const auto found = values.find(name);
    if (found == values.end() || found->second.empty()) throw HawserError("the invite is missing " + name);
    return found->second;
}

int64_t parse_whole_number(const std::string& name, const std::string& text) {
    int64_t number = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (error != std::errc{} || end != text.data() + text.size() || number <= 0) {
        throw HawserError("the invite's " + name + " is not a number");
    }
    return number;
}

}  // namespace

std::string encode_invite(const Invite& invite) {
    const std::string body =
        "network_name=" + invite.network_name + "\n"
        "network=" + format_network(invite.network) + "\n"
        "name=" + invite.name + "\n"
        "address=" + format_address(invite.address) + "\n"
        "one_time_private_key=" + format_key(invite.one_time_private_key) + "\n"
        "coordinator_endpoint=" + invite.coordinator_endpoint + "\n"
        "coordinator_address=" + format_address(invite.coordinator_address) + "\n"
        "coordinator_public_key=" + format_key(invite.coordinator_public_key) + "\n"
        "api_port=" + std::to_string(invite.api_port) + "\n"
        "mtu_bytes=" + std::to_string(invite.mtu_bytes) + "\n"
        "expires_unix_seconds=" + std::to_string(invite.expires_unix_seconds) + "\n";
    return INVITE_PREFIX + encode_base64(body);
}

Invite decode_invite(const std::string& text) {
    const std::string trimmed = trim(text);
    if (trimmed.rfind(INVITE_PREFIX, 0) != 0) {
        throw HawserError("that is not an invite. One starts with " + INVITE_PREFIX);
    }
    const auto values = parse_named_values(decode_base64(trimmed.substr(INVITE_PREFIX.size())), "the invite");

    Invite invite;
    invite.network_name = require_value(values, "network_name");
    invite.name = require_value(values, "name");
    invite.coordinator_endpoint = require_value(values, "coordinator_endpoint");
    parse_host_and_port(invite.coordinator_endpoint);

    const auto network = parse_network(require_value(values, "network"));
    const auto address = parse_address(require_value(values, "address"));
    const auto coordinator_address = parse_address(require_value(values, "coordinator_address"));
    const auto one_time_private_key = parse_key(require_value(values, "one_time_private_key"));
    const auto coordinator_public_key = parse_key(require_value(values, "coordinator_public_key"));
    if (!network || !address || !coordinator_address || !one_time_private_key || !coordinator_public_key ||
        !network->contains(*address) || !network->contains(*coordinator_address)) {
        throw HawserError("the invite is damaged. Ask for a new one.");
    }
    invite.network = *network;
    invite.address = *address;
    invite.coordinator_address = *coordinator_address;
    invite.one_time_private_key = *one_time_private_key;
    invite.coordinator_public_key = *coordinator_public_key;
    invite.api_port = static_cast<int>(parse_whole_number("api_port", require_value(values, "api_port")));
    invite.mtu_bytes = static_cast<int>(parse_whole_number("mtu_bytes", require_value(values, "mtu_bytes")));
    invite.expires_unix_seconds = parse_whole_number("expires_unix_seconds", require_value(values, "expires_unix_seconds"));
    return invite;
}

}  // namespace hawser
