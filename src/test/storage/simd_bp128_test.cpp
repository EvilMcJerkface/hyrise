#include <boost/hana/at_key.hpp>
#include <boost/hana/map.hpp>
#include <boost/hana/pair.hpp>

#include <bitset>
#include <iostream>
#include <memory>

#include "base_test.hpp"
#include "gtest/gtest.h"

#include "storage/zero_suppression/simd_bp128_decoder.hpp"
#include "storage/zero_suppression/simd_bp128_encoder.hpp"
#include "storage/zero_suppression/simd_bp128_vector.hpp"
#include "storage/zero_suppression/utils.hpp"

#include "types.hpp"
#include "utils/enum_constant.hpp"

namespace opossum {

namespace {

// Used for debugging purposes
[[maybe_unused]] void print_encoded_vector(const SimdBp128Vector& vector) {
  for (auto _128_bit : vector.data()) {
    for (auto _32_bit : _128_bit.data) {
      std::cout << std::bitset<32>{_32_bit} << "|";
    }
    std::cout << std::endl;
  }
}

}  // namespace

class SimdBp128Test : public BaseTest, public ::testing::WithParamInterface<uint8_t> {
 protected:
  void SetUp() override {
    _bit_size = GetParam();
    _min = 1ul << (_bit_size - 1u);
    _max = (1ul << _bit_size) - 1u;
  }

  pmr_vector<uint32_t> generate_sequence(size_t count) {
    auto sequence = pmr_vector<uint32_t>(count);
    auto value = _min;
    for (auto& elem : sequence) {
      elem = value;

      value += 1u;
      if (value > _max) value = _min;
    }

    return sequence;
  }

  std::unique_ptr<BaseZeroSuppressionVector> encode(const pmr_vector<uint32_t>& vector) {
    auto encoder = SimdBp128Encoder{};
    auto encoded_vector = encoder.encode(vector.get_allocator(), vector);
    EXPECT_EQ(encoded_vector->size(), vector.size());

    return encoded_vector;
  }

 private:
  uint8_t _bit_size;
  uint32_t _min;
  uint32_t _max;
};

auto formatter = [](const ::testing::TestParamInfo<uint8_t> info) {
  return std::to_string(static_cast<uint32_t>(info.param));
};

INSTANTIATE_TEST_CASE_P(BitSizes, SimdBp128Test, ::testing::Range(uint8_t{1}, uint8_t{33}), formatter);

TEST_P(SimdBp128Test, DecodeSequenceUsingIterators) {
  const auto sequence = generate_sequence(4'200);
  const auto encoded_sequence_base = encode(sequence);

  auto encoded_sequence = dynamic_cast<SimdBp128Vector*>(encoded_sequence_base.get());
  EXPECT_NE(encoded_sequence, nullptr);

  auto seq_it = sequence.cbegin();
  auto encoded_seq_it = encoded_sequence->cbegin();
  const auto encoded_seq_end = encoded_sequence->cend();
  for (; encoded_seq_it != encoded_seq_end; seq_it++, encoded_seq_it++) {
    EXPECT_EQ(*seq_it, *encoded_seq_it);
  }
}

TEST_P(SimdBp128Test, DecodeSequenceUsingDecoder) {
  const auto sequence = generate_sequence(4'200);
  const auto encoded_sequence = encode(sequence);

  auto decoder = encoded_sequence->create_base_decoder();

  auto seq_it = sequence.cbegin();
  const auto seq_end = sequence.cend();
  auto index = 0u;
  for (; seq_it != seq_end; seq_it++, index++) {
    EXPECT_EQ(*seq_it, decoder->get(index));
  }
}

TEST_P(SimdBp128Test, DecodeSequenceUsingDecodeMethod) {
  const auto sequence = generate_sequence(4'200);
  const auto encoded_sequence = encode(sequence);

  auto decoded_sequence = encoded_sequence->decode();

  auto seq_it = sequence.cbegin();
  const auto seq_end = sequence.cend();
  auto decoded_seq_it = decoded_sequence.cbegin();
  for (; seq_it != seq_end; seq_it++, decoded_seq_it++) {
    EXPECT_EQ(*seq_it, *decoded_seq_it);
  }
}

}  // namespace opossum