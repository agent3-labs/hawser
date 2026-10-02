#include "hosts.h"

#include "text.h"

namespace hawser {

namespace {

const std::string BLOCK_START = "# hawser begin";
const std::string BLOCK_END = "# hawser end";

}  // namespace

std::string render_hosts(const std::string& current_text, const std::string& network_name, const std::vector<HostEntry>& entries) {
    std::string text;
    bool is_inside_block = false;
    for (const std::string& line : split_lines(current_text)) {
        if (line.rfind(BLOCK_START, 0) == 0) {
            is_inside_block = true;
        } else if (line.rfind(BLOCK_END, 0) == 0) {
            is_inside_block = false;
        } else if (!is_inside_block) {
            text += line + "\n";
        }
    }
    if (entries.empty()) return text;

    text += BLOCK_START + ": machines of the " + network_name + " network. Written by hawser; edits here are lost.\n";
    for (const HostEntry& entry : entries) {
        text += format_address(entry.address) + " " + entry.name + "." + network_name + " " + entry.name + "\n";
    }
    text += BLOCK_END + "\n";
    return text;
}

void update_hosts_file(const std::string& path, const std::string& network_name, const std::vector<HostEntry>& entries) {
    const std::string current_text = file_exists(path) ? read_file(path) : "";
    const std::string wanted_text = render_hosts(current_text, network_name, entries);
    if (wanted_text != current_text) overwrite_file(path, wanted_text);
}

}  // namespace hawser
