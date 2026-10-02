// What one machine knows about itself and its network, kept in hawser.conf.

#pragma once

#include <string>

#include "address.h"
#include "key.h"

namespace hawser {

const char DEFAULT_INTERFACE_NAME[] = "hawser0";
// WireGuard's own customary port.
const int DEFAULT_LISTEN_PORT = 51820;
const int DEFAULT_API_PORT = 51821;
// What fits inside a 1500-byte packet once WireGuard has wrapped it.
const int DEFAULT_MTU_BYTES = 1420;
const char DEFAULT_HOSTS_FILE[] = "/etc/hosts";

enum class Role { coordinator, agent };

struct Settings {
    Role role = Role::agent;
    std::string network_name;
    Ipv4Network network;
    // This machine.
    std::string name;
    Ipv4Address address;
    std::string interface_name = DEFAULT_INTERFACE_NAME;
    int listen_port = DEFAULT_LISTEN_PORT;
    // Where the coordinator answers agents, on its address inside the network.
    int api_port = DEFAULT_API_PORT;
    int mtu_bytes = DEFAULT_MTU_BYTES;
    bool manages_hosts = true;
    std::string hosts_file = DEFAULT_HOSTS_FILE;

    // "host:port" that machines outside dial to reach the coordinator. On the
    // coordinator it is its own; on an agent it is where to find the coordinator.
    std::string coordinator_endpoint;
    // Agent only.
    Ipv4Address coordinator_address;
    Key coordinator_public_key;
};

struct Paths {
    std::string config_dir;
    std::string state_dir;

    std::string settings_file() const { return config_dir + "/hawser.conf"; }
    std::string private_key_file() const { return config_dir + "/private.key"; }
    std::string machines_file() const { return state_dir + "/machines"; }
    std::string machines_lock_file() const { return state_dir + "/machines.lock"; }
};

// /etc/hawser and /var/lib/hawser, unless HAWSER_CONFIG_DIR or
// HAWSER_STATE_DIR say otherwise.
Paths read_paths();

std::string format_settings(const Settings& settings);
// Throws HawserError naming the first value that is missing or malformed.
Settings parse_settings(const std::string& text);

Settings load_settings(const Paths& paths);
void save_settings(const Paths& paths, const Settings& settings);

Key load_private_key(const Paths& paths);
void save_private_key(const Paths& paths, const Key& private_key);

// "host:port" split into its halves. Throws HawserError when it is not that.
struct HostAndPort {
    std::string host;
    int port = 0;
};
HostAndPort parse_host_and_port(const std::string& text);

}  // namespace hawser
