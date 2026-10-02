#include "daemon.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "hosts.h"
#include "text.h"

namespace hawser {

namespace {

// How often the kernel's view of the peers is read. Short, because this is
// what notices a direct path coming up or going quiet.
const int OBSERVE_INTERVAL_SECONDS = 2;
// How often an agent asks the coordinator for the list of machines.
const int FETCH_INTERVAL_SECONDS = 10;
const int COORDINATOR_SYNC_INTERVAL_SECONDS = 5;
const int API_TIMEOUT_SECONDS = 5;
const int API_BACKLOG = 16;
const size_t REQUEST_LENGTH_MAX = 256;
const int MILLISECONDS_PER_SECOND = 1000;
// An agent that has heard nothing from the coordinator for this long looks its
// address up again, in case the coordinator moved.
const int COORDINATOR_SILENCE_SECONDS = 120;
// How soon to try again when the coordinator's name cannot be looked up.
const int RESOLVE_RETRY_SECONDS = 10;

std::atomic<bool> stop_requested{false};

void request_stop(int) {
    stop_requested = true;
}

// Tells systemd the interface is up, so that whatever is ordered after this
// service, Docker for one, starts with the machine's address already there.
// This is systemd's notify protocol: one datagram to the socket it names.
// Run by hand, there is no such socket and nothing happens.
void notify_service_ready() {
    const char* socket_path = std::getenv("NOTIFY_SOCKET");
    if (socket_path == nullptr || *socket_path == '\0') return;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const size_t path_length = std::strlen(socket_path);
    if (path_length >= sizeof(address.sun_path)) return;
    std::memcpy(address.sun_path, socket_path, path_length);
    // A leading @ names a socket that is not a file; the kernel spells that with a zero byte.
    if (address.sun_path[0] == '@') address.sun_path[0] = '\0';

    const int notify_socket = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (notify_socket < 0) return;
    const char message[] = "READY=1";
    sendto(notify_socket, message, sizeof(message) - 1, MSG_NOSIGNAL, reinterpret_cast<sockaddr*>(&address),
           static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path_length));
    close(notify_socket);
}

std::vector<HostEntry> list_host_entries(const std::vector<Machine>& machines) {
    std::vector<HostEntry> entries;
    for (const Machine& machine : machines) {
        if (machine.state == MachineState::joined) entries.push_back(HostEntry{machine.name, machine.address});
    }
    return entries;
}

void refresh_hosts(const Settings& settings, const std::vector<HostEntry>& entries) {
    if (!settings.manages_hosts) return;
    try {
        update_hosts_file(settings.hosts_file, settings.network_name, entries);
    } catch (const HawserError& error) {
        // Names are a convenience. Failing to write them must not stop the network.
        log_line(error.what());
    }
}

void send_all(int connection, const std::string& text) {
    send(connection, text.data(), text.size(), MSG_NOSIGNAL);
}

// One line from the caller, or nothing if it sends none in time.
std::optional<std::string> read_request_line(int connection) {
    std::string line;
    char buffer[REQUEST_LENGTH_MAX];
    while (line.size() < REQUEST_LENGTH_MAX) {
        pollfd waiting{connection, POLLIN, 0};
        if (poll(&waiting, 1, API_TIMEOUT_SECONDS * MILLISECONDS_PER_SECOND) <= 0) return std::nullopt;
        const ssize_t count = recv(connection, buffer, sizeof(buffer), 0);
        if (count <= 0) return std::nullopt;
        line.append(buffer, static_cast<size_t>(count));
        const size_t line_end = line.find('\n');
        if (line_end != std::string::npos) return trim(line.substr(0, line_end));
    }
    return std::nullopt;
}

// Waits until the caller has hung up, or gives up after the timeout.
void wait_for_hangup(int connection) {
    char buffer[64];
    pollfd waiting{connection, POLLIN, 0};
    while (poll(&waiting, 1, API_TIMEOUT_SECONDS * MILLISECONDS_PER_SECOND) > 0) {
        if (recv(connection, buffer, sizeof(buffer), 0) <= 0) return;
    }
}

class Coordinator {
  public:
    Coordinator(const Paths& paths, const Settings& settings) : paths_(paths), settings_(settings) {}

