#include "path.h"

#include <algorithm>

namespace hawser {

void observe_path(PathHealth& health, int64_t last_handshake_unix_seconds, uint64_t received_bytes, int64_t now_unix_seconds) {
    // The first look only sets the baseline. A daemon that has just started
    // finds counters left by whatever ran before it, and bytes received at
    // some unknown time in the past prove nothing about the path now.
    if (health.has_baseline && received_bytes != health.received_bytes) {
        health.last_heard_unix_seconds = now_unix_seconds;
    }
    if (last_handshake_unix_seconds != health.last_handshake_unix_seconds) {
        // A handshake carries its own time, so it is dated by that and not by
        // when it was noticed.
        health.last_heard_unix_seconds = std::max(health.last_heard_unix_seconds, last_handshake_unix_seconds);
    }
    health.has_baseline = true;
    health.last_handshake_unix_seconds = last_handshake_unix_seconds;
    health.received_bytes = received_bytes;
}

bool is_path_alive(const PathHealth& health, int64_t now_unix_seconds) {
    if (health.last_heard_unix_seconds == 0) return false;
    return now_unix_seconds - health.last_heard_unix_seconds <= DIRECT_PATH_SILENCE_SECONDS;
}

}  // namespace hawser
