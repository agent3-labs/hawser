#include "key.h"

#include <cstring>

extern "C" {
#include "wireguard.h"
}

namespace hawser {

Key generate_private_key() {
    Key key;
    wg_generate_private_key(key.bytes.data());
    return key;
}

Key derive_public_key(const Key& private_key) {
    Key public_key;
    wg_generate_public_key(public_key.bytes.data(), private_key.bytes.data());
    return public_key;
}

std::string format_key(const Key& key) {
    wg_key_b64_string text = {};
    wg_key_to_base64(text, key.bytes.data());
    return text;
}

std::optional<Key> parse_key(std::string_view text) {
    wg_key_b64_string padded = {};
    if (text.size() != sizeof(padded) - 1) return std::nullopt;
    std::memcpy(padded, text.data(), text.size());
    Key key;
    if (wg_key_from_base64(key.bytes.data(), padded) != 0) return std::nullopt;
    return key;
}

}  // namespace hawser
