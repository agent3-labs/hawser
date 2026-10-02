#include "protocol.h"

#include <cerrno>
#include <charconv>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "text.h"

namespace hawser {

namespace {

const std::string PROTOCOL_GREETING = "hawser 1";
const std::string UNKNOWN_FIELD = "-";
const size_t MACHINE_FIELD_COUNT = 6;
const int MILLISECONDS_PER_SECOND = 1000;

struct OpenSocket {
    int descriptor = -1;
    ~OpenSocket() {
        if (descriptor >= 0) close(descriptor);
    }
};

}  // namespace

std::string format_network_view(const NetworkView& view) {
    std::string text = PROTOCOL_GREETING + "\n";
    text += "network " + view.network_name + " " + format_network(view.network) + "\n";
    for (const MachineView& machine : view.machines) {
        text += "machine " + machine.name + " " + format_address(machine.address) + " " + format_key(machine.public_key) +
                " " + (machine.endpoint.empty() ? UNKNOWN_FIELD : machine.endpoint) + " " +
                (machine.seen_seconds_ago ? std::to_string(*machine.seen_seconds_ago) : UNKNOWN_FIELD) + "\n";
    }
    return text + "end\n";
}

NetworkView parse_network_view(const std::string& text) {
    const std::vector<std::string> lines = split_lines(text);
    // The last line proves the answer arrived whole. Half a list would look
    // like machines having left, and their routes would be taken away.
    if (lines.size() < 3 || trim(lines.front()) != PROTOCOL_GREETING || trim(lines.back()) != "end") {
        throw HawserError("the coordinator's answer was cut short or is not one hawser understands");
    }

    NetworkView view;
    const std::vector<std::string> network_fields = split_words(lines[1]);
    const auto network = network_fields.size() == 3 ? parse_network(network_fields[2]) : std::nullopt;
    if (network_fields.size() != 3 || network_fields[0] != "network" || !network) {
        throw HawserError("the coordinator's answer names no network");
    }
    view.network_name = network_fields[1];
    view.network = *network;

    for (size_t index = 2; index + 1 < lines.size(); ++index) {
        const std::vector<std::string> fields = split_words(lines[index]);
        if (fields.size() != MACHINE_FIELD_COUNT || fields[0] != "machine") {
            throw HawserError("the coordinator's answer has a line hawser cannot read: " + lines[index]);
        }
        MachineView machine;
        machine.name = fields[1];
        const auto address = parse_address(fields[2]);
        const auto public_key = parse_key(fields[3]);
        if (!address || !public_key) throw HawserError("the coordinator's answer has a line hawser cannot read: " + lines[index]);
        machine.address = *address;
        machine.public_key = *public_key;
        if (fields[4] != UNKNOWN_FIELD) machine.endpoint = fields[4];
        if (fields[5] != UNKNOWN_FIELD) {
            int64_t seconds = 0;
            const auto [end, error] = std::from_chars(fields[5].data(), fields[5].data() + fields[5].size(), seconds);
            if (error != std::errc{}) throw HawserError("the coordinator's answer has a line hawser cannot read: " + lines[index]);
            machine.seen_seconds_ago = seconds;
        }
        view.machines.push_back(machine);
    }
    return view;
}

std::string call_coordinator(Ipv4Address coordinator_address, int api_port, const std::string& request_line, int timeout_seconds) {
    const std::string coordinator = format_address(coordinator_address) + ":" + std::to_string(api_port);
    OpenSocket connection;
    connection.descriptor = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (connection.descriptor < 0) throw HawserError(std::string("cannot open a socket: ") + std::strerror(errno));

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(static_cast<uint16_t>(api_port));
    destination.sin_addr.s_addr = htonl(coordinator_address.value);

    const int timeout_milliseconds = timeout_seconds * MILLISECONDS_PER_SECOND;
    if (connect(connection.descriptor, reinterpret_cast<sockaddr*>(&destination), sizeof(destination)) != 0) {
        if (errno != EINPROGRESS) throw HawserError("cannot reach the coordinator at " + coordinator + ": " + std::strerror(errno));
        pollfd waiting{connection.descriptor, POLLOUT, 0};
        if (poll(&waiting, 1, timeout_milliseconds) <= 0) {
            throw HawserError("the coordinator at " + coordinator + " did not answer within " + std::to_string(timeout_seconds) + " seconds");
        }
        int connect_error = 0;
        socklen_t length = sizeof(connect_error);
        getsockopt(connection.descriptor, SOL_SOCKET, SO_ERROR, &connect_error, &length);
        if (connect_error != 0) throw HawserError("cannot reach the coordinator at " + coordinator + ": " + std::strerror(connect_error));
    }

    const std::string request = request_line + "\n";
    if (send(connection.descriptor, request.data(), request.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(request.size())) {
        throw HawserError("cannot send to the coordinator at " + coordinator + ": " + std::strerror(errno));
    }

    std::string answer;
    char buffer[4096];
    while (true) {
        pollfd waiting{connection.descriptor, POLLIN, 0};
        if (poll(&waiting, 1, timeout_milliseconds) <= 0) {
            throw HawserError("the coordinator at " + coordinator + " stopped answering");
        }
        const ssize_t count = recv(connection.descriptor, buffer, sizeof(buffer), 0);
        if (count < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (count < 0) throw HawserError("lost the coordinator at " + coordinator + ": " + std::strerror(errno));
        if (count == 0) break;
        answer.append(buffer, static_cast<size_t>(count));
    }
    return answer;
}

}  // namespace hawser
