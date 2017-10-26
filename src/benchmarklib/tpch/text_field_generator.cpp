#include "text_field_generator.hpp"

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace tpch {

/**
 * According to the TPCH specification, there should be 300MB of text generated by a given grammar.
 * The text is later used to draw substrings from to fill multiple columns.
 */
TextFieldGenerator::TextFieldGenerator(benchmark_utilities::RandomGenerator random_generator)
    : _random_gen(random_generator),
      _grammar(TpchGrammar(random_generator)),
      _text(_grammar.random_text(300'000'000)) {}

std::string TextFieldGenerator::text_string(size_t lower_length, size_t upper_length) {
  auto length = _random_gen.random_number(lower_length, upper_length);
  auto start = _random_gen.random_number(0, _text.size() - length);
  return _text.substr(start, length);
}

/**
 * v_string corresponds to the TPC-H specification of v-string, which defines it as
 * a random string consisting of random characters from an alphanumeric character-set
 * of at least 64 characters. The length of the string should be between lower_length
 * and upper_length (inclusive).
 */
std::string TextFieldGenerator::v_string(size_t lower_length, size_t upper_length) {
  size_t length = _random_gen.random_number(lower_length, upper_length);
  std::string s;
  s.reserve(length);
  for (size_t i = 0; i < length; i++) {
    auto offset = _random_gen.random_number(0, 63);
    char next_char;
    // to construct alphanumeric characters, different ranges of offset
    // are mapped to different character ranges by the following cascade:
    // (denoted as offset range -> character range)
    if (offset < 10) {  // 0-9 -> 0-9
      next_char = '0' + offset;
    } else {
      offset -= 10;
      if (offset < 26) {  // 10-35 -> a-z
        next_char = 'a' + offset;
      } else {
        offset -= 26;
        if (offset < 26) {  // 36-61 -> A-Z
          next_char = 'A' + offset;
        } else {  // 62-63 -> special characters
          offset -= 26;
          if (offset == 0) {
            next_char = '.';
          } else {
            next_char = ' ';
          }
        }
      }
    }
    s.append(1, next_char);
  }

  return s;
}

/**
 * From the TPC-H specification:
 * The term phone number represents a string of numeric characters separated by hyphens and generated as follows:
 * Let i be an index into the list of strings Nations,
 * Let country_code be the sub-string representation of the number (i + 10),
 * Let local_number1 be random [100 .. 999],
 * Let local_number2 be random [100 .. 999],
 * Let local_number3 be random [1000 .. 9999],
 * The phone number string is obtained by concatenating the following sub-strings:
 * country_code, "-", local_number1, "-", local_number2, "-", local_number3
 */
std::string TextFieldGenerator::generate_phone_number(uint32_t nationkey) {
  std::stringstream ss;
  auto country_code = nationkey + 10;
  ss << country_code << "-" << _random_gen.random_number(100, 999) << "-" << _random_gen.random_number(100, 999);
  ss << "-" << _random_gen.random_number(1000, 9999);
  return ss.str();
}

std::string TextFieldGenerator::pad_int_with_zeroes(size_t number, size_t length) {
  std::stringstream ss;
  ss << std::setfill('0') << std::setw(length) << number;
  return ss.str();
}

/**
 * generate_name_of_part generates a field for the P_NAME column in the PART table
 * by concatenating five unique randomly selected strings from part_name_words,
 * separated by a single space.
 */
std::string TextFieldGenerator::generate_name_of_part() {
  auto word_ids = _random_gen.select_unique_ids(5, part_name_words.size());
  // The following lines concatenate the strings, having a delimiter
  // only between the strings, no trailing or leading one.
  auto it = word_ids.begin();
  std::stringstream ss;
  ss << part_name_words[*it++];
  for (; it != word_ids.end(); it++) {
    ss << " " << part_name_words[*it];
  }
  return ss.str();
}

std::string TextFieldGenerator::generate_type_of_part() {
  std::stringstream ss;
  ss << _grammar.random_word(part_type_syllables_1) << " ";
  ss << _grammar.random_word(part_type_syllables_2) << " ";
  ss << _grammar.random_word(part_type_syllables_3);
  return ss.str();
}

std::string TextFieldGenerator::generate_container_of_part() {
  return _grammar.random_word(part_container_syllables_1) + " " + _grammar.random_word(part_container_syllables_2);
}

std::string TextFieldGenerator::generate_customer_segment() { return _grammar.random_word(customer_segments); }

std::string TextFieldGenerator::generate_order_priority() { return _grammar.random_word(order_priorities); }

std::string TextFieldGenerator::generate_lineitem_instruction() { return _grammar.random_word(lineitem_instructions); }

std::string TextFieldGenerator::generate_lineitem_mode() { return _grammar.random_word(lineitem_modes); }

const std::vector<std::string> TextFieldGenerator::nation_names = {
    "ALGERIA", "ARGENTINA", "BRAZIL",         "CANADA",       "EGYPT", "ETHIOPIA", "FRANCE",
    "GERMANY", "INDIA",     "INDONESIA",      "IRAN",         "IRAQ",  "JAPAN",    "JORDAN",
    "KENYA",   "MOROCCO",   "MOZAMBIQUE",     "PERU",         "CHINA", "ROMANIA",  "SAUDI ARABIA",
    "VIETNAM", "RUSSIA",    "UNITED KINGDOM", "UNITED STATES"};

const std::vector<std::string> TextFieldGenerator::region_names = {"AFRICA", "AMERICA", "ASIA", "EUROPE",
                                                                   "MIDDLE EAST"};

const std::vector<std::string> TextFieldGenerator::part_name_words = {
    "almond",   "antique", "aquamarine", "azure",     "beige",      "bisque",    "black",     "blanched", "blue",
    "blush",    "brown",   "burlywood",  "burnished", "chartreuse", "chiffon",   "chocolate", "coral",    "cornflower",
    "cornsilk", "cream",   "cyan",       "dark",      "deep",       "dim",       "dodger",    "drab",     "firebrick",
    "floral",   "forest",  "frosted",    "gainsboro", "ghost",      "goldenrod", "green",     "grey",     "honeydew",
    "hot",      "indian",  "ivory",      "khaki",     "lace",       "lavender",  "lawn",      "lemon",    "light",
    "lime",     "linen",   "magenta",    "maroon",    "medium",     "metallic",  "midnight",  "mint",     "misty",
    "moccasin", "navajo",  "navy",       "olive",     "orange",     "orchid",    "pale",      "papaya",   "peach",
    "peru",     "pink",    "plum",       "powder",    "puff",       "purple",    "red",       "rose",     "rosy",
    "royal",    "saddle",  "salmon",     "sandy",     "seashell",   "sienna",    "sky",       "slate",    "smoke",
    "snow",     "spring",  "steel",      "tan",       "thistle",    "tomato",    "turquoise", "violet",   "wheat",
    "white",    "yellow"};

const std::vector<std::string> TextFieldGenerator::part_type_syllables_1 = {"STANDARD", "SMALL",   "MEDIUM",
                                                                            "LARGE",    "ECONOMY", "PROMO"};
const std::vector<std::string> TextFieldGenerator::part_type_syllables_2 = {"ANODIZED", "BURNISHED", "PLATED",
                                                                            "POLISHED", "BRUSHED"};
const std::vector<std::string> TextFieldGenerator::part_type_syllables_3 = {"TIN", "NICKEL", "BRASS", "STEEL",
                                                                            "COPPER"};

const std::vector<std::string> TextFieldGenerator::part_container_syllables_1 = {"SM", "LG", "MED", "JUMBO", "WRAP"};
const std::vector<std::string> TextFieldGenerator::part_container_syllables_2 = {"CASE", "BOX",  "BAG", "JAR",
                                                                                 "PKG",  "PACK", "CAN", "DRUM"};

const std::vector<std::string> TextFieldGenerator::customer_segments = {"AUTOMOBILE", "BUILDING", "FURNITURE",
                                                                        "MACHINERY", "HOUSEHOLD"};

const std::vector<std::string> TextFieldGenerator::order_priorities = {"1-URGENT", "2-HIGH", "3-MEDIUM",
                                                                       "4-NOT SPECIFIED", "5-LOW"};

const std::vector<std::string> TextFieldGenerator::lineitem_instructions = {"DELIVER IN PERSON", "COLLECT COD", "NONE",
                                                                            "TAKE BACK RETURN"};

const std::vector<std::string> TextFieldGenerator::lineitem_modes = {"REG AIR", "AIR",  "RAIL", "SHIP",
                                                                     "TRUCK",   "MAIL", "FOB"};

}  // namespace tpch