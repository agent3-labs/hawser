#include "commands.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <iostream>
#include <map>
#include <thread>

#include <unistd.h>

#include "daemon.h"
#include "hosts.h"
#include "invite.h"
#include "machines.h"
#include "protocol.h"
#include "settings.h"
#include "text.h"
#include "tunnel.h"

namespace hawser {

namespace {

const int DEFAULT_INVITE_VALID_HOURS = 24;
const int SECONDS_PER_HOUR = 3600;
// How long `join` keeps trying to reach the coordinator through a tunnel that
// has only just been made.
const int JOIN_WAIT_SECONDS = 60;
const int JOIN_RETRY_SECONDS = 2;
const int API_TIMEOUT_SECONDS = 5;
// Lets the last packets of the join leave under the invite's key before the
// interface is given the machine's own.
const int KEY_SWAP_PAUSE_MILLISECONDS = 500;

// "--name value" pairs and the bare words between them.
struct Arguments {
    std::map<std::string, std::string> options;
    std::vector<std::string> words;
};

Arguments parse_arguments(const std::vector<std::string>& raw, const std::vector<std::string>& known_options) {
    Arguments arguments;
    for (size_t index = 0; index < raw.size(); ++index) {
        if (raw[index].rfind("--", 0) != 0) {
            arguments.words.push_back(raw[index]);
            continue;
        }
        const std::string name = raw[index].substr(2);
        if (std::find(known_options.begin(), known_options.end(), name) == known_options.end()) {
            throw HawserError("unknown option --" + name);
        }
        if (index + 1 >= raw.size()) throw HawserError("--" + name + " needs a value");
        arguments.options[name] = raw[++index];
    }
    return arguments;
}

const std::string& require_option(const Arguments& arguments, const std::string& name) {
    const auto found = arguments.options.find(name);
    if (found == arguments.options.end() || found->second.empty()) throw HawserError("--" + name + " is required");
    return found->second;
}

int read_number_option(const Arguments& arguments, const std::string& name, int fallback) {
    const auto found = arguments.options.find(name);
    if (found == arguments.options.end()) return fallback;
    int number = 0;
    const std::string& text = found->second;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (error != std::errc{} || end != text.data() + text.size() || number <= 0) {
        throw HawserError("--" + name + " must be a number above zero, not '" + text + "'");
    }
    return number;
}

void require_root() {
    if (geteuid() != 0) throw HawserError("run this as root: hawser changes the machine's network");
}

void require_coordinator(const Settings& settings, const std::string& command) {
    if (settings.role != Role::coordinator) {
        throw HawserError("'" + command + "' is run on the coordinator. This machine is an agent.");
    }
}

std::string describe_seconds_ago(int64_t seconds) {
    if (seconds < 60) return std::to_string(seconds) + "s ago";
    if (seconds < SECONDS_PER_HOUR) return std::to_string(seconds / 60) + "m ago";
    return std::to_string(seconds / SECONDS_PER_HOUR) + "h ago";
}

void print_table(const std::vector<std::vector<std::string>>& rows) {
    std::vector<size_t> widths;
    for (const auto& row : rows) {
        widths.resize(std::max(widths.size(), row.size()), 0);
        for (size_t column = 0; column < row.size(); ++column) widths[column] = std::max(widths[column], row[column].size());
    }
    for (const auto& row : rows) {
        std::string line;
        for (size_t column = 0; column < row.size(); ++column) {
            line += row[column];
            if (column + 1 < row.size()) line += std::string(widths[column] - row[column].size() + 2, ' ');
        }
        std::cout << line << "\n";
    }
}

}  // namespace

void run_init(const std::vector<std::string>& raw_arguments) {
    require_root();
    const Arguments arguments = parse_arguments(
        raw_arguments, {"network-name", "network", "name", "endpoint", "address", "listen-port", "api-port", "interface", "mtu"});
    const Paths paths = read_paths();
    if (file_exists(paths.settings_file())) {
        throw HawserError("this machine is already set up (" + paths.settings_file() + " exists)");
    }

    Settings settings;
    settings.role = Role::coordinator;
    settings.network_name = require_option(arguments, "network-name");
    settings.name = require_option(arguments, "name");
    if (!is_valid_name(settings.network_name) || !is_valid_name(settings.name)) {
        throw HawserError("names are lower-case letters, digits and dashes, such as 'home' or 'build-box'");
    }

    const auto network = parse_network(require_option(arguments, "network"));
    if (!network) throw HawserError("--network must be a network such as 10.77.0.0/24, with a prefix from /8 to /30");
    settings.network = *network;

    settings.address = network->first_host();
    if (arguments.options.contains("address")) {
        const auto address = parse_address(arguments.options.at("address"));
        const bool is_usable = address && network->contains(*address) && *address >= network->first_host() &&
                               *address <= network->last_host();
        if (!is_usable) throw HawserError("--address must be an address a machine can hold inside " + format_network(*network));
        settings.address = *address;
    }

    settings.coordinator_endpoint = require_option(arguments, "endpoint");
    // The port machines dial is the port this machine listens on, unless a
    // router in front forwards one to the other.
    settings.listen_port = read_number_option(arguments, "listen-port", parse_host_and_port(settings.coordinator_endpoint).port);
    settings.api_port = read_number_option(arguments, "api-port", DEFAULT_API_PORT);
    settings.mtu_bytes = read_number_option(arguments, "mtu", DEFAULT_MTU_BYTES);
    if (arguments.options.contains("interface")) settings.interface_name = arguments.options.at("interface");

    const Key private_key = generate_private_key();
    save_private_key(paths, private_key);
    save_settings(paths, settings);
    {
        const MachinesLock lock(paths);
        lock.save({Machine{settings.name, settings.address, derive_public_key(private_key), MachineState::joined, 0}});
    }

    std::cout << "Created the " << settings.network_name << " network, " << format_network(settings.network) << ".\n"
              << "This machine is its coordinator: " << settings.name << ", " << format_address(settings.address) << ".\n"
              << "Machines will dial " << settings.coordinator_endpoint << ", so UDP port " << settings.listen_port
              << " must be reachable from outside.\n\n"
              << "Start it:        hawser up\n"
              << "Add a machine:   hawser invite <name>\n";
}

void run_invite(const std::vector<std::string>& raw_arguments) {
    require_root();
    const Arguments arguments = parse_arguments(raw_arguments, {"address", "valid-hours"});
    if (arguments.words.size() != 1) throw HawserError("usage: hawser invite <name> [--address <address>] [--valid-hours <hours>]");
    const std::string& name = arguments.words[0];
    if (!is_valid_name(name)) throw HawserError("a name is lower-case letters, digits and dashes, such as 'build-box'");

    const Paths paths = read_paths();
    const Settings settings = load_settings(paths);
    require_coordinator(settings, "invite");
    const int valid_hours = read_number_option(arguments, "valid-hours", DEFAULT_INVITE_VALID_HOURS);
    const Key coordinator_public_key = derive_public_key(load_private_key(paths));

    Invite invite;
    {
        const MachinesLock lock(paths);
        std::vector<Machine> machines = drop_expired_invites(lock.load(), read_unix_seconds());
        if (std::any_of(machines.begin(), machines.end(), [&](const Machine& machine) { return machine.name == name; })) {
            throw HawserError("there is already a machine called " + name + ". Remove it first with: hawser remove " + name);
        }

        std::optional<Ipv4Address> address = find_free_address(settings.network, machines);
        if (arguments.options.contains("address")) {
            address = parse_address(arguments.options.at("address"));
            const bool is_usable = address && settings.network.contains(*address) && *address >= settings.network.first_host() &&
                                   *address <= settings.network.last_host();
            const bool is_taken = address && std::any_of(machines.begin(), machines.end(), [&](const Machine& machine) {
                return machine.address == *address;
            });
            if (!is_usable || is_taken) {
                throw HawserError("--address must be a free address inside " + format_network(settings.network));
            }
        }
        if (!address) throw HawserError("every address in " + format_network(settings.network) + " is taken");

        invite.one_time_private_key = generate_private_key();
        invite.expires_unix_seconds = read_unix_seconds() + int64_t{valid_hours} * SECONDS_PER_HOUR;
        machines.push_back(Machine{name, *address, derive_public_key(invite.one_time_private_key), MachineState::invited,
                                   invite.expires_unix_seconds});
        lock.save(machines);
        // Tell the kernel now, so the invite works the moment it is printed.
        if (tunnel_exists(settings.interface_name)) {
            apply_peers(settings.interface_name, plan_coordinator_peers(settings, machines));
        }
        invite.address = *address;
    }

    invite.network_name = settings.network_name;
    invite.network = settings.network;
    invite.name = name;
    invite.coordinator_endpoint = settings.coordinator_endpoint;
    invite.coordinator_address = settings.address;
    invite.coordinator_public_key = coordinator_public_key;
    invite.api_port = settings.api_port;
    invite.mtu_bytes = settings.mtu_bytes;

    // The explanation goes to standard error, so `hawser invite x > file`
    // captures the invite and nothing else.
    std::cerr << name << " will be " << format_address(invite.address) << ". On that machine, as root, within " << valid_hours
              << " hours:\n\n  hawser join <the line below>\n\n"
              << "The line works once and holds a key. Send it the way you would send a password.\n\n";
    std::cout << encode_invite(invite) << "\n";
}

void run_join(const std::vector<std::string>& raw_arguments) {
    require_root();
    const Arguments arguments = parse_arguments(raw_arguments, {"listen-port", "interface"});
    if (arguments.words.size() != 1) throw HawserError("usage: hawser join <invite> [--listen-port <port>] [--interface <name>]");
    const Paths paths = read_paths();
    if (file_exists(paths.settings_file())) {
        throw HawserError("this machine is already set up (" + paths.settings_file() + " exists). Run 'hawser down' and remove that folder to join again.");
    }

    const Invite invite = decode_invite(arguments.words[0]);
    if (invite.expires_unix_seconds <= read_unix_seconds()) throw HawserError("this invite has expired. Ask for a new one.");

    Settings settings;
    settings.role = Role::agent;
    settings.network_name = invite.network_name;
    settings.network = invite.network;
    settings.name = invite.name;
    settings.address = invite.address;
    settings.listen_port = read_number_option(arguments, "listen-port", DEFAULT_LISTEN_PORT);
    settings.api_port = invite.api_port;
    settings.mtu_bytes = invite.mtu_bytes;
    settings.coordinator_endpoint = invite.coordinator_endpoint;
    settings.coordinator_address = invite.coordinator_address;
    settings.coordinator_public_key = invite.coordinator_public_key;
    if (arguments.options.contains("interface")) settings.interface_name = arguments.options.at("interface");

    const bool was_tunnel_there = tunnel_exists(settings.interface_name);
    const Key private_key = generate_private_key();
    try {
        // Reach the coordinator with the invite's key, and hand it this machine's own.
        bring_up_tunnel(settings, invite.one_time_private_key);
        const std::string coordinator_endpoint = resolve_endpoint(settings.coordinator_endpoint);
        apply_peers(settings.interface_name, plan_agent_peers(settings, coordinator_endpoint, NetworkView{}, {}, read_unix_seconds()));

        const std::string request = "redeem " + format_key(derive_public_key(private_key));
        const int64_t give_up_unix_seconds = read_unix_seconds() + JOIN_WAIT_SECONDS;
        std::string answer;
        while (true) {
            try {
                answer = trim(call_coordinator(settings.coordinator_address, settings.api_port, request, API_TIMEOUT_SECONDS));
                break;
            } catch (const HawserError& error) {
                if (read_unix_seconds() >= give_up_unix_seconds) {
                    throw HawserError(std::string(error.what()) + ". Is the coordinator running, and is its UDP port open?");
                }
                std::this_thread::sleep_for(std::chrono::seconds(JOIN_RETRY_SECONDS));
            }
        }
        if (answer != "ok") {
            throw HawserError("the coordinator refused: " + (answer.rfind("error ", 0) == 0 ? answer.substr(6) : answer));
        }
    } catch (...) {
        if (!was_tunnel_there) remove_tunnel(settings.interface_name);
        throw;
    }

    save_private_key(paths, private_key);
    save_settings(paths, settings);
    std::this_thread::sleep_for(std::chrono::milliseconds(KEY_SWAP_PAUSE_MILLISECONDS));
    set_tunnel_identity(settings.interface_name, private_key, settings.listen_port);

    std::cout << "Joined the " << settings.network_name << " network as " << settings.name << ", "
              << format_address(settings.address) << ".\n"
              << "Start it:  hawser up\n";
}

void run_up(const std::vector<std::string>& raw_arguments) {
    require_root();
    parse_arguments(raw_arguments, {});
    const Paths paths = read_paths();
    const Settings settings = load_settings(paths);
    install_stop_signals();
    if (settings.role == Role::coordinator) run_coordinator(paths, settings);
    else run_agent(paths, settings);
}

void run_down(const std::vector<std::string>& raw_arguments) {
    require_root();
    parse_arguments(raw_arguments, {});
    const Settings settings = load_settings(read_paths());
    remove_tunnel(settings.interface_name);
    if (settings.manages_hosts) update_hosts_file(settings.hosts_file, settings.network_name, {});
    std::cout << "Removed " << settings.interface_name << ". This machine has left the network until 'hawser up' runs again.\n";
}

void run_status(const std::vector<std::string>& raw_arguments) {
    require_root();
    parse_arguments(raw_arguments, {});
    const Paths paths = read_paths();
    const Settings settings = load_settings(paths);
    if (!tunnel_exists(settings.interface_name)) {
        std::cout << settings.interface_name << " is down. Start it with: hawser up\n";
        return;
    }
    const TunnelState tunnel = read_tunnel(settings.interface_name);
    const int64_t now = read_unix_seconds();
    const auto find_peer = [&](const Key& key) {
        return std::find_if(tunnel.peers.begin(), tunnel.peers.end(),
                            [&](const TunnelPeer& peer) { return peer.public_key == key; });
    };
    const auto describe_handshake = [&](const Key& key) -> std::string {
        const auto peer = find_peer(key);
        if (peer == tunnel.peers.end() || peer->last_handshake_unix_seconds == 0) return "never";
        return describe_seconds_ago(std::max<int64_t>(0, now - peer->last_handshake_unix_seconds));
    };

    std::cout << "Network " << settings.network_name << ", " << format_network(settings.network) << ". This machine: "
              << settings.name << ", " << format_address(settings.address) << ".\n\n";

    if (settings.role == Role::coordinator) {
        std::vector<Machine> machines;
        {
            const MachinesLock lock(paths);
            machines = lock.load();
        }
        std::vector<std::vector<std::string>> rows = {{"NAME", "ADDRESS", "STATE", "LAST HEARD", "ENDPOINT"}};
        for (const Machine& machine : machines) {
            if (machine.address == settings.address) {
                rows.push_back({machine.name, format_address(machine.address), "coordinator", "-", "-"});
                continue;
            }
            const auto peer = find_peer(machine.public_key);
            const std::string endpoint = peer != tunnel.peers.end() && !peer->endpoint.empty() ? peer->endpoint : "-";
            rows.push_back({machine.name, format_address(machine.address),
                            machine.state == MachineState::joined ? "joined" : "invited", describe_handshake(machine.public_key), endpoint});
        }
        print_table(rows);
        return;
    }

    NetworkView view;
    try {
        view = parse_network_view(call_coordinator(settings.coordinator_address, settings.api_port, "peers", API_TIMEOUT_SECONDS));
    } catch (const HawserError& error) {
        std::cout << "The coordinator is not answering: " << error.what() << "\n";
        return;
    }
    std::string coordinator_name = "the coordinator";
    for (const MachineView& machine : view.machines) {
        if (machine.public_key == settings.coordinator_public_key) coordinator_name = machine.name;
    }
    std::vector<std::vector<std::string>> rows = {{"NAME", "ADDRESS", "PATH", "LAST HEARD"}};
    for (const MachineView& machine : view.machines) {
        std::string path = "through " + coordinator_name;
        std::string heard = "-";
        if (machine.address == settings.address) {
            path = "this machine";
        } else if (machine.public_key == settings.coordinator_public_key) {
            path = "direct (coordinator)";
            heard = describe_handshake(machine.public_key);
        } else {
            const auto peer = find_peer(machine.public_key);
            if (peer != tunnel.peers.end() && !peer->allowed_networks.empty()) {
                path = "direct";
                heard = describe_handshake(machine.public_key);
            }
        }
        rows.push_back({machine.name, format_address(machine.address), path, heard});
    }
    print_table(rows);
}

void run_remove(const std::vector<std::string>& raw_arguments) {
    require_root();
    const Arguments arguments = parse_arguments(raw_arguments, {});
    if (arguments.words.size() != 1) throw HawserError("usage: hawser remove <name>");
    const std::string& name = arguments.words[0];
    const Paths paths = read_paths();
    const Settings settings = load_settings(paths);
    require_coordinator(settings, "remove");
    if (name == settings.name) throw HawserError("the coordinator cannot remove itself");

    const MachinesLock lock(paths);
    std::vector<Machine> machines = lock.load();
    const size_t count_before = machines.size();
    std::erase_if(machines, [&](const Machine& machine) { return machine.name == name; });
    if (machines.size() == count_before) throw HawserError("there is no machine called " + name);
    lock.save(machines);
    // Cut it off now, not at the daemon's next look at the file.
    if (tunnel_exists(settings.interface_name)) {
        apply_peers(settings.interface_name, plan_coordinator_peers(settings, machines));
    }
    std::cout << "Removed " << name << ". It is cut off now; the other machines forget it within seconds.\n";
}

}  // namespace hawser
