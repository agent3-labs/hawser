#include "machines.h"
#include "test.h"
#include "text.h"

using namespace hawser;

namespace {

Machine make_machine(const std::string& name, const std::string& address, MachineState state, int64_t expires = 0) {
    return Machine{name, *parse_address(address), derive_public_key(generate_private_key()), state, expires};
}

}  // namespace

TEST(the_machine_list_survives_being_written_and_read_back) {
    const std::vector<Machine> machines = {
        make_machine("vps", "10.77.0.1", MachineState::joined),
        make_machine("build-box", "10.77.0.2", MachineState::invited, 1790000000),
    };
    CHECK(parse_machines(format_machines(machines)) == machines);
}

TEST(a_damaged_machine_line_is_refused_whole) {
    // Skipping a bad line would quietly drop a machine from the network.
    CHECK_THROWS(parse_machines("vps 10.77.0.1 not-a-key joined 0\n"));
    CHECK_THROWS(parse_machines("vps 10.77.0.1\n"));
    const std::string key = format_key(derive_public_key(generate_private_key()));
    CHECK_THROWS(parse_machines("vps 10.77.0.1 " + key + " asleep 0\n"));
    CHECK_THROWS(parse_machines("Not_A_Name 10.77.0.1 " + key + " joined 0\n"));
}

TEST(names_are_single_dns_labels) {
    CHECK(is_valid_name("vps"));
    CHECK(is_valid_name("build-box-2"));
    CHECK(!is_valid_name(""));
    CHECK(!is_valid_name("Workstation"));
    CHECK(!is_valid_name("my.machine"));
    CHECK(!is_valid_name("-leading"));
    CHECK(!is_valid_name("trailing-"));
    CHECK(!is_valid_name("has space"));
    CHECK(!is_valid_name(std::string(64, 'a')));
    CHECK(is_valid_name(std::string(63, 'a')));
}

TEST(a_new_machine_gets_the_lowest_address_nobody_holds) {
    const Ipv4Network network = *parse_network("10.77.0.0/29");
    std::vector<Machine> machines = {
        make_machine("vps", "10.77.0.1", MachineState::joined),
        make_machine("c", "10.77.0.3", MachineState::joined),
    };
    CHECK(format_address(*find_free_address(network, machines)) == "10.77.0.2");
    machines.push_back(make_machine("b", "10.77.0.2", MachineState::invited, 1));
    // An invite holds its address even before it is used.
    CHECK(format_address(*find_free_address(network, machines)) == "10.77.0.4");
}

TEST(a_full_network_hands_out_nothing) {
    // A /30 has two addresses a machine can hold.
    const Ipv4Network network = *parse_network("10.77.0.0/30");
    const std::vector<Machine> machines = {
        make_machine("a", "10.77.0.1", MachineState::joined),
        make_machine("b", "10.77.0.2", MachineState::joined),
    };
    CHECK(!find_free_address(network, machines));
}

TEST(only_unused_invites_past_their_time_are_dropped) {
    const std::vector<Machine> machines = {
        make_machine("vps", "10.77.0.1", MachineState::joined),
        make_machine("late", "10.77.0.2", MachineState::invited, 100),
        make_machine("in-time", "10.77.0.3", MachineState::invited, 300),
    };
    const std::vector<Machine> remaining = drop_expired_invites(machines, 200);
    CHECK(remaining.size() == 2);
    CHECK(remaining[0].name == "vps");
    CHECK(remaining[1].name == "in-time");
}
