#include "protocol.h"
#include "test.h"
#include "text.h"

using namespace hawser;

namespace {

NetworkView make_view() {
    NetworkView view;
    view.network_name = "home";
    view.network = *parse_network("10.77.0.0/24");
    view.machines = {
        MachineView{"vps", *parse_address("10.77.0.1"), derive_public_key(generate_private_key()), "", 0},
        MachineView{"workstation", *parse_address("10.77.0.2"), derive_public_key(generate_private_key()), "203.0.113.7:41820", 12},
        MachineView{"laptop", *parse_address("10.77.0.3"), derive_public_key(generate_private_key()), "", std::nullopt},
    };
    return view;
}

}  // namespace

TEST(a_network_view_survives_the_wire) {
    const NetworkView view = make_view();
    CHECK(parse_network_view(format_network_view(view)) == view);
}

TEST(a_machine_never_heard_from_has_no_endpoint_and_no_time) {
    const NetworkView read_back = parse_network_view(format_network_view(make_view()));
    CHECK(read_back.machines[2].endpoint.empty());
    CHECK(!read_back.machines[2].seen_seconds_ago.has_value());
    CHECK(read_back.machines[0].seen_seconds_ago == 0);
}

TEST(an_answer_cut_short_is_refused_whole) {
    // Half a list must not be taken for a list: the missing machines would
    // lose their routes.
    const std::string text = format_network_view(make_view());
    for (size_t length = 0; length + 1 < text.size(); length += 7) {
        CHECK_THROWS(parse_network_view(text.substr(0, length)));
    }
}

TEST(an_answer_that_is_not_a_view_is_refused) {
    CHECK_THROWS(parse_network_view("error join first\n"));
    CHECK_THROWS(parse_network_view("hawser 2\nnetwork home 10.77.0.0/24\nend\n"));
    CHECK_THROWS(parse_network_view("hawser 1\nnetwork home nonsense\nend\n"));
    CHECK_THROWS(parse_network_view("hawser 1\nnetwork home 10.77.0.0/24\nmachine vps 10.77.0.1 badkey - -\nend\n"));
}

TEST(a_network_with_only_the_coordinator_is_a_valid_view) {
    NetworkView view = make_view();
    view.machines.resize(1);
    CHECK(parse_network_view(format_network_view(view)).machines.size() == 1);
}
