#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "rhydb/common/bitmap.h"
#include "rhydb/query_engine/filter/operators/operator.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/column.h"
#include "rhydb/storage/column/row_layout.h"
#include "rhydb/storage/column/string_column.h"

namespace rhydb::query_engine::scalar_expressions {
class And;
}

namespace rhydb::query_engine::filter::operators {

class Predicate {
  public:
   virtual ~Predicate() noexcept = default;

   [[nodiscard]] virtual std::string toString() const = 0;
   [[nodiscard]] virtual bool match(storage::column::RowId row_id) const = 0;
   // Often there are faster ways to generate the results, than calling match on each row.
   // Optimise that case by overriding this method
   [[nodiscard]] virtual roaring::Roaring makeBitmap(
      const storage::column::RowLayout& row_layout
   ) const {
      roaring::Roaring result;
      for (const storage::column::RowId row_id : row_layout) {
         if (match(row_id)) {
            result.add(row_id.toGlobal());
         }
      }
      return result;
   };
   [[nodiscard]] virtual double estimateSelectivity(uint32_t /*row_count*/) const { return 0.5; }
   [[nodiscard]] virtual std::unique_ptr<Predicate> copy() const = 0;
   [[nodiscard]] virtual std::unique_ptr<Predicate> negate() const = 0;
};

using PredicateVector = std::vector<std::unique_ptr<Predicate>>;

enum class Comparator : uint8_t {
   EQUALS,
   LESS,
   HIGHER,
   LESS_OR_EQUALS,
   HIGHER_OR_EQUALS,
   NOT_EQUALS,
};

inline std::string_view displayComparator(Comparator comparator) {
   switch (comparator) {
      case Comparator::EQUALS:
         return "=";
      case Comparator::NOT_EQUALS:
         return "<>";
      case Comparator::LESS:
         return "<";
      case Comparator::HIGHER:
         return ">";
      case Comparator::LESS_OR_EQUALS:
         return "<=";
      case Comparator::HIGHER_OR_EQUALS:
         return ">=";
   }
   RHYDB_UNREACHABLE();
}

template <storage::column::Column ColumnType>
class CompareToValueSelection : public Predicate {
   using OwnedValue = std::conditional_t<
      std::is_same_v<typename ColumnType::value_type, std::string_view>,
      std::string,
      typename ColumnType::value_type>;

   const ColumnType& column;
   Comparator comparator;
   OwnedValue value;
   bool with_nulls;

  public:
   CompareToValueSelection(
      const ColumnType& column,
      Comparator comparator,
      ColumnType::value_type value,
      bool with_nulls
   )
       : column(column),
         comparator(comparator),
         value(value),
         with_nulls(with_nulls) {}

   CompareToValueSelection(
      const ColumnType& column,
      Comparator comparator,
      ColumnType::value_type value
   )
       : CompareToValueSelection(column, comparator, value, false) {}

   [[nodiscard]] std::string toString() const override {
      return fmt::format(
         "${} {} {} {}",
         schema::columnTypeToString(ColumnType::TYPE),
         column.metadata->column_name,
         displayComparator(comparator),
         value
      );
   }

   [[nodiscard]] bool match(storage::column::RowId row_id) const override {
      if (column.isNull(row_id)) {
         return with_nulls;
      }
      switch (comparator) {
         case Comparator::EQUALS:
            return column.getValue(row_id) == value;
         case Comparator::NOT_EQUALS:
            return column.getValue(row_id) != value;
         case Comparator::LESS:
            return column.getValue(row_id) < value;
         case Comparator::HIGHER_OR_EQUALS:
            return column.getValue(row_id) >= value;
         case Comparator::HIGHER:
            return column.getValue(row_id) > value;
         case Comparator::LESS_OR_EQUALS:
            return column.getValue(row_id) <= value;
      }
      RHYDB_UNREACHABLE();
   }

   [[nodiscard]] std::unique_ptr<Predicate> copy() const override {
      return std::make_unique<CompareToValueSelection<ColumnType>>(column, comparator, value);
   }

   [[nodiscard]] std::unique_ptr<Predicate> negate() const override {
      switch (comparator) {
         case Comparator::EQUALS:
            return std::make_unique<CompareToValueSelection>(
               column, Comparator::NOT_EQUALS, value, !with_nulls
            );
         case Comparator::NOT_EQUALS:
            return std::make_unique<CompareToValueSelection>(
               column, Comparator::EQUALS, value, !with_nulls
            );
         case Comparator::LESS:
            return std::make_unique<CompareToValueSelection>(
               column, Comparator::HIGHER_OR_EQUALS, value, !with_nulls
            );
         case Comparator::HIGHER_OR_EQUALS:
            return std::make_unique<CompareToValueSelection>(
               column, Comparator::LESS, value, !with_nulls
            );
         case Comparator::HIGHER:
            return std::make_unique<CompareToValueSelection>(
               column, Comparator::LESS_OR_EQUALS, value, !with_nulls
            );
         case Comparator::LESS_OR_EQUALS:
            return std::make_unique<CompareToValueSelection>(
               column, Comparator::HIGHER, value, !with_nulls
            );
      }
      RHYDB_UNREACHABLE();
   }
};

template <>
bool CompareToValueSelection<rhydb::storage::column::StringColumn>::match(
   rhydb::storage::column::RowId row_id
) const;

class Selection : public Operator {
   friend class scalar_expressions::And;

  private:
   std::optional<std::unique_ptr<Operator>> child_operator;
   std::vector<std::unique_ptr<Predicate>> predicates;
   storage::column::RowLayout row_layout;

   Selection(
      std::optional<std::unique_ptr<Operator>> child_operator,
      std::vector<std::unique_ptr<Predicate>>&& predicates,
      storage::column::RowLayout row_layout
   );

  public:
   Selection(
      std::unique_ptr<Operator>&& child_operator,
      std::vector<std::unique_ptr<Predicate>>&& predicates,
      storage::column::RowLayout row_layout
   );

   Selection(
      std::unique_ptr<Operator>&& child_operator,
      std::unique_ptr<Predicate> predicate,
      storage::column::RowLayout row_layout
   );

   Selection(
      std::vector<std::unique_ptr<Predicate>>&& predicates,
      storage::column::RowLayout row_layout
   );

   Selection(std::unique_ptr<Predicate> predicate, storage::column::RowLayout row_layout);

   ~Selection() noexcept override;

   [[nodiscard]] Type type() const override;

   [[nodiscard]] Bitmap evaluate() const override;

   [[nodiscard]] std::string toString() const override;

   static std::unique_ptr<Operator> negate(std::unique_ptr<Selection>&& selection);

  private:
   template <std::ranges::range PredicateRange>
   [[nodiscard]] static bool matchesPredicates(
      const PredicateRange& predicates,
      storage::column::RowId row_id
   ) {
      return std::ranges::all_of(predicates, [row_id](const auto& predicate) {
         return predicate->match(row_id);
      });
   }
};

}  // namespace rhydb::query_engine::filter::operators
