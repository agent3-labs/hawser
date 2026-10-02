#include "text.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace hawser {

namespace {

const char BASE64_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string describe_errno(const std::string& action, const std::string& path) {
    return action + " " + path + ": " + std::strerror(errno);
}

}  // namespace

std::string trim(std::string_view text) {
    const char* blanks = " \t\r\n";
    const size_t first = text.find_first_not_of(blanks);
    if (first == std::string_view::npos) return "";
    const size_t last = text.find_last_not_of(blanks);
    return std::string(text.substr(first, last - first + 1));
}

std::vector<std::string> split_words(std::string_view line) {
    std::vector<std::string> words;
    std::istringstream stream{std::string(line)};
    std::string word;
    while (stream >> word) words.push_back(word);
    return words;
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        lines.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

std::map<std::string, std::string> parse_named_values(std::string_view text, const std::string& source) {
    std::map<std::string, std::string> values;
    for (const std::string& raw_line : split_lines(text)) {
        const std::string line = trim(raw_line);
        if (line.empty() || line[0] == '#') continue;
        const size_t equals = line.find('=');
        if (equals == std::string::npos) throw HawserError(source + ": expected name=value, found '" + line + "'");
        values[trim(line.substr(0, equals))] = trim(line.substr(equals + 1));
    }
    return values;
}

std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw HawserError(describe_errno("cannot read", path));
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

bool file_exists(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0;
}

void write_file(const std::string& path, std::string_view content, unsigned mode) {
    const std::string unfinished_path = path + ".tmp";
    const int file = open(unfinished_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (file < 0) throw HawserError(describe_errno("cannot write", unfinished_path));
    // The mode given to open() is cut down by the umask; a key file must end up exactly as asked.
    fchmod(file, mode);
    size_t written = 0;
    while (written < content.size()) {
        const ssize_t count = write(file, content.data() + written, content.size() - written);
        if (count < 0) {
            close(file);
            throw HawserError(describe_errno("cannot write", unfinished_path));
        }
        written += static_cast<size_t>(count);
    }
    fsync(file);
    close(file);
    if (rename(unfinished_path.c_str(), path.c_str()) != 0) {
        throw HawserError(describe_errno("cannot replace", path));
    }
}

void overwrite_file(const std::string& path, std::string_view content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) throw HawserError(describe_errno("cannot write", path));
    file << content;
    file.flush();
    if (!file) throw HawserError(describe_errno("cannot write", path));
}

void make_directory(const std::string& path, unsigned mode) {
    if (mkdir(path.c_str(), mode) != 0 && errno != EEXIST) {
        throw HawserError(describe_errno("cannot create", path));
    }
}

std::string encode_base64(std::string_view bytes) {
    std::string text;
    size_t index = 0;
    while (index < bytes.size()) {
        const size_t group_size = std::min<size_t>(3, bytes.size() - index);
        uint32_t group = 0;
        for (size_t offset = 0; offset < 3; ++offset) {
            group <<= 8;
            if (offset < group_size) group |= static_cast<unsigned char>(bytes[index + offset]);
        }
        for (size_t position = 0; position < 4; ++position) {
            const bool is_padding = position > group_size;
            text += is_padding ? '=' : BASE64_ALPHABET[(group >> (18 - 6 * position)) & 0x3f];
        }
        index += group_size;
    }
    return text;
}

std::string decode_base64(std::string_view text) {
    std::string bytes;
    uint32_t group = 0;
    int bit_count = 0;
    for (const char character : text) {
        if (character == '=') break;
        const char* found = std::strchr(BASE64_ALPHABET, character);
        if (character == '\0' || found == nullptr) throw HawserError("that is not Base64");
        group = (group << 6) | static_cast<uint32_t>(found - BASE64_ALPHABET);
        bit_count += 6;
        if (bit_count >= 8) {
            bit_count -= 8;
            bytes += static_cast<char>((group >> bit_count) & 0xff);
        }
    }
    return bytes;
}

int64_t read_unix_seconds() {
    return static_cast<int64_t>(std::time(nullptr));
}

void log_line(const std::string& message) {
    // Standard error, unbuffered, so the lines reach the journal as they happen.
    std::cerr << "hawser: " << message << std::endl;
}

}  // namespace hawser
