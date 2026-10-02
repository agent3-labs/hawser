#include "report.h"

#include <cstdio>

namespace hawser {

namespace {

std::string quote(const std::string& text) {
    std::string quoted = "\"";
    for (const unsigned char character : text) {
        if (character == '"' || character == '\\') {
            quoted += '\\';
            quoted += static_cast<char>(character);
        } else if (character < 0x20) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", character);
            quoted += escaped;
        } else {
            quoted += static_cast<char>(character);
        }
    }
    return quoted + "\"";
}

// A text value, or null when there is none.
std::string quote_or_null(const std::string& text) {
    return text.empty() ? "null" : quote(text);
}

std::string format_machine(const StatusMachine& machine) {
    return "{\"name\": " + quote(machine.name) + ", \"address\": " + quote(machine.address) +
           ", \"is_coordinator\": " + (machine.is_coordinator ? "true" : "false") + ", \"state\": " + quote(machine.state) +
           ", \"path\": " + quote(machine.path) + ", \"heard_seconds_ago\": " +
           (machine.heard_seconds_ago ? std::to_string(*machine.heard_seconds_ago) : "null") +
           ", \"endpoint\": " + quote_or_null(machine.endpoint) + "}";
}

}  // namespace

std::string format_status_json(const StatusReport& report) {
    std::string machines;
    for (const StatusMachine& machine : report.machines) {
        if (!machines.empty()) machines += ", ";
        machines += format_machine(machine);
    }
    return "{\"interface\": " + quote(report.interface_name) + ", \"up\": " + (report.is_up ? "true" : "false") +
           ", \"role\": " + quote(report.role) + ", \"network_name\": " + quote(report.network_name) +
           ", \"network\": " + quote(report.network) + ", \"name\": " + quote(report.name) +
           ", \"address\": " + quote(report.address) + ", \"coordinator_error\": " + quote_or_null(report.coordinator_error) +
           ", \"machines\": [" + machines + "]}";
}

}  // namespace hawser
