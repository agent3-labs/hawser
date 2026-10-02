// hawser: a private network between your machines, over kernel WireGuard.
//
// One machine with a public address is the coordinator. It hands every other
// machine an address and tells each one how to find the rest.

#include <iostream>
#include <string>
#include <vector>

#include "commands.h"
#include "text.h"

#ifndef HAWSER_VERSION
#define HAWSER_VERSION "development"
#endif

namespace {

const char USAGE[] =
    "usage: hawser <command>\n"
    "\n"
    "On the coordinator, the one machine others can dial:\n"
    "  init --network-name <name> --network <a.b.c.d/n> --name <name> --endpoint <host:port>\n"
    "                    create a network with this machine as its coordinator\n"
    "  invite <name>     print the one line that lets a new machine join\n"
    "  remove <name>     take a machine out of the network\n"
    "\n"
    "On every other machine:\n"
    "  join <invite>     join the network the invite came from\n"
    "\n"
    "On any machine:\n"
    "  up                run the network; this is what the service runs\n"
    "  status [--json]   list the machines and how each is reached; --json for a program\n"
    "  down              remove the interface and leave until 'up' runs again\n"
    "  version           print the version\n";

}  // namespace

int main(int argument_count, char** argument_values) {
    const std::vector<std::string> words(argument_values + 1, argument_values + argument_count);
    if (words.empty() || words[0] == "help" || words[0] == "--help" || words[0] == "-h") {
        std::cout << USAGE;
        return words.empty() ? 1 : 0;
    }
    const std::string& command = words[0];
    const std::vector<std::string> arguments(words.begin() + 1, words.end());

    try {
        if (command == "init") hawser::run_init(arguments);
        else if (command == "invite") hawser::run_invite(arguments);
        else if (command == "join") hawser::run_join(arguments);
        else if (command == "up") hawser::run_up(arguments);
        else if (command == "down") hawser::run_down(arguments);
        else if (command == "status") hawser::run_status(arguments);
        else if (command == "remove") hawser::run_remove(arguments);
        else if (command == "version" || command == "--version") std::cout << "hawser " << HAWSER_VERSION << "\n";
        else {
            std::cerr << "hawser: unknown command '" << command << "'\n\n" << USAGE;
            return 1;
        }
    } catch (const hawser::HawserError& error) {
        std::cerr << "hawser: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
