#include "address.h"
#include "test.h"
#include "text.h"

using namespace hawser;

TEST(an_address_survives_being_written_and_read_back) {
    const auto address = parse_address("10.77.0.5");
    CHECK(address.has_value());
    CHECK(format_address(*address) == "10.77.0.5");
}

TEST(text_that_is_not_an_address_is_refused) {
    CHECK(!parse_address("10.77.0"));
    CHECK(!parse_address("10.77.0.256"));
    CHECK(!parse_address("vps"));
    CHECK(!parse_address(""));
}

TEST(a_network_knows_which_addresses_are_inside_it) {
    const auto network = parse_network("10.77.0.0/24");
    CHECK(network.has_value());
    CHECK(network->contains(*parse_address("10.77.0.200")));
    CHECK(!network->contains(*parse_address("10.77.1.1")));
    CHECK(format_network(*network) == "10.77.0.0/24");
}

TEST(a_network_keeps_its_own_and_its_broadcast_address_back) {
    const auto network = parse_network("10.77.0.0/24");
    CHECK(format_address(network->first_host()) == "10.77.0.1");
    CHECK(format_address(network->last_host()) == "10.77.0.254");
}

TEST(a_network_written_with_host_bits_is_refused_not_guessed_at) {
    // 10.77.0.5/24 could mean the network or the machine. Say no.
    CHECK(!parse_network("10.77.0.5/24"));
}

TEST(a_network_too_small_or_too_large_is_refused) {
    CHECK(!parse_network("10.77.0.0/31"));
    CHECK(!parse_network("10.77.0.0/32"));
    CHECK(!parse_network("0.0.0.0/0"));
    CHECK(!parse_network("10.77.0.0"));
    CHECK(!parse_network("10.77.0.0/x"));
    CHECK(!parse_network("10.77.0.0/24x"));
}

TEST(the_carrier_grade_range_tailscale_uses_is_accepted) {
    const auto network = parse_network("100.64.0.0/10");
    CHECK(network.has_value());
    CHECK(network->contains(*parse_address("100.100.101.32")));
    CHECK(!network->contains(*parse_address("100.128.0.1")));
}

TEST(a_network_of_one_routes_exactly_that_address) {
    const Ipv4Network one = network_of_one(*parse_address("10.77.0.5"));
    CHECK(format_network(one) == "10.77.0.5/32");
    CHECK(one.contains(*parse_address("10.77.0.5")));
    CHECK(!one.contains(*parse_address("10.77.0.6")));
}
