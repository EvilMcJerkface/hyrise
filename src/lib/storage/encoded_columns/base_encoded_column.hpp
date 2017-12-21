#pragma once

#include "column_encoding_type.hpp"

#include "storage/base_column.hpp"

namespace opossum {

/**
 * @brief Base class of all encoded columns
 *
 * Since encoded columns are immutable, all member variables
 * of sub-classes should be declared const.
 */
class BaseEncodedColumn : public BaseColumn {
 public:
  // Encoded columns are immutable
  void append(const AllTypeVariant&) final;

  virtual EncodingType encoding_type() const = 0;
};

}  // namespace opossum
