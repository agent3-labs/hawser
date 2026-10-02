// How machines find each other by name: a block of lines hawser keeps in the
// hosts file, and nothing outside that block is ever touched.

#pragma once

#include <string>
#include <vector>

#include "address.h"

namespace hawser {

struct HostEntry {
    std::string name;
    Ipv4Address address;
};

// The hosts file's text with hawser's block replaced by one listing `entries`.
// Each machine is reachable as `name` and as `name.network_name`. With no
// entries the block is removed altogether.
std::string render_hosts(const std::string& current_text, const std::string& network_name, const std::vector<HostEntry>& entries);

// Brings the hosts file in line, writing only when something changed.
void update_hosts_file(const std::string& path, const std::string& network_name, const std::vector<HostEntry>& entries);

}  // namespace hawser
