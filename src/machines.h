// The coordinator's record of every machine in the network.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "address.h"
#include "key.h"
#include "settings.h"

namespace hawser {

enum class MachineState {
    // Has an address and a one-time key from its invite, and has not joined yet.
    invited,
    // Has swapped the invite's key for one only it holds.
    joined,
};

struct Machine {
    std::string name;
    Ipv4Address address;
    Key public_key;
    MachineState state = MachineState::invited;
    // When an unused invite stops working. Zero once joined.
    int64_t invite_expires_unix_seconds = 0;

    bool operator==(const Machine&) const = default;
};

// A name is one DNS label, because it ends up in the hosts file.
bool is_valid_name(std::string_view name);

std::string format_machines(const std::vector<Machine>& machines);
// Throws HawserError on a line it cannot read.
std::vector<Machine> parse_machines(const std::string& text);

// The lowest address in the network no machine holds, or nothing when full.
std::optional<Ipv4Address> find_free_address(const Ipv4Network& network, const std::vector<Machine>& machines);

// Invites nobody used in time. Returns the machines that remain.
std::vector<Machine> drop_expired_invites(const std::vector<Machine>& machines, int64_t now_unix_seconds);

// Holds the machines file locked for as long as it lives, so the daemon and a
// command run beside it never write over each other.
class MachinesLock {
  public:
    explicit MachinesLock(const Paths& paths);
    ~MachinesLock();
    MachinesLock(const MachinesLock&) = delete;
    MachinesLock& operator=(const MachinesLock&) = delete;

    std::vector<Machine> load() const;
    void save(const std::vector<Machine>& machines) const;

  private:
    Paths paths_;
    int lock_file_ = -1;
};

}  // namespace hawser
