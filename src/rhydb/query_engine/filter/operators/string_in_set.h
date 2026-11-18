#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>

#include <roaring/roaring.hh>

#include "rhydb/query_engine/filter/operators/selection.h"
#include "rhydb/storage/column/column.h"

namespace rhydb::query_engine::filter::operators {

template <storage::column::Column ColumnType>
class StringInSet : public Predicate {
  public:
   enum class Comparator : uint8_t { IN, NOT_IN };

  private:
   const ColumnType* column;
   std::unordered_set<std::string> values;
   Comparator comparator;
   /// Whether the rows where the column is null match
   bool with_nulls;

  public:
   explicit StringInSet(
      const ColumnType* column,
      Comparator comparator,
      std::unordered_set<std::string> values,
      bool with_nulls = false
   );

   ~StringInSet() noexcept override;

   [[nodiscard]] std::string toString() const override;
   [[nodiscard]] bool match(storage::column::RowId row_id) const override;

   [[nodiscard]] std::unique_ptr<Predicate> copy() const override;
   [[nodiscard]] std::unique_ptr<Predicate> negate() const override;
};

}  // namespace rhydb::query_engine::filter::operators