    // Brings the kernel and the hosts file in line with the machines file.
    void sync() {
        const std::lock_guard<std::mutex> guard(sync_mutex_);
        // The file stays locked until the kernel matches it. `invite` and
        // `remove` change both under the same lock, so a sync that read the
        // file a moment before them cannot put the old peers back.
        const MachinesLock lock(paths_);
        const std::vector<Machine> recorded = lock.load();
        const std::vector<Machine> machines = drop_expired_invites(recorded, read_unix_seconds());
        if (machines.size() != recorded.size()) {
            lock.save(machines);
            log_line("removed " + std::to_string(recorded.size() - machines.size()) + " invite(s) nobody used in time");
        }
        apply_peers(settings_.interface_name, plan_coordinator_peers(settings_, machines));
        refresh_hosts(settings_, list_host_entries(machines));
    }

    void serve_until_stopped() {
        const int listener = open_listener();
        while (!is_stop_requested()) {
            pollfd waiting{listener, POLLIN, 0};
            if (poll(&waiting, 1, MILLISECONDS_PER_SECOND) <= 0) continue;
            sockaddr_in caller{};
            socklen_t caller_length = sizeof(caller);
            const int connection = accept4(listener, reinterpret_cast<sockaddr*>(&caller), &caller_length, SOCK_CLOEXEC);
            if (connection < 0) continue;
            try {
                answer(connection, Ipv4Address{ntohl(caller.sin_addr.s_addr)});
            } catch (const HawserError& error) {
                log_line(std::string("could not answer a machine: ") + error.what());
            }
            close(connection);
        }
        close(listener);
    }

  private:
    int open_listener() const {
        const int listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (listener < 0) throw HawserError(std::string("cannot open a socket: ") + std::strerror(errno));
        const int enabled = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
        // Bound to the address inside the network and nothing else, so the
        // only way to reach it is through the tunnel.
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(settings_.api_port));
        address.sin_addr.s_addr = htonl(settings_.address.value);
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(listener, API_BACKLOG) != 0) {
            const std::string reason = std::strerror(errno);
            close(listener);
            throw HawserError("cannot listen on " + format_address(settings_.address) + ":" +
                              std::to_string(settings_.api_port) + ": " + reason);
        }
        return listener;
    }

    void answer(int connection, Ipv4Address caller_address) {
        const std::optional<std::string> request = read_request_line(connection);
        if (!request) return;
        const std::vector<std::string> words = split_words(*request);

        // Held to the end, so the regular sync cannot swap a key in the
        // kernel while its owner is still being told the swap was accepted.
        const std::lock_guard<std::mutex> guard(sync_mutex_);
        const MachinesLock lock(paths_);
        std::vector<Machine> machines = lock.load();

        // WireGuard only lets a packet in from an address if it was sealed
        // with that address's key, so the caller's address is its identity.
        const auto caller = std::find_if(machines.begin(), machines.end(),
                                         [&](const Machine& machine) { return machine.address == caller_address; });
        if (caller == machines.end()) {
            send_all(connection, "error this address belongs to no machine\n");
            return;
        }

        if (words.size() == 1 && words[0] == "peers") {
            if (caller->state != MachineState::joined) {
                send_all(connection, "error join first\n");
                return;
            }
            const NetworkView view =
                build_network_view(settings_, machines, read_tunnel(settings_.interface_name), read_unix_seconds());
            send_all(connection, format_network_view(view));
            return;
        }

        if (words.size() == 2 && words[0] == "redeem") {
            const std::optional<Key> new_public_key = parse_key(words[1]);
            const bool is_key_taken = new_public_key && std::any_of(machines.begin(), machines.end(), [&](const Machine& machine) {
                return machine.public_key == *new_public_key;
            });
            if (caller->state != MachineState::invited) {
                send_all(connection, "error this invite was already used\n");
            } else if (!new_public_key || *new_public_key == Key{} || is_key_taken) {
                send_all(connection, "error that key cannot be used\n");
            } else {
                caller->public_key = *new_public_key;
                caller->state = MachineState::joined;
                caller->invite_expires_unix_seconds = 0;
                lock.save(machines);
                // The answer travels under the invite's key. The kernel is
                // only told about the new key once the caller has hung up,
                // or the answer would be sealed with a key it no longer has.
                send_all(connection, "ok\n");
                shutdown(connection, SHUT_WR);
                wait_for_hangup(connection);
                apply_peers(settings_.interface_name, plan_coordinator_peers(settings_, machines));
                refresh_hosts(settings_, list_host_entries(machines));
                log_line(caller->name + " joined as " + format_address(caller->address));
            }
            return;
        }

        send_all(connection, "error unknown request\n");
    }

    Paths paths_;
    Settings settings_;
    std::mutex sync_mutex_;
};

}  // namespace

void install_stop_signals() {
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);
}

bool is_stop_requested() {
    return stop_requested;
}

