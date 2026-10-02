#include <algorithm>

#include "daemon.h"
#include "test.h"

using namespace hawser;

namespace {

const int64_t NOW = 1790000000;

struct Network {
    Settings coordinator_settings;
    Settings agent_settings;
    Key coordinator_key = derive_public_key(generate_private_key());
    Key agent_key = derive_public_key(generate_private_key());
    Key other_key = derive_public_key(generate_private_key());
    Key far_key = derive_public_key(generate_private_key());
    NetworkView view;

    Network() {
        coordinator_settings.role = Role::coordinator;
        coordinator_settings.network_name = "home";
        coordinator_settings.network = *parse_network("10.77.0.0/24");
        coordinator_settings.name = "vps";
        coordinator_settings.address = *parse_address("10.77.0.1");

        agent_settings = coordinator_settings;
        agent_settings.role = Role::agent;
        agent_settings.name = "workstation";
        agent_settings.address = *parse_address("10.77.0.2");
        agent_settings.coordinator_address = coordinator_settings.address;
        agent_settings.coordinator_public_key = coordinator_key;

        view.network_name = "home";
        view.network = coordinator_settings.network;
        view.machines = {
            MachineView{"vps", *parse_address("10.77.0.1"), coordinator_key, "", 0},
            MachineView{"workstation", *parse_address("10.77.0.2"), agent_key, "203.0.113.7:41820", 3},
            MachineView{"laptop", *parse_address("10.77.0.3"), other_key, "198.51.100.9:51820", 5},
            MachineView{"never-seen", *parse_address("10.77.0.4"), far_key, "", std::nullopt},
        };
    }
};

const TunnelPeer* find_peer(const std::vector<TunnelPeer>& peers, const Key& key) {
    const auto found = std::find_if(peers.begin(), peers.end(), [&](const TunnelPeer& peer) { return peer.public_key == key; });
    return found == peers.end() ? nullptr : &*found;
}

PathHealth make_alive_health() {
    PathHealth health;
    observe_path(health, 0, 0, NOW - 10);
    observe_path(health, NOW - 5, 92, NOW - 4);
    return health;
}

}  // namespace

TEST(an_agent_routes_the_whole_network_to_the_coordinator) {
    const Network network;
    const auto peers = plan_agent_peers(network.agent_settings, "192.0.2.1:51820", network.view, {}, NOW);
    const TunnelPeer* coordinator = find_peer(peers, network.coordinator_key);
    CHECK(coordinator != nullptr);
    CHECK(coordinator->allowed_networks == std::vector<Ipv4Network>{network.agent_settings.network});
    CHECK(coordinator->endpoint == "192.0.2.1:51820");
    CHECK(coordinator->keepalive_seconds == KEEPALIVE_SECONDS);
}

TEST(an_unproven_machine_is_probed_but_given_no_traffic) {
    // This is what stops traffic being sent down a path nobody has shown to work.
    const Network network;
    const auto peers = plan_agent_peers(network.agent_settings, "192.0.2.1:51820", network.view, {}, NOW);
    const TunnelPeer* laptop = find_peer(peers, network.other_key);
    CHECK(laptop != nullptr);
    CHECK(laptop->endpoint == "198.51.100.9:51820");
    CHECK(laptop->keepalive_seconds == KEEPALIVE_SECONDS);
    CHECK(laptop->allowed_networks.empty());
}

TEST(a_machine_whose_direct_path_is_alive_is_routed_its_own_address) {
    const Network network;
    const std::map<Key, PathHealth> health = {{network.other_key, make_alive_health()}};
    const auto peers = plan_agent_peers(network.agent_settings, "192.0.2.1:51820", network.view, health, NOW);
    const TunnelPeer* laptop = find_peer(peers, network.other_key);
    CHECK(laptop->allowed_networks == std::vector<Ipv4Network>{network_of_one(*parse_address("10.77.0.3"))});
}

