#include "machines.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstring>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include "text.h"

namespace hawser {

namespace {

// The longest a DNS label may be.
const size_t NAME_LENGTH_MAX = 63;
const size_t MACHINE_FIELD_COUNT = 5;

}  // namespace

bool is_valid_name(std::string_view name) {
    if (name.empty() || name.size() > NAME_LENGTH_MAX) return false;
    if (name.front() == '-' || name.back() == '-') return false;
    return std::all_of(name.begin(), name.end(), [](char character) {
        return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '-';
    });
}

std::string format_machines(const std::vector<Machine>& machines) {
    std::string text = "# name address public_key state invite_expires_unix_seconds\n";
    for (const Machine& machine : machines) {
        text += machine.name + " " + format_address(machine.address) + " " + format_key(machine.public_key) + " " +
                (machine.state == MachineState::joined ? "joined" : "invited") + " " +
                std::to_string(machine.invite_expires_unix_seconds) + "\n";
    }
    return text;
}

std::vector<Machine> parse_machines(const std::string& text) {
    std::vector<Machine> machines;
    for (const std::string& raw_line : split_lines(text)) {
        const std::string line = trim(raw_line);
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> fields = split_words(line);
        if (fields.size() != MACHINE_FIELD_COUNT) throw HawserError("machines file: cannot read the line '" + line + "'");

        Machine machine;
        machine.name = fields[0];
        const auto address = parse_address(fields[1]);
        const auto public_key = parse_key(fields[2]);
        int64_t expires = 0;
        const auto [end, error] = std::from_chars(fields[4].data(), fields[4].data() + fields[4].size(), expires);
        const bool is_state_known = fields[3] == "joined" || fields[3] == "invited";
        if (!is_valid_name(machine.name) || !address || !public_key || !is_state_known || error != std::errc{}) {
            throw HawserError("machines file: cannot read the line '" + line + "'");
        }
        machine.address = *address;
        machine.public_key = *public_key;
        machine.state = fields[3] == "joined" ? MachineState::joined : MachineState::invited;
        machine.invite_expires_unix_seconds = expires;
        machines.push_back(machine);
    }
    return machines;
}

std::optional<Ipv4Address> find_free_address(const Ipv4Network& network, const std::vector<Machine>& machines) {
    for (uint32_t value = network.first_host().value; value <= network.last_host().value; ++value) {
        const Ipv4Address candidate{value};
        const bool is_taken = std::any_of(machines.begin(), machines.end(),
                                          [&](const Machine& machine) { return machine.address == candidate; });
        if (!is_taken) return candidate;
    }
    return std::nullopt;
}

std::vector<Machine> drop_expired_invites(const std::vector<Machine>& machines, int64_t now_unix_seconds) {
    std::vector<Machine> remaining;
    for (const Machine& machine : machines) {
        const bool is_expired =
            machine.state == MachineState::invited && machine.invite_expires_unix_seconds <= now_unix_seconds;
        if (!is_expired) remaining.push_back(machine);
    }
    return remaining;
}

MachinesLock::MachinesLock(const Paths& paths) : paths_(paths) {
    make_directory(paths_.state_dir, 0700);
    lock_file_ = open(paths_.machines_lock_file().c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock_file_ < 0 || flock(lock_file_, LOCK_EX) != 0) {
        throw HawserError("cannot lock " + paths_.machines_lock_file() + ": " + std::strerror(errno));
    }
}

MachinesLock::~MachinesLock() {
    if (lock_file_ >= 0) close(lock_file_);
}

std::vector<Machine> MachinesLock::load() const {
    if (!file_exists(paths_.machines_file())) return {};
    return parse_machines(read_file(paths_.machines_file()));
}

void MachinesLock::save(const std::vector<Machine>& machines) const {
    write_file(paths_.machines_file(), format_machines(machines), 0600);
}

}  // namespace hawser
