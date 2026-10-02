#include "settings.h"
#include "test.h"
#include "text.h"

using namespace hawser;

namespace {

Settings make_agent_settings() {
    Settings settings;
    settings.role = Role::agent;
    settings.network_name = "home";
    settings.network = *parse_network("10.77.0.0/24");
    settings.name = "workstation";
    settings.address = *parse_address("10.77.0.2");
    settings.coordinator_endpoint = "vpn.example.com:51820";
    settings.coordinator_address = *parse_address("10.77.0.1");
    settings.coordinator_public_key = derive_public_key(generate_private_key());
    return settings;
}

}  // namespace

TEST(settings_survive_being_written_and_read_back) {
    const Settings settings = make_agent_settings();
    const Settings read_back = parse_settings(format_settings(settings));
    CHECK(read_back.role == Role::agent);
    CHECK(read_back.network == settings.network);
    CHECK(read_back.name == "workstation");
    CHECK(read_back.address == settings.address);
    CHECK(read_back.listen_port == DEFAULT_LISTEN_PORT);
    CHECK(read_back.mtu_bytes == DEFAULT_MTU_BYTES);
    CHECK(read_back.manages_hosts);
    CHECK(read_back.coordinator_endpoint == "vpn.example.com:51820");
    CHECK(read_back.coordinator_public_key == settings.coordinator_public_key);
}

TEST(a_coordinator_needs_no_coordinator_key) {
    Settings settings = make_agent_settings();
    settings.role = Role::coordinator;
    CHECK(parse_settings(format_settings(settings)).role == Role::coordinator);
}

TEST(a_missing_value_is_named_and_never_defaulted) {
    std::string text = format_settings(make_agent_settings());
    const size_t line_start = text.find("mtu_bytes=");
    text.erase(line_start, text.find('\n', line_start) - line_start + 1);
    try {
        parse_settings(text);
        CHECK(false);
    } catch (const HawserError& error) {
        CHECK(std::string(error.what()).find("mtu_bytes") != std::string::npos);
    }
}

TEST(an_address_outside_the_network_is_refused) {
    std::string text = format_settings(make_agent_settings());
    const std::string wanted = "address=10.77.0.2";
    text.replace(text.find(wanted), wanted.size(), "address=10.99.0.2");
    CHECK_THROWS(parse_settings(text));
}

TEST(host_and_port_is_split_at_the_last_colon) {
    const HostAndPort parts = parse_host_and_port("vpn.example.com:51820");
    CHECK(parts.host == "vpn.example.com");
    CHECK(parts.port == 51820);
    CHECK_THROWS(parse_host_and_port("vpn.example.com"));
    CHECK_THROWS(parse_host_and_port("vpn.example.com:"));
    CHECK_THROWS(parse_host_and_port("vpn.example.com:99999"));
    CHECK_THROWS(parse_host_and_port(":51820"));
}
