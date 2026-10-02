#include "tunnel.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>

#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "settings.h"
#include "text.h"

extern "C" {
#include "wireguard.h"
}

namespace hawser {

namespace {

std::string describe_failure(const std::string& action, int negative_errno) {
    const int code = negative_errno < 0 ? -negative_errno : errno;
    std::string message = action + ": " + std::strerror(code);
    if (code == EPERM || code == EACCES) message += ". hawser changes the network, so it has to run as root.";
    return message;
}

void copy_interface_name(char* destination, const std::string& interface_name) {
    if (interface_name.empty() || interface_name.size() >= IFNAMSIZ) {
        throw HawserError("'" + interface_name + "' is not usable as an interface name");
    }
    std::memset(destination, 0, IFNAMSIZ);
    std::memcpy(destination, interface_name.data(), interface_name.size());
}

std::string format_endpoint(const wg_endpoint& endpoint) {
    if (endpoint.addr.sa_family != AF_INET) return "";
    char address[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &endpoint.addr4.sin_addr, address, sizeof(address));
    return std::string(address) + ":" + std::to_string(ntohs(endpoint.addr4.sin_port));
}

// "address:port" as the kernel wants it. The address must already be numeric.
sockaddr_in parse_endpoint(const std::string& endpoint) {
    const HostAndPort parts = parse_host_and_port(endpoint);
    sockaddr_in socket_address{};
    socket_address.sin_family = AF_INET;
    socket_address.sin_port = htons(static_cast<uint16_t>(parts.port));
    if (inet_pton(AF_INET, parts.host.c_str(), &socket_address.sin_addr) != 1) {
        throw HawserError("'" + endpoint + "' is not an IPv4 address and port");
    }
    return socket_address;
}

// Runs one of the old interface ioctls, which is all that setting an address,
// a size and "up" takes.
void run_interface_ioctl(int control_socket, unsigned long request, ifreq& arguments, const std::string& action) {
    if (ioctl(control_socket, request, &arguments) != 0) {
        const int code = errno;
        close(control_socket);
        throw HawserError(describe_failure(action, -code));
    }
}

void set_interface_address(int control_socket, ifreq& arguments, unsigned long request, uint32_t value, const std::string& action) {
    auto* socket_address = reinterpret_cast<sockaddr_in*>(&arguments.ifr_addr);
    *socket_address = {};
    socket_address->sin_family = AF_INET;
    socket_address->sin_addr.s_addr = htonl(value);
    run_interface_ioctl(control_socket, request, arguments, action);
}

std::string read_first_line(const std::string& path) {
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    return trim(line);
}

bool write_kernel_setting(const std::string& path, const std::string& value) {
    if (read_first_line(path) == value) return true;
    std::ofstream file(path);
    file << value;
    file.flush();
    return static_cast<bool>(file) && read_first_line(path) == value;
}

}  // namespace

bool tunnel_exists(const std::string& interface_name) {
    return if_nametoindex(interface_name.c_str()) != 0;
}

void create_tunnel(const std::string& interface_name) {
    if (tunnel_exists(interface_name)) return;
    const int result = wg_add_device(interface_name.c_str());
    if (result < 0) {
        throw HawserError(describe_failure("cannot create the WireGuard interface " + interface_name, result) +
                          " If the kernel has no WireGuard, load it with: modprobe wireguard");
    }
}

void remove_tunnel(const std::string& interface_name) {
    if (!tunnel_exists(interface_name)) return;
    const int result = wg_del_device(interface_name.c_str());
    if (result < 0) throw HawserError(describe_failure("cannot remove " + interface_name, result));
}

void set_tunnel_identity(const std::string& interface_name, const Key& private_key, int listen_port) {
    wg_device device{};
    copy_interface_name(device.name, interface_name);
    device.flags = static_cast<wg_device_flags>(WGDEVICE_HAS_PRIVATE_KEY | WGDEVICE_HAS_LISTEN_PORT);
    std::memcpy(device.private_key, private_key.bytes.data(), sizeof(device.private_key));
    device.listen_port = static_cast<uint16_t>(listen_port);
    const int result = wg_set_device(&device);
    if (result < 0) throw HawserError(describe_failure("cannot set the key and port of " + interface_name, result));
}

void configure_tunnel_address(const std::string& interface_name, Ipv4Address address, const Ipv4Network& network, int mtu_bytes) {
    const int control_socket = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (control_socket < 0) throw HawserError(describe_failure("cannot open a socket", 0));

    ifreq arguments{};
    copy_interface_name(arguments.ifr_name, interface_name);
    set_interface_address(control_socket, arguments, SIOCSIFADDR, address.value, "cannot set the address of " + interface_name);
    // With the network's mask on the address, the kernel adds the route to the whole network by itself.
    set_interface_address(control_socket, arguments, SIOCSIFNETMASK, network.mask(), "cannot set the netmask of " + interface_name);

    arguments.ifr_mtu = mtu_bytes;
    run_interface_ioctl(control_socket, SIOCSIFMTU, arguments, "cannot set the packet size of " + interface_name);

    run_interface_ioctl(control_socket, SIOCGIFFLAGS, arguments, "cannot read the state of " + interface_name);
    arguments.ifr_flags = static_cast<short>(arguments.ifr_flags | IFF_UP | IFF_RUNNING);
    run_interface_ioctl(control_socket, SIOCSIFFLAGS, arguments, "cannot bring up " + interface_name);
    close(control_socket);
}

TunnelState read_tunnel(const std::string& interface_name) {
    wg_device* device = nullptr;
    const int result = wg_get_device(&device, interface_name.c_str());
    if (result < 0 || device == nullptr) throw HawserError(describe_failure("cannot read " + interface_name, result));

    TunnelState state;
    std::memcpy(state.public_key.bytes.data(), device->public_key, sizeof(device->public_key));
    state.listen_port = device->listen_port;
    for (const wg_peer* peer = device->first_peer; peer != nullptr; peer = peer->next_peer) {
        TunnelPeer tunnel_peer;
        std::memcpy(tunnel_peer.public_key.bytes.data(), peer->public_key, sizeof(peer->public_key));
        tunnel_peer.endpoint = format_endpoint(peer->endpoint);
        tunnel_peer.keepalive_seconds = peer->persistent_keepalive_interval;
        tunnel_peer.last_handshake_unix_seconds = peer->last_handshake_time.tv_sec;
        tunnel_peer.received_bytes = peer->rx_bytes;
        for (const wg_allowedip* allowed = peer->first_allowedip; allowed != nullptr; allowed = allowed->next_allowedip) {
            if (allowed->family != AF_INET) continue;
            tunnel_peer.allowed_networks.push_back(Ipv4Network{Ipv4Address{ntohl(allowed->ip4.s_addr)}, allowed->cidr});
        }
        state.peers.push_back(tunnel_peer);
    }
    wg_free_device(device);
    return state;
}

void apply_peers(const std::string& interface_name, const std::vector<TunnelPeer>& wanted) {
    const TunnelState current = read_tunnel(interface_name);
    const auto is_known_to_kernel = [&](const Key& key) {
        return std::any_of(current.peers.begin(), current.peers.end(),
                           [&](const TunnelPeer& peer) { return peer.public_key == key; });
    };
    const auto is_wanted = [&](const Key& key) {
        return std::any_of(wanted.begin(), wanted.end(), [&](const TunnelPeer& peer) { return peer.public_key == key; });
    };

    // Both lists are built whole before anything points into them: the kernel
    // library takes linked lists, and a vector that grows moves its elements.
    std::vector<wg_peer> peers;
    std::vector<std::vector<wg_allowedip>> allowed_lists;

    for (const TunnelPeer& wanted_peer : wanted) {
        wg_peer peer{};
        peer.flags = static_cast<wg_peer_flags>(WGPEER_HAS_PUBLIC_KEY | WGPEER_REPLACE_ALLOWEDIPS |
                                                WGPEER_HAS_PERSISTENT_KEEPALIVE_INTERVAL);
        std::memcpy(peer.public_key, wanted_peer.public_key.bytes.data(), sizeof(peer.public_key));
        peer.persistent_keepalive_interval = static_cast<uint16_t>(wanted_peer.keepalive_seconds);
        const bool gives_endpoint = !wanted_peer.endpoint.empty() &&
                                    (wanted_peer.replaces_endpoint || !is_known_to_kernel(wanted_peer.public_key));
        if (gives_endpoint) peer.endpoint.addr4 = parse_endpoint(wanted_peer.endpoint);
        peers.push_back(peer);

        std::vector<wg_allowedip> allowed_list;
        for (const Ipv4Network& network : wanted_peer.allowed_networks) {
            wg_allowedip allowed{};
            allowed.family = AF_INET;
            allowed.ip4.s_addr = htonl(network.base.value);
            allowed.cidr = static_cast<uint8_t>(network.prefix_length);
            allowed_list.push_back(allowed);
        }
        allowed_lists.push_back(allowed_list);
    }
    for (const TunnelPeer& current_peer : current.peers) {
        if (is_wanted(current_peer.public_key)) continue;
        wg_peer peer{};
        peer.flags = static_cast<wg_peer_flags>(WGPEER_HAS_PUBLIC_KEY | WGPEER_REMOVE_ME);
        std::memcpy(peer.public_key, current_peer.public_key.bytes.data(), sizeof(peer.public_key));
        peers.push_back(peer);
        allowed_lists.emplace_back();
    }
    if (peers.empty()) return;

    for (size_t index = 0; index < peers.size(); ++index) {
        std::vector<wg_allowedip>& allowed_list = allowed_lists[index];
        for (size_t allowed_index = 0; allowed_index + 1 < allowed_list.size(); ++allowed_index) {
            allowed_list[allowed_index].next_allowedip = &allowed_list[allowed_index + 1];
        }
        if (!allowed_list.empty()) {
            peers[index].first_allowedip = &allowed_list.front();
            peers[index].last_allowedip = &allowed_list.back();
        }
        if (index + 1 < peers.size()) peers[index].next_peer = &peers[index + 1];
    }

    wg_device device{};
    copy_interface_name(device.name, interface_name);
    device.first_peer = &peers.front();
    device.last_peer = &peers.back();
    const int result = wg_set_device(&device);
    if (result < 0) throw HawserError(describe_failure("cannot set the peers of " + interface_name, result));
}

void enable_forwarding(const std::string& interface_name) {
    if (!write_kernel_setting("/proc/sys/net/ipv4/ip_forward", "1")) {
        throw HawserError(
            "cannot turn on packet forwarding (net.ipv4.ip_forward). The coordinator needs it to pass "
            "traffic between machines that cannot reach each other. Set it with: sysctl -w net.ipv4.ip_forward=1");
    }
    // A packet relayed here leaves by the interface it came in on. Left alone,
    // the kernel answers that with a redirect telling the sender to go direct,
    // which is exactly what the sender could not do. The kernel uses the
    // larger of the two settings, so both have to be off. Not fatal if it
    // cannot be set: relaying still works, with stray redirects.
    write_kernel_setting("/proc/sys/net/ipv4/conf/" + interface_name + "/send_redirects", "0");
    write_kernel_setting("/proc/sys/net/ipv4/conf/all/send_redirects", "0");
}

std::string resolve_endpoint(const std::string& host_and_port) {
    const HostAndPort parts = parse_host_and_port(host_and_port);
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* found = nullptr;
    const int result = getaddrinfo(parts.host.c_str(), nullptr, &hints, &found);
    if (result != 0 || found == nullptr) {
        throw HawserError("cannot find the address of " + parts.host + ": " + gai_strerror(result));
    }
    char address[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr, address, sizeof(address));
    freeaddrinfo(found);
    return std::string(address) + ":" + std::to_string(parts.port);
}

}  // namespace hawser
