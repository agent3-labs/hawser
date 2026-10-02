#include "hosts.h"
#include "test.h"

using namespace hawser;

namespace {

const std::string SYSTEM_HOSTS = "127.0.0.1 localhost\n::1 ip6-localhost\n";

std::vector<HostEntry> make_entries() {
    return {{"vps", *parse_address("10.77.0.1")}, {"workstation", *parse_address("10.77.0.2")}};
}

}  // namespace

TEST(every_machine_gets_a_short_and_a_full_name) {
    const std::string text = render_hosts(SYSTEM_HOSTS, "home", make_entries());
    CHECK(text.find("10.77.0.1 vps.home vps\n") != std::string::npos);
    CHECK(text.find("10.77.0.2 workstation.home workstation\n") != std::string::npos);
}

TEST(lines_that_are_not_hawsers_are_left_exactly_as_they_were) {
    const std::string text = render_hosts(SYSTEM_HOSTS, "home", make_entries());
    CHECK(text.rfind(SYSTEM_HOSTS, 0) == 0);
}

TEST(writing_twice_changes_nothing_the_second_time) {
    const std::string once = render_hosts(SYSTEM_HOSTS, "home", make_entries());
    CHECK(render_hosts(once, "home", make_entries()) == once);
}

TEST(a_machine_that_left_is_taken_out) {
    const std::string before = render_hosts(SYSTEM_HOSTS, "home", make_entries());
    const std::string after = render_hosts(before, "home", {{"vps", *parse_address("10.77.0.1")}});
    CHECK(after.find("workstation") == std::string::npos);
    CHECK(after.find("vps") != std::string::npos);
}

TEST(with_no_machines_the_block_is_removed_and_the_file_is_as_it_was) {
    const std::string with_block = render_hosts(SYSTEM_HOSTS, "home", make_entries());
    CHECK(render_hosts(with_block, "home", {}) == SYSTEM_HOSTS);
}

TEST(lines_added_after_the_block_by_somebody_else_are_kept) {
    const std::string with_block = render_hosts(SYSTEM_HOSTS, "home", make_entries()) + "192.168.1.9 printer\n";
    const std::string text = render_hosts(with_block, "home", make_entries());
    CHECK(text.find("192.168.1.9 printer\n") != std::string::npos);
    CHECK(text.find("127.0.0.1 localhost\n") != std::string::npos);
}