TEST(a_machine_whose_direct_path_went_quiet_loses_its_route_and_keeps_being_probed) {
    const Network network;
    const std::map<Key, PathHealth> health = {{network.other_key, make_alive_health()}};
    const int64_t later = NOW + DIRECT_PATH_SILENCE_SECONDS + 60;
    const auto peers = plan_agent_peers(network.agent_settings, "192.0.2.1:51820", network.view, health, later);
    const TunnelPeer* laptop = find_peer(peers, network.other_key);
    CHECK(laptop != nullptr);
    CHECK(laptop->allowed_networks.empty());
}

TEST(an_agent_is_never_its_own_peer) {
    const Network network;
    const auto peers = plan_agent_peers(network.agent_settings, "192.0.2.1:51820", network.view, {}, NOW);
    CHECK(find_peer(peers, network.agent_key) == nullptr);
}

TEST(a_machine_with_no_known_endpoint_is_left_to_the_coordinator) {
    const Network network;
    const auto peers = plan_agent_peers(network.agent_settings, "192.0.2.1:51820", network.view, {}, NOW);
    CHECK(find_peer(peers, network.far_key) == nullptr);
    // The coordinator's row in the list does not make it a second peer.
    CHECK(std::count_if(peers.begin(), peers.end(), [&](const TunnelPeer& peer) {
              return peer.public_key == network.coordinator_key;
          }) == 1);
}

TEST(the_coordinator_routes_each_machine_exactly_its_own_address) {
    const Network network;
    const std::vector<Machine> machines = {
        Machine{"vps", *parse_address("10.77.0.1"), network.coordinator_key, MachineState::joined, 0},
        Machine{"workstation", *parse_address("10.77.0.2"), network.agent_key, MachineState::joined, 0},
        Machine{"invited", *parse_address("10.77.0.5"), network.far_key, MachineState::invited, NOW + 100},
    };
    const auto peers = plan_coordinator_peers(network.coordinator_settings, machines);
    CHECK(peers.size() == 2);
    CHECK(find_peer(peers, network.coordinator_key) == nullptr);
    CHECK(find_peer(peers, network.agent_key)->allowed_networks ==
          std::vector<Ipv4Network>{network_of_one(*parse_address("10.77.0.2"))});
    // An invited machine must be reachable, or it could never hand over its key.
    CHECK(find_peer(peers, network.far_key) != nullptr);
}

TEST(agents_are_told_only_about_machines_that_have_joined) {
    const Network network;
    const std::vector<Machine> machines = {
        Machine{"vps", *parse_address("10.77.0.1"), network.coordinator_key, MachineState::joined, 0},
        Machine{"workstation", *parse_address("10.77.0.2"), network.agent_key, MachineState::joined, 0},
        Machine{"laptop", *parse_address("10.77.0.3"), network.other_key, MachineState::joined, 0},
        Machine{"invited", *parse_address("10.77.0.5"), network.far_key, MachineState::invited, NOW + 100},
    };
    TunnelState tunnel;
    TunnelPeer seen;
    seen.public_key = network.agent_key;
    seen.endpoint = "203.0.113.7:41820";
    seen.last_handshake_unix_seconds = NOW - 12;
    TunnelPeer unseen;
    unseen.public_key = network.other_key;
    tunnel.peers = {seen, unseen};

    const NetworkView view = build_network_view(network.coordinator_settings, machines, tunnel, NOW);
    CHECK(view.machines.size() == 3);
    CHECK(view.machines[0].name == "vps");
    CHECK(view.machines[0].seen_seconds_ago == 0);
    CHECK(view.machines[1].endpoint == "203.0.113.7:41820");
    CHECK(view.machines[1].seen_seconds_ago == 12);
    // Joined, but not heard from since the coordinator started.
    CHECK(view.machines[2].endpoint.empty());
    CHECK(!view.machines[2].seen_seconds_ago.has_value());
}