void sleep_unless_stopped(int seconds) {
    const int steps_per_second = 5;
    for (int step = 0; step < seconds * steps_per_second && !is_stop_requested(); ++step) {
        std::this_thread::sleep_for(std::chrono::milliseconds(MILLISECONDS_PER_SECOND / steps_per_second));
    }
}

void bring_up_tunnel(const Settings& settings, const Key& private_key) {
    create_tunnel(settings.interface_name);
    set_tunnel_identity(settings.interface_name, private_key, settings.listen_port);
    configure_tunnel_address(settings.interface_name, settings.address, settings.network, settings.mtu_bytes);
}

std::vector<TunnelPeer> plan_coordinator_peers(const Settings& settings, const std::vector<Machine>& machines) {
    std::vector<TunnelPeer> peers;
    for (const Machine& machine : machines) {
        if (machine.address == settings.address) continue;
        TunnelPeer peer;
        peer.public_key = machine.public_key;
        peer.allowed_networks = {network_of_one(machine.address)};
        // No endpoint and no keepalive: every machine calls the coordinator
        // and keeps the path open from its side.
        peers.push_back(peer);
    }
    return peers;
}

std::vector<TunnelPeer> plan_agent_peers(const Settings& settings, const std::string& coordinator_endpoint,
                                         const NetworkView& view, const std::map<Key, PathHealth>& health,
                                         int64_t now_unix_seconds) {
    std::vector<TunnelPeer> peers;

    TunnelPeer coordinator;
    coordinator.public_key = settings.coordinator_public_key;
    coordinator.endpoint = coordinator_endpoint;
    coordinator.allowed_networks = {settings.network};
    coordinator.keepalive_seconds = KEEPALIVE_SECONDS;
    peers.push_back(coordinator);

    for (const MachineView& machine : view.machines) {
        const bool is_this_machine = machine.address == settings.address;
        const bool is_coordinator = machine.public_key == settings.coordinator_public_key;
        // Without an endpoint there is nowhere to try. It stays reachable through the coordinator.
        if (is_this_machine || is_coordinator || machine.endpoint.empty()) continue;

        TunnelPeer peer;
        peer.public_key = machine.public_key;
        peer.endpoint = machine.endpoint;
        peer.keepalive_seconds = KEEPALIVE_SECONDS;
        const auto found = health.find(machine.public_key);
        if (found != health.end() && is_path_alive(found->second, now_unix_seconds)) {
            peer.allowed_networks = {network_of_one(machine.address)};
        }
        peers.push_back(peer);
    }
    return peers;
}

NetworkView build_network_view(const Settings& settings, const std::vector<Machine>& machines,
                               const TunnelState& tunnel, int64_t now_unix_seconds) {
    NetworkView view;
    view.network_name = settings.network_name;
    view.network = settings.network;
    for (const Machine& machine : machines) {
        if (machine.state != MachineState::joined) continue;
        MachineView machine_view;
        machine_view.name = machine.name;
        machine_view.address = machine.address;
        machine_view.public_key = machine.public_key;
        if (machine.address == settings.address) {
            // The coordinator itself. Agents already hold its endpoint.
            machine_view.seen_seconds_ago = 0;
        } else {
            const auto peer = std::find_if(tunnel.peers.begin(), tunnel.peers.end(),
                                           [&](const TunnelPeer& candidate) { return candidate.public_key == machine.public_key; });
            if (peer != tunnel.peers.end() && peer->last_handshake_unix_seconds > 0) {
                machine_view.endpoint = peer->endpoint;
                machine_view.seen_seconds_ago = std::max<int64_t>(0, now_unix_seconds - peer->last_handshake_unix_seconds);
            }
        }
        view.machines.push_back(machine_view);
    }
    return view;
}

void run_coordinator(const Paths& paths, const Settings& settings) {
    bring_up_tunnel(settings, load_private_key(paths));
    enable_forwarding(settings.interface_name);

    Coordinator coordinator(paths, settings);
    coordinator.sync();
    notify_service_ready();
    log_line("coordinating the " + settings.network_name + " network (" + format_network(settings.network) + ") as " +
             settings.name + ", " + format_address(settings.address));

    std::thread api_thread([&] {
        try {
            coordinator.serve_until_stopped();
        } catch (const HawserError& error) {
            log_line(error.what());
            stop_requested = true;
        }
    });
    while (!is_stop_requested()) {
        try {
            coordinator.sync();
        } catch (const HawserError& error) {
            log_line(error.what());
        }
        sleep_unless_stopped(COORDINATOR_SYNC_INTERVAL_SECONDS);
    }
    api_thread.join();
}

