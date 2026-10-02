#include "report.h"
#include "test.h"
#include "text.h"

using namespace hawser;

namespace {

StatusReport make_report() {
    StatusReport report;
    report.interface_name = "hawser0";
    report.is_up = true;
    report.role = "coordinator";
    report.network_name = "home";
    report.network = "10.77.0.0/24";
    report.name = "vps";
    report.address = "10.77.0.1";
    StatusMachine self;
    self.name = "vps";
    self.address = "10.77.0.1";
    self.is_coordinator = true;
    self.state = "joined";
    self.path = "self";
    StatusMachine other;
    other.name = "workstation";
    other.address = "10.77.0.2";
    other.state = "joined";
    other.path = "direct";
    other.heard_seconds_ago = 12;
    other.endpoint = "203.0.113.7:51820";
    report.machines = {self, other};
    return report;
}

}  // namespace

TEST(a_status_report_is_one_line_of_json) {
    const std::string json = format_status_json(make_report());
    CHECK(json.find('\n') == std::string::npos);
    CHECK(json.front() == '{' && json.back() == '}');
    CHECK(json.find("\"up\": true") != std::string::npos);
    CHECK(json.find("\"role\": \"coordinator\"") != std::string::npos);
    CHECK(json.find("\"network\": \"10.77.0.0/24\"") != std::string::npos);
}

TEST(what_is_not_known_is_null_not_an_empty_string_or_zero) {
    const std::string json = format_status_json(make_report());
    // The coordinator's own row: never heard from itself, and no endpoint.
    CHECK(json.find("\"path\": \"self\", \"heard_seconds_ago\": null, \"endpoint\": null") != std::string::npos);
    CHECK(json.find("\"heard_seconds_ago\": 12, \"endpoint\": \"203.0.113.7:51820\"") != std::string::npos);
    CHECK(json.find("\"coordinator_error\": null") != std::string::npos);
}

TEST(a_network_that_is_down_says_so_and_lists_nothing) {
    StatusReport report = make_report();
    report.is_up = false;
    report.machines.clear();
    const std::string json = format_status_json(report);
    CHECK(json.find("\"up\": false") != std::string::npos);
    CHECK(json.find("\"machines\": []") != std::string::npos);
}

TEST(text_that_would_break_json_is_escaped) {
    StatusReport report = make_report();
    report.coordinator_error = "it said \"no\"\nand a \\ too";
    const std::string json = format_status_json(report);
    CHECK(json.find("\"coordinator_error\": \"it said \\\"no\\\"\\u000aand a \\\\ too\"") != std::string::npos);
    CHECK(json.find('\n') == std::string::npos);
}
