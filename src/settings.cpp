#include "settings.h"

#include <charconv>
#include <cstdlib>
#include <map>

#include "text.h"

namespace hawser {

namespace {

const int PORT_MAX = 65535;
// The least IPv4 allows, and the most an Ethernet path carries.
const int MTU_BYTES_MIN = 576;
const int MTU_BYTES_MAX = 9000;

std::string read_environment(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? value : fallback;
}

int parse_number(const std::string& name, const std::string& text, int minimum, int maximum) {
    int number = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (error != std::errc{} || end != text.data() + text.size() || number < minimum || number > maximum) {
        throw HawserError(name + " must be a number from " + std::to_string(minimum) + " to " +
                          std::to_string(maximum) + ", not '" + text + "'");
    }
    return number;
}

const std::string& require_value(const std::map<std::string, std::string>& values, const std::string& name) {
    const auto found = values.find(name);
    if (found == values.end() || found->second.empty()) throw HawserError("hawser.conf: " + name + " is not set");
    return found->second;
}

}  // namespace

Paths read_paths() {
    return Paths{
        read_environment("HAWSER_CONFIG_DIR", "/etc/hawser"),
        read_environment("HAWSER_STATE_DIR", "/var/lib/hawser"),
    };
}

std::string format_settings(const Settings& settings) {
    const bool is_coordinator = settings.role == Role::coordinator;
    std::string text =
        "# Written by hawser. This machine's place in the network.\n"
        "role=" + std::string(is_coordinator ? "coordinator" : "agent") + "\n"
        "network_name=" + settings.network_name + "\n"
        "network=" + format_network(settings.network) + "\n"
        "name=" + settings.name + "\n"
        "address=" + format_address(settings.address) + "\n"
        "interface_name=" + settings.interface_name + "\n"
        "listen_port=" + std::to_string(settings.listen_port) + "\n"
        "api_port=" + std::to_string(settings.api_port) + "\n"
        "mtu_bytes=" + std::to_string(settings.mtu_bytes) + "\n"
        "# yes keeps every machine's name in the hosts file; no leaves that file alone.\n"
        "manages_hosts=" + std::string(settings.manages_hosts ? "yes" : "no") + "\n"
        "hosts_file=" + settings.hosts_file + "\n"
        "# Where machines dial the coordinator from outside the network.\n"
        "coordinator_endpoint=" + settings.coordinator_endpoint + "\n";
    if (!is_coordinator) {
        text += "coordinator_address=" + format_address(settings.coordinator_address) + "\n"
                "coordinator_public_key=" + format_key(settings.coordinator_public_key) + "\n";
    }
    return text;
}

Settings parse_settings(const std::string& text) {
    const auto values = parse_named_values(text, "hawser.conf");
    Settings settings;

    const std::string role = require_value(values, "role");
    if (role == "coordinator") settings.role = Role::coordinator;
    else if (role == "agent") settings.role = Role::agent;
    else throw HawserError("hawser.conf: role must be coordinator or agent, not '" + role + "'");

    settings.network_name = require_value(values, "network_name");
    const auto network = parse_network(require_value(values, "network"));
    if (!network) throw HawserError("hawser.conf: network is not a network such as 10.77.0.0/24");
    settings.network = *network;

    settings.name = require_value(values, "name");
    const auto address = parse_address(require_value(values, "address"));
    if (!address || !settings.network.contains(*address)) {
        throw HawserError("hawser.conf: address is not an address inside " + format_network(settings.network));
    }
    settings.address = *address;

    settings.interface_name = require_value(values, "interface_name");
    settings.listen_port = parse_number("listen_port", require_value(values, "listen_port"), 1, PORT_MAX);
    settings.api_port = parse_number("api_port", require_value(values, "api_port"), 1, PORT_MAX);
    settings.mtu_bytes = parse_number("mtu_bytes", require_value(values, "mtu_bytes"), MTU_BYTES_MIN, MTU_BYTES_MAX);

    const std::string manages_hosts = require_value(values, "manages_hosts");
    if (manages_hosts != "yes" && manages_hosts != "no") throw HawserError("hawser.conf: manages_hosts must be yes or no");
    settings.manages_hosts = manages_hosts == "yes";
    settings.hosts_file = require_value(values, "hosts_file");

    settings.coordinator_endpoint = require_value(values, "coordinator_endpoint");
    parse_host_and_port(settings.coordinator_endpoint);

    if (settings.role == Role::agent) {
        const auto coordinator_address = parse_address(require_value(values, "coordinator_address"));
        if (!coordinator_address || !settings.network.contains(*coordinator_address)) {
            throw HawserError("hawser.conf: coordinator_address is not an address inside the network");
        }
        settings.coordinator_address = *coordinator_address;
        const auto coordinator_public_key = parse_key(require_value(values, "coordinator_public_key"));
        if (!coordinator_public_key) throw HawserError("hawser.conf: coordinator_public_key is not a WireGuard key");
        settings.coordinator_public_key = *coordinator_public_key;
    }
    return settings;
}

Settings load_settings(const Paths& paths) {
    if (!file_exists(paths.settings_file())) {
        throw HawserError("this machine is not set up: " + paths.settings_file() +
                          " does not exist. Run 'hawser init' on the coordinator or 'hawser join' on any other machine.");
    }
    return parse_settings(read_file(paths.settings_file()));
}

void save_settings(const Paths& paths, const Settings& settings) {
    make_directory(paths.config_dir, 0700);
    write_file(paths.settings_file(), format_settings(settings), 0600);
}

Key load_private_key(const Paths& paths) {
    const auto key = parse_key(trim(read_file(paths.private_key_file())));
    if (!key) throw HawserError(paths.private_key_file() + " does not hold a WireGuard key");
    return *key;
}

void save_private_key(const Paths& paths, const Key& private_key) {
    make_directory(paths.config_dir, 0700);
    write_file(paths.private_key_file(), format_key(private_key) + "\n", 0600);
}

HostAndPort parse_host_and_port(const std::string& text) {
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == text.size()) {
        throw HawserError("'" + text + "' is not host:port, for example vpn.example.com:51820");
    }
    return HostAndPort{text.substr(0, colon), parse_number("the port", text.substr(colon + 1), 1, PORT_MAX)};
}

}  // namespace hawser
