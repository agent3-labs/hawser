#include "invite.h"
#include "test.h"
#include "text.h"

using namespace hawser;

namespace {

Invite make_invite() {
    Invite invite;
    invite.network_name = "home";
    invite.network = *parse_network("10.77.0.0/24");
    invite.name = "workstation";
    invite.address = *parse_address("10.77.0.2");
    invite.one_time_private_key = generate_private_key();
    invite.coordinator_endpoint = "vpn.example.com:51820";
    invite.coordinator_address = *parse_address("10.77.0.1");
    invite.coordinator_public_key = derive_public_key(generate_private_key());
    invite.api_port = 51821;
    invite.mtu_bytes = 1420;
    invite.expires_unix_seconds = 1790000000;
    return invite;
}

}  // namespace

TEST(an_invite_survives_being_copied_between_machines) {
    const Invite invite = make_invite();
    // Pasted with stray whitespace around it, as it will be.
    const Invite read_back = decode_invite("  " + encode_invite(invite) + "\n");
    CHECK(read_back.network_name == "home");
    CHECK(read_back.network == invite.network);
    CHECK(read_back.name == "workstation");
    CHECK(read_back.address == invite.address);
    CHECK(read_back.one_time_private_key == invite.one_time_private_key);
    CHECK(read_back.coordinator_endpoint == invite.coordinator_endpoint);
    CHECK(read_back.coordinator_address == invite.coordinator_address);
    CHECK(read_back.coordinator_public_key == invite.coordinator_public_key);
    CHECK(read_back.api_port == 51821);
    CHECK(read_back.mtu_bytes == 1420);
    CHECK(read_back.expires_unix_seconds == 1790000000);
}

TEST(an_invite_is_one_line_with_no_spaces) {
    const std::string text = encode_invite(make_invite());
    CHECK(text.rfind("hawser1:", 0) == 0);
    CHECK(text.find_first_of(" \n\t") == std::string::npos);
}

TEST(text_that_is_not_an_invite_is_refused) {
    CHECK_THROWS(decode_invite("hello"));
    CHECK_THROWS(decode_invite("hawser1:!!!"));
    CHECK_THROWS(decode_invite("hawser1:" + encode_base64("name=only\n")));
}

TEST(a_cut_off_invite_is_refused) {
    const std::string text = encode_invite(make_invite());
    CHECK_THROWS(decode_invite(text.substr(0, text.size() / 2)));
}
