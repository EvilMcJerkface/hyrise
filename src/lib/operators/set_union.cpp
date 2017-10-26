#include "set_union.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include "storage/chunk.hpp"
#include "storage/reference_column.hpp"
#include "storage/table.hpp"
#include "types.hpp"

/**
 * ### SetUnion implementation
 * The SetUnion Operator turns each input table into a ReferenceMatrix.
 * The rows in the ReferenceMatrices need to be sorted in order for them to be merged, that is,
 * performing the SetUnion  operation.
 * Since sorting a multi-column matrix by rows would require a lot of value-copying, for each ReferenceMatrix, a
 * VirtualPosList is created.
 * Each element of this VirtualPosList references a row in a ReferenceMatrix by index. This way, if two values need to
 * be swapped while sorting, only two indices need to swapped instead of a RowIDs for each column in the
 * ReferenceMatrices.
 * Using a implementation derived from std::set_union, the two virtual pos lists are merged into the result table.
 *
 *
 * ### About ReferenceMatrices
 * The ReferenceMatrix consists of N rows and C columns of RowIDs.
 * N is the same number as the number of rows in the input table.
 * Each of the C column can represent 1..S columns in the input table. All rows represented by a ReferenceMatrix-Column
 * contain the same PosList in each Chunk.
 *
 * The ReferenceMatrix of a StoredTable will only contain one column, the ReferenceMatrix of the result of a 3 way Join
 * will contain 3 columns
 *
 *
 * ### TODO(anybody) for performance improvements
 * Instead of using a ReferenceMatrix, consider using a linked list of RowIDs for each row. Since most of the sorting
 *      will depend on the leftmost column, this way most of the time no remote memory would need to be accessed
 *
 * The sorting, which is the most expensive part of this operator, could probably be parallelized.
 */

namespace {

// See doc above
using ReferenceMatrix = std::vector<opossum::PosList>;

using VirtualPosList = std::vector<size_t>;

/**
 * Comparator for performing the std::sort() of a virtual pos list
 */
struct VirtualPosListCmpContext {
  ReferenceMatrix& reference_matrix;
  bool operator()(size_t lhs, size_t rhs) const {
    for (const auto& reference_matrix_column : reference_matrix) {
      const auto left_row_id = reference_matrix_column[lhs];
      const auto right_row_id = reference_matrix_column[rhs];

      if (left_row_id < right_row_id) return true;
      if (right_row_id < left_row_id) return false;
    }
    return false;
  }
};

}  // namespace

