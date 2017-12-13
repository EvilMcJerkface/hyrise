#pragma once

#include <algorithm>
#include <limits>
#include <memory>

#include "base_column_encoder.hpp"

#include "storage/encoded_columns/dictionary_column.hpp"
#include "storage/value_column.hpp"
#include "storage/zero_suppression/utils.hpp"
#include "types.hpp"
#include "utils/enum_constant.hpp"

namespace opossum {

class DictionaryEncoder : public ColumnEncoder<DictionaryEncoder> {
 public:
  static constexpr auto _encoding_type = enum_c<EncodingType, EncodingType::Dictionary>;

  template <typename T>
  std::shared_ptr<BaseColumn> _encode(const std::shared_ptr<ValueColumn<T>>& value_column) {
    // See: https://goo.gl/MCM5rr
    // Create dictionary (enforce uniqueness and sorting)
    const auto& values = value_column->values();
    auto dictionary = pmr_vector<T>{values.cbegin(), values.cend(), values.get_allocator()};

    // Remove null values from value vector
    if (value_column->is_nullable()) {
      const auto& null_values = value_column->null_values();

      // Swap values to back if value is null
      auto erase_from_here_it = dictionary.end();
      auto null_it = null_values.crbegin();
      for (auto dict_it = dictionary.rbegin(); dict_it != dictionary.rend(); ++dict_it, ++null_it) {
        if (*null_it) {
          std::swap(*dict_it, *(--erase_from_here_it));
        }
      }

      // Erase null values
      dictionary.erase(erase_from_here_it, dictionary.end());
    }

    std::sort(dictionary.begin(), dictionary.end());
    dictionary.erase(std::unique(dictionary.begin(), dictionary.end()), dictionary.end());
    dictionary.shrink_to_fit();

    auto attribute_vector = pmr_vector<uint32_t>{values.get_allocator()};
    attribute_vector.reserve(values.size());

    const auto null_value_id = static_cast<uint32_t>(dictionary.size());

    if (value_column->is_nullable()) {
      const auto& null_values = value_column->null_values();

      /**
       * Iterators are used because values and null_values are of
       * type tbb::concurrent_vector and thus index-based access isn’t in O(1)
       */
      auto value_it = values.cbegin();
      auto null_value_it = null_values.cbegin();
      for (; value_it != values.cend(); ++value_it, ++null_value_it) {
        if (*null_value_it) {
          attribute_vector.push_back(null_value_id);
          continue;
        }

        const auto value_id = _get_value_id(dictionary, *value_it);
        attribute_vector.push_back(value_id);
      }
    } else {
      auto value_it = values.cbegin();
      for (; value_it != values.cend(); ++value_it) {
        const auto value_id = _get_value_id(dictionary, *value_it);
        attribute_vector.push_back(value_id);
      }
    }

    // We need to increment the dictionary size here because of possible null values.
    const auto zs_type = get_fixed_size_byte_aligned_encoding(dictionary.size() + 1u);

    auto encoded_attribute_vector = encode_by_zs_type(zs_type, attribute_vector, attribute_vector.get_allocator());

    auto dictionary_sptr = std::make_shared<pmr_vector<T>>(std::move(dictionary));
    auto attribute_vector_sptr = std::shared_ptr<BaseZeroSuppressionVector>(std::move(encoded_attribute_vector));
    return std::make_shared<DictionaryColumn<T>>(dictionary_sptr, attribute_vector_sptr, ValueID{null_value_id});
  }

 private:
  template <typename T>
  static ValueID _get_value_id(const pmr_vector<T>& dictionary, const T& value) {
    return static_cast<ValueID>(
        std::distance(dictionary.cbegin(), std::lower_bound(dictionary.cbegin(), dictionary.cend(), value)));
  }

  ZsType get_fixed_size_byte_aligned_encoding(size_t unique_values_count) {
    if (unique_values_count <= std::numeric_limits<uint8_t>::max()) {
      return ZsType::FixedSize1ByteAligned;
    } else if (unique_values_count <= std::numeric_limits<uint16_t>::max()) {
      return ZsType::FixedSize2ByteAligned;
    } else {
      return ZsType::FixedSize4ByteAligned;
    }
  }
};

}  // namespace opossum