// The long-running half of hawser: `hawser up`.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "key.h"
#include "machines.h"
#include "path.h"
#include "protocol.h"
#include "settings.h"
#include "tunnel.h"

namespace hawser {

// Stops the loops below on SIGINT or SIGTERM.
void install_stop_signals();
bool is_stop_requested();
// Returns early when asked to stop.
void sleep_unless_stopped(int seconds);

// Creates the interface if needed and gives it this machine's key, port and
// address. Safe on an interface that is already up: its peers are kept.
void bring_up_tunnel(const Settings& settings, const Key& private_key);

// Runs until asked to stop. The interface is left up on the way out, so a
// restart of the daemon does not cut connections that are working.
void run_coordinator(const Paths& paths, const Settings& settings);
void run_agent(const Paths& paths, const Settings& settings);

// The peers a coordinator's interface should have: every machine, each
// routed its own address.
std::vector<TunnelPeer> plan_coordinator_peers(const Settings& settings, const std::vector<Machine>& machines);

// The peers an agent's interface should have. The coordinator is routed the
// whole network; another machine is routed its own address only while its
// direct path is alive, and that narrower route then wins.
std::vector<TunnelPeer> plan_agent_peers(const Settings& settings, const std::string& coordinator_endpoint,
                                         const NetworkView& view, const std::map<Key, PathHealth>& health,
                                         int64_t now_unix_seconds);

// What the coordinator tells an agent about the network.
NetworkView build_network_view(const Settings& settings, const std::vector<Machine>& machines,
                               const TunnelState& tunnel, int64_t now_unix_seconds);

}  // namespace hawser
