// Small helpers for text and files, shared by everything else.

#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace hawser {

// A failure the person running hawser can act on. Printed without a trace.
struct HawserError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::string trim(std::string_view text);
std::vector<std::string> split_words(std::string_view line);
std::vector<std::string> split_lines(std::string_view text);

// Lines of name=value, with blank lines and # comments skipped. `source` names
// the text in the error thrown for a line that is neither.
std::map<std::string, std::string> parse_named_values(std::string_view text, const std::string& source);

std::string read_file(const std::string& path);
bool file_exists(const std::string& path);

// Writes a whole file by renaming a finished copy over it, so a reader never
// sees half of it. `mode` is the file's permission bits.
void write_file(const std::string& path, std::string_view content, unsigned mode);

// Writes a file where it stands. For /etc/hosts, which Docker mounts as a
// single file and which therefore cannot be replaced by a rename.
void overwrite_file(const std::string& path, std::string_view content);

void make_directory(const std::string& path, unsigned mode);

// Base64 of arbitrary bytes, for the invite a person copies between machines.
std::string encode_base64(std::string_view bytes);
// Throws HawserError when the text is not Base64.
std::string decode_base64(std::string_view text);

int64_t read_unix_seconds();

void log_line(const std::string& message);

}  // namespace hawser
