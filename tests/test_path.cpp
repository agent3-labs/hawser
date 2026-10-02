#include "path.h"
#include "test.h"

using namespace hawser;

namespace {
const int64_t NOW = 1790000000;
}

TEST(a_path_never_heard_from_is_not_alive) {
    PathHealth health;
    observe_path(health, 0, 0, NOW);
    observe_path(health, 0, 0, NOW + 2);
    CHECK(!is_path_alive(health, NOW + 2));
}

TEST(a_fresh_handshake_brings_a_path_alive) {
    PathHealth health;
    observe_path(health, 0, 0, NOW);
    observe_path(health, NOW + 1, 92, NOW + 2);
    CHECK(is_path_alive(health, NOW + 2));
}

TEST(a_path_stays_alive_while_keepalives_arrive) {
    PathHealth health;
    observe_path(health, 0, 0, NOW);
    observe_path(health, NOW, 92, NOW);
    uint64_t received_bytes = 92;
    // Ten minutes with no new handshake noticed, only a keepalive every 25 seconds.
    for (int64_t now = NOW + KEEPALIVE_SECONDS; now <= NOW + 600; now += KEEPALIVE_SECONDS) {
        received_bytes += 32;
        observe_path(health, NOW, received_bytes, now);
        CHECK(is_path_alive(health, now));
    }
}

TEST(a_path_that_goes_quiet_stops_being_alive_after_three_missed_keepalives) {
    PathHealth health;
    observe_path(health, 0, 0, NOW);
    observe_path(health, NOW, 92, NOW);
    observe_path(health, NOW, 92, NOW + DIRECT_PATH_SILENCE_SECONDS);
    CHECK(is_path_alive(health, NOW + DIRECT_PATH_SILENCE_SECONDS));
    observe_path(health, NOW, 92, NOW + DIRECT_PATH_SILENCE_SECONDS + 1);
    CHECK(!is_path_alive(health, NOW + DIRECT_PATH_SILENCE_SECONDS + 1));
}

TEST(a_quiet_path_comes_back_when_it_carries_something_again) {
    PathHealth health;
    observe_path(health, 0, 0, NOW);
    observe_path(health, NOW, 92, NOW);
    observe_path(health, NOW, 92, NOW + 500);
    CHECK(!is_path_alive(health, NOW + 500));
    observe_path(health, NOW, 124, NOW + 502);
    CHECK(is_path_alive(health, NOW + 502));
}

TEST(counters_left_from_before_the_daemon_started_prove_nothing) {
    // The kernel still holds a peer from hours ago: an old handshake and a
    // large byte count. A daemon seeing it for the first time must not route
    // traffic to it on that evidence.
    PathHealth health;
    observe_path(health, NOW - 7200, 5000000, NOW);
    CHECK(!is_path_alive(health, NOW));
    observe_path(health, NOW - 7200, 5000000, NOW + 2);
    CHECK(!is_path_alive(health, NOW + 2));
}

TEST(a_path_that_was_working_when_the_daemon_restarted_is_alive_at_once) {
    // The kernel holds a peer whose handshake is seconds old. Restarting the
    // daemon must not cut it.
    PathHealth health;
    observe_path(health, NOW - 10, 5000000, NOW);
    CHECK(is_path_alive(health, NOW));
}
