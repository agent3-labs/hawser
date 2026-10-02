// WireGuard keys. A machine is identified by its public key.

#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hawser {

struct Key {
    std::array<uint8_t, 32> bytes{};

    auto operator<=>(const Key&) const = default;
};

Key generate_private_key();
Key derive_public_key(const Key& private_key);

// The 44-character Base64 form `wg` prints.
std::string format_key(const Key& key);
std::optional<Key> parse_key(std::string_view text);

}  // namespace hawser