namespace opossum {

SetUnion::SetUnion(const std::shared_ptr<const AbstractOperator>& left,
                   const std::shared_ptr<const AbstractOperator>& right)
    : AbstractReadOnlyOperator(left, right) {}

uint8_t SetUnion::num_in_tables() const { return 2; }

uint8_t SetUnion::num_out_tables() const { return 1; }

std::shared_ptr<AbstractOperator> SetUnion::recreate(const std::vector<AllParameterVariant>& args) const {
  return std::make_shared<SetUnion>(input_left()->recreate(args), input_right()->recreate(args));
}

const std::string SetUnion::name() const { return "SetUnion"; }

const std::string SetUnion::description() const { return "SetUnion"; }

std::shared_ptr<const Table> SetUnion::_on_execute() {
  const auto early_result = _analyze_input();
  if (early_result) {
    return early_result;
  }

  /**
   * For each input, create a ReferenceMatrix
   */
  const auto build_reference_matrix = [&](const auto& input_table, auto& reference_matrix) {
    reference_matrix.resize(_column_segment_begins.size());
    for (auto& pos_list : reference_matrix) {
      pos_list.reserve(input_table->row_count());
    }

    for (auto chunk_id = ChunkID{0}; chunk_id < input_table->chunk_count(); ++chunk_id) {
      const auto& chunk = input_table->get_chunk(ChunkID{chunk_id});

      for (size_t segment_id = 0; segment_id < _column_segment_begins.size(); ++segment_id) {
        const auto column_id = _column_segment_begins[segment_id];
        const auto column = chunk.get_column(column_id);
        const auto ref_column = std::dynamic_pointer_cast<const ReferenceColumn>(column);

        auto& out_pos_list = reference_matrix[segment_id];
        auto in_pos_list = ref_column->pos_list();
        std::copy(in_pos_list->begin(), in_pos_list->end(), std::back_inserter(out_pos_list));
      }
    }
  };

  ReferenceMatrix reference_matrix_left;
  ReferenceMatrix reference_matrix_right;

  build_reference_matrix(_input_table_left(), reference_matrix_left);
  build_reference_matrix(_input_table_right(), reference_matrix_right);

  /**
   * Init the virtual pos lists
   */
  VirtualPosList virtual_pos_list_left(_input_table_left()->row_count(), 0u);
  std::iota(virtual_pos_list_left.begin(), virtual_pos_list_left.end(), 0u);
  VirtualPosList virtual_pos_list_right(_input_table_right()->row_count(), 0u);
  std::iota(virtual_pos_list_right.begin(), virtual_pos_list_right.end(), 0u);

  /**
   * Sort the virtual pos lists, so we can merge them
   */
  std::sort(virtual_pos_list_left.begin(), virtual_pos_list_left.end(),
            VirtualPosListCmpContext{reference_matrix_left});
  std::sort(virtual_pos_list_right.begin(), virtual_pos_list_right.end(),
            VirtualPosListCmpContext{reference_matrix_right});

  /**
   * Build result table
   */
  auto left_idx = size_t{0};
  auto right_idx = size_t{0};
  const auto num_left = virtual_pos_list_left.size();
  const auto num_right = virtual_pos_list_right.size();

  // Somewhat random way to decide on a chunk size.
  const auto out_chunk_size = std::max(_input_table_left()->chunk_size(), _input_table_right()->chunk_size());

  auto out_table = Table::create_with_layout_from(_input_table_left(), out_chunk_size);

  std::vector<std::shared_ptr<PosList>> pos_lists(reference_matrix_left.size());
  std::generate(pos_lists.begin(), pos_lists.end(), [&] { return std::make_shared<PosList>(); });

  // Adds one row to the pos_lists currently being filled
  const auto emit_row = [&](const ReferenceMatrix& reference_matrix, size_t row_idx) {
    for (size_t pos_list_idx = 0; pos_list_idx < pos_lists.size(); ++pos_list_idx) {
      pos_lists[pos_list_idx]->emplace_back(reference_matrix[pos_list_idx][row_idx]);
    }
  };

  // Turn 'pos_lists' into a new chunk and append it to the table
  const auto emit_chunk = [&]() {
    Chunk chunk;

    for (size_t pos_lists_idx = 0; pos_lists_idx < pos_lists.size(); ++pos_lists_idx) {
      const auto segment_column_id_begin = _column_segment_begins[pos_lists_idx];
      const auto segment_column_id_end = pos_lists_idx >= _column_segment_begins.size() - 1
                                             ? _input_table_left()->column_count()
                                             : _column_segment_begins[pos_lists_idx + 1];
      for (auto column_id = segment_column_id_begin; column_id < segment_column_id_end; ++column_id) {
        auto ref_column = std::make_shared<ReferenceColumn>(
            _referenced_tables[pos_lists_idx], _referenced_column_ids[column_id], pos_lists[pos_lists_idx]);
        chunk.add_column(ref_column);
      }
    }

    out_table->emplace_chunk(std::move(chunk));
  };

  // Comparator of two ReferenceMatrix rows
  const auto cmp = [](const auto& matrix_a, const auto idx_a, const auto& matrix_b, const auto idx_b) {
    for (size_t column_idx = 0; column_idx < matrix_a.size(); ++column_idx) {
      if (matrix_a[column_idx][idx_a] < matrix_b[column_idx][idx_b]) return true;
      if (matrix_b[column_idx][idx_b] < matrix_a[column_idx][idx_a]) return false;
    }
    return false;
  };

  size_t chunk_row_idx = 0;
  for (; left_idx < num_left || right_idx < num_right;) {
    /**
     * Begin derived from std::union()
     */
    if (left_idx == num_left) {
      emit_row(reference_matrix_right, virtual_pos_list_right[right_idx]);
      ++right_idx;
    } else if (right_idx == num_right) {
      emit_row(reference_matrix_left, virtual_pos_list_left[left_idx]);
      ++left_idx;
    } else if (cmp(reference_matrix_right, virtual_pos_list_right[right_idx], reference_matrix_left,
                   virtual_pos_list_left[left_idx])) {
      emit_row(reference_matrix_right, virtual_pos_list_right[right_idx]);
      ++right_idx;
    } else {
      emit_row(reference_matrix_left, virtual_pos_list_left[left_idx]);

      if (!cmp(reference_matrix_left, virtual_pos_list_left[left_idx], reference_matrix_right,
               virtual_pos_list_right[right_idx])) {
        ++right_idx;
      }
      ++left_idx;
    }
    ++chunk_row_idx;
    /**
     * End derived from std::union()
     */

    /**
     * Emit a completed chunk
     */
    if (chunk_row_idx == out_chunk_size && out_chunk_size != 0) {
      emit_chunk();

      chunk_row_idx = 0;
      std::generate(pos_lists.begin(), pos_lists.end(), [&] { return std::make_shared<PosList>(); });
    }
  }

  if (chunk_row_idx != 0) {
    emit_chunk();
  }

  return out_table;
}

std::shared_ptr<const Table> SetUnion::_analyze_input() {
  Assert(_input_table_left()->column_count() == _input_table_right()->column_count(),
         "Input tables must have the same layout. Column count mismatch.");

  // Later code relies on input tables containing columns
  if (_input_table_left()->column_count() == 0) {
    return _input_table_left();
  }

  /**
   * Check the column layout (column names and column types)
   */
  for (ColumnID::base_type column_idx = 0; column_idx < _input_table_left()->column_count(); ++column_idx) {
    Assert(_input_table_left()->column_type(ColumnID{column_idx}) ==
               _input_table_right()->column_type(ColumnID{column_idx}),
           "Input tables must have the same layout. Column type mismatch.");
    Assert(_input_table_left()->column_name(ColumnID{column_idx}) ==
               _input_table_right()->column_name(ColumnID{column_idx}),
           "Input tables must have the same layout. Column name mismatch.");
  }

  /**
   * Later code relies on both tables having > 0 rows. If one doesn't, we can just return the other as the result of
   * the operator
   */
  if (_input_table_left()->row_count() == 0) {
    return _input_table_right();
  }
  if (_input_table_right()->row_count() == 0) {
    return _input_table_left();
  }

  /**
   * Both tables must contain only ReferenceColumns
   */
  Assert(_input_table_left()->get_type() == TableType::References &&
             _input_table_right()->get_type() == TableType::References,
         "SetUnion doesn't support non-reference tables yet");

  /**
   * Identify the column segments (verification that this is the same for all chunks happens in the #if IS_DEBUG block
   * below)
   */
  const auto add_column_segments = [&](const auto& table) {
    auto current_pos_list = std::shared_ptr<const PosList>();
    const auto& first_chunk = table->get_chunk(ChunkID{0});
    for (auto column_id = ColumnID{0}; column_id < table->column_count(); ++column_id) {
      const auto column = first_chunk.get_column(column_id);
      const auto ref_column = std::dynamic_pointer_cast<const ReferenceColumn>(column);
      auto pos_list = ref_column->pos_list();

      if (current_pos_list != pos_list) {
        current_pos_list = pos_list;
        _column_segment_begins.emplace_back(column_id);
      }
    }
  };

  add_column_segments(_input_table_left());
  add_column_segments(_input_table_right());

  std::sort(_column_segment_begins.begin(), _column_segment_begins.end());
  const auto unique_end_iter = std::unique(_column_segment_begins.begin(), _column_segment_begins.end());
  _column_segment_begins.resize(std::distance(_column_segment_begins.begin(), unique_end_iter));

  /**
   * Identify the tables referenced in each column segment (verification that this is the same for all chunks happens
   * in the #if IS_DEBUG block below)
   */
  const auto& first_chunk_left = _input_table_left()->get_chunk(ChunkID{0});
  for (const auto& segment_begin : _column_segment_begins) {
    const auto column = first_chunk_left.get_column(segment_begin);
    const auto ref_column = std::dynamic_pointer_cast<const ReferenceColumn>(column);
    _referenced_tables.emplace_back(ref_column->referenced_table());
  }

  /**
   * Identify the column_ids referenced by each column (verification that this is the same for all chunks happens
   * in the #if IS_DEBUG block below)
   */
  for (auto column_id = ColumnID{0}; column_id < _input_table_left()->column_count(); ++column_id) {
    const auto column = first_chunk_left.get_column(column_id);
    const auto ref_column = std::dynamic_pointer_cast<const ReferenceColumn>(column);
    _referenced_column_ids.emplace_back(ref_column->referenced_column_id());
  }

#if IS_DEBUG
  /**
   * Make sure all chunks have the same column segments and actually reference the tables and column_ids that the
   * segments in the first chunk of the left input table reference
   */
  const auto verify_column_segments_in_all_chunks = [&](const auto& table) {
    for (auto chunk_id = ChunkID{0}; chunk_id < table->chunk_count(); ++chunk_id) {
      auto current_pos_list = std::shared_ptr<const PosList>();
      size_t next_segment_id = 0;
      const auto& chunk = table->get_chunk(chunk_id);
      for (auto column_id = ColumnID{0}; column_id < table->column_count(); ++column_id) {
        if (column_id == _column_segment_begins[next_segment_id]) {
          next_segment_id++;
          current_pos_list = nullptr;
        }

        const auto column = chunk.get_column(column_id);
        const auto ref_column = std::dynamic_pointer_cast<const ReferenceColumn>(column);
        auto pos_list = ref_column->pos_list();

        if (current_pos_list == nullptr) {
          current_pos_list = pos_list;
        }

        Assert(ref_column->referenced_table() == _referenced_tables[next_segment_id - 1],
               "ReferenceColumn (Chunk: " + std::to_string(chunk_id) + ", Column: " + std::to_string(column_id) +
                   ") "
                   "doesn't reference the same table as the column at the same index in the first chunk "
                   "of the left input table does");
        Assert(ref_column->referenced_column_id() == _referenced_column_ids[column_id],
               "ReferenceColumn (Chunk: " + std::to_string(chunk_id) + ", Column: " + std::to_string(column_id) +
                   ")"
                   " doesn't reference the same table as the column at the same index in the first chunk "
                   "of the left input table does");
        Assert(current_pos_list == pos_list, "Different PosLists in column segment");
      }
    }
  };

  verify_column_segments_in_all_chunks(_input_table_left());
  verify_column_segments_in_all_chunks(_input_table_right());
#endif

  return nullptr;
}
}  // namespace opossum
