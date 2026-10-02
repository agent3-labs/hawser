// The commands a person runs. Each returns when it is done.

#pragma once

#include <string>
#include <vector>

namespace hawser {

// Each takes the words that followed the command's name.
void run_init(const std::vector<std::string>& arguments);
void run_invite(const std::vector<std::string>& arguments);
void run_join(const std::vector<std::string>& arguments);
void run_up(const std::vector<std::string>& arguments);
void run_down(const std::vector<std::string>& arguments);
void run_status(const std::vector<std::string>& arguments);
void run_remove(const std::vector<std::string>& arguments);

}  // namespace hawser
