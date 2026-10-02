#include "test.h"
#include "text.h"

using namespace hawser;

TEST(base64_survives_every_length_of_input) {
    // The three padding cases, and bytes that are not text.
    for (const std::string& bytes : {std::string(""), std::string("a"), std::string("ab"), std::string("abc"),
                                     std::string("abcd"), std::string("\x00\xff\x10\x80", 4)}) {
        CHECK(decode_base64(encode_base64(bytes)) == bytes);
    }
    CHECK(encode_base64("ab") == "YWI=");
}

TEST(text_that_is_not_base64_is_refused) {
    CHECK_THROWS(decode_base64("not base64!"));
}

TEST(named_values_skip_comments_and_blank_lines) {
    const auto values = parse_named_values("# a comment\n\nname = vps \nnetwork=10.77.0.0/24\n", "test");
    CHECK(values.size() == 2);
    CHECK(values.at("name") == "vps");
    CHECK(values.at("network") == "10.77.0.0/24");
}

TEST(a_line_that_is_not_name_equals_value_is_refused) {
    CHECK_THROWS(parse_named_values("just words\n", "test"));
}

TEST(words_and_lines_are_split_as_expected) {
    CHECK(split_words("  machine   vps 10.77.0.1 ").size() == 3);
    CHECK(split_lines("one\ntwo\n").size() == 2);
    CHECK(split_lines("one\ntwo").size() == 2);
    CHECK(trim("  \t x y \r\n") == "x y");
}