void run_agent(const Paths& paths, const Settings& settings) {
    bring_up_tunnel(settings, load_private_key(paths));
    notify_service_ready();
    log_line("joining the " + settings.network_name + " network as " + settings.name + ", " + format_address(settings.address));

    std::string coordinator_endpoint;
    std::optional<NetworkView> view;
    std::map<Key, PathHealth> health;
    std::set<Key> direct_keys;
    int64_t last_fetch_unix_seconds = 0;
    int64_t next_resolve_unix_seconds = 0;
    bool is_coordinator_reachable = true;

    while (!is_stop_requested()) {
        const int64_t now = read_unix_seconds();
        try {
            const TunnelState tunnel = read_tunnel(settings.interface_name);
            for (const TunnelPeer& peer : tunnel.peers) {
                observe_path(health[peer.public_key], peer.last_handshake_unix_seconds, peer.received_bytes, now);
            }
            std::erase_if(health, [&](const auto& entry) {
                return std::none_of(tunnel.peers.begin(), tunnel.peers.end(),
                                    [&](const TunnelPeer& peer) { return peer.public_key == entry.first; });
            });

            // Look the coordinator up at the start, and again whenever it has
            // gone quiet, since a name may now point somewhere else.
            const auto coordinator_health = health.find(settings.coordinator_public_key);
            const bool is_coordinator_quiet =
                coordinator_health == health.end() ||
                now - coordinator_health->second.last_heard_unix_seconds > COORDINATOR_SILENCE_SECONDS;
            bool replaces_coordinator_endpoint = false;
            if ((coordinator_endpoint.empty() || is_coordinator_quiet) && now >= next_resolve_unix_seconds) {
                try {
                    coordinator_endpoint = resolve_endpoint(settings.coordinator_endpoint);
                    replaces_coordinator_endpoint = true;
                    next_resolve_unix_seconds = now + COORDINATOR_SILENCE_SECONDS;
                } catch (const HawserError& error) {
                    log_line(error.what());
                    next_resolve_unix_seconds = now + RESOLVE_RETRY_SECONDS;
                }
            }

            if (now - last_fetch_unix_seconds >= FETCH_INTERVAL_SECONDS) {
                last_fetch_unix_seconds = now;
                try {
                    view = parse_network_view(
                        call_coordinator(settings.coordinator_address, settings.api_port, "peers", API_TIMEOUT_SECONDS));
                    if (!is_coordinator_reachable) log_line("the coordinator is answering again");
                    is_coordinator_reachable = true;
                } catch (const HawserError& error) {
                    // Carry on with the last list. Machines that talk directly keep doing so.
                    if (is_coordinator_reachable) log_line(error.what());
                    is_coordinator_reachable = false;
                }
            }

            std::vector<TunnelPeer> wanted;
            if (view) {
                wanted = plan_agent_peers(settings, coordinator_endpoint, *view, health, now);
            } else {
                // No list yet. Keep whatever peers are there, so restarting
                // while the coordinator is away cuts nothing that works.
                wanted = plan_agent_peers(settings, coordinator_endpoint, NetworkView{}, health, now);
                for (const TunnelPeer& peer : tunnel.peers) {
                    if (peer.public_key != settings.coordinator_public_key) wanted.push_back(peer);
                }
            }
            wanted.front().replaces_endpoint = replaces_coordinator_endpoint;
            apply_peers(settings.interface_name, wanted);

            if (view) {
                std::set<Key> now_direct_keys;
                std::vector<HostEntry> entries;
                for (const MachineView& machine : view->machines) {
                    entries.push_back(HostEntry{machine.name, machine.address});
                    const auto peer = std::find_if(wanted.begin(), wanted.end(), [&](const TunnelPeer& candidate) {
                        return candidate.public_key == machine.public_key;
                    });
                    const bool is_direct = peer != wanted.end() && !peer->allowed_networks.empty() &&
                                           machine.public_key != settings.coordinator_public_key;
                    if (is_direct) now_direct_keys.insert(machine.public_key);
                    if (is_direct != direct_keys.contains(machine.public_key)) {
                        log_line(machine.name + (is_direct ? ": connected directly" : ": going through the coordinator"));
                    }
                }
                direct_keys = now_direct_keys;
                refresh_hosts(settings, entries);
            }
        } catch (const HawserError& error) {
            log_line(error.what());
        }
        sleep_unless_stopped(OBSERVE_INTERVAL_SECONDS);
    }
}

}  // namespace hawser
