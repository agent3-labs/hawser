// Deciding whether two machines can talk directly.
//
// An agent keeps every other machine as a WireGuard peer, but at first gives
// it no addresses to route. The peer still gets keepalives, so the two sides
// try to reach each other in the background while their traffic goes through
// the coordinator. Only once the direct path has been heard from does the
// agent route the machine's address to it, and it takes the route away again
// when the path goes quiet. Traffic is never sent down a path nobody has
// proven.

#pragma once

#include <cstdint>

namespace hawser {

// How often each side sends an empty packet down a quiet path.
const int KEEPALIVE_SECONDS = 25;
// Three keepalives missed in a row. A healthy path is never this quiet,
// because the other side sends something at least every KEEPALIVE_SECONDS.
const int DIRECT_PATH_SILENCE_SECONDS = 3 * KEEPALIVE_SECONDS;

struct PathHealth {
    // False until the peer's counters have been looked at once.
    bool has_baseline = false;
    // The kernel's counters for the peer when last looked at.
    int64_t last_handshake_unix_seconds = 0;
    uint64_t received_bytes = 0;
    // When the path last carried something from the other side. Zero if never.
    int64_t last_heard_unix_seconds = 0;
};

// Takes the kernel's current counters for a peer and notes whether the path
// carried anything since the last look.
void observe_path(PathHealth& health, int64_t last_handshake_unix_seconds, uint64_t received_bytes, int64_t now_unix_seconds);

bool is_path_alive(const PathHealth& health, int64_t now_unix_seconds);

}  // namespace hawser
