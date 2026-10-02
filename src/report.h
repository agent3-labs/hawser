// What `hawser status` knows, as one value that can be printed as a table for
// a person or as JSON for a program.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hawser {

struct StatusMachine {
    std::string name;
    std::string address;
    bool is_coordinator = false;
    // "joined" or "invited". An agent only ever hears of joined machines.
    std::string state;
    // How this machine reaches it: "self", "direct", or "relayed" through the coordinator.
    std::string path;
    // How long ago this machine last had a handshake with it. Nothing when never, or for itself.
    std::optional<int64_t> heard_seconds_ago;
    // Where it was last heard from, as "address:port". Empty when not known.
    std::string endpoint;
};

struct StatusReport {
    std::string interface_name;
    // False when the interface does not exist: the network is down on this machine.
    bool is_up = false;
    // "coordinator" or "agent".
    std::string role;
    std::string network_name;
    std::string network;
    std::string name;
    std::string address;
    // Set on an agent whose coordinator did not answer. The machines are then unknown.
    std::string coordinator_error;
    std::vector<StatusMachine> machines;
};

// One JSON object on one line. Names and values are escaped, so a name can
// hold anything without breaking the output.
std::string format_status_json(const StatusReport& report);

}  // namespace hawser
