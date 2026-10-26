#pragma once

#include "rhydb/common/panic.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/storage/column/bool_column.h"
#include "rhydb/storage/column/column.h"
#include "rhydb/storage/column/date32_column.h"
#include "rhydb/storage/column/dictionary_encoded_column.h"
#include "rhydb/storage/column/float_column.h"
#include "rhydb/storage/column/int_column.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/column/zstd_compressed_string_column.h"

namespace rhydb::storage::column {

/// Calls `func.template operator()<Column>(args...)` with the column class storing `type`
template <typename VisitorFunction, typename... Args>
static decltype(auto) visit(const schema::DataType& type, VisitorFunction&& func, Args&&... args) {
   using Kind = schema::DataType::Kind;
   switch (type.getKind()) {
      case Kind::STRING:
         return func.template operator()<StringColumn>(std::forward<Args>(args)...);
      case Kind::DICTIONARY_ENCODED:
         if (type.getDictionaryValueType() == schema::DataType::DATE32) {
            return func.template operator()<DictionaryEncodedColumn<Date32Column>>(
               std::forward<Args>(args)...
            );
         }
         RHYDB_ASSERT(type.getDictionaryValueType() == schema::DataType::STRING);
         return func.template operator()<DictionaryEncodedColumn<StringColumn>>(
            std::forward<Args>(args)...
         );
      case Kind::DATE32:
         return func.template operator()<Date32Column>(std::forward<Args>(args)...);
      case Kind::BOOL:
         return func.template operator()<BoolColumn>(std::forward<Args>(args)...);
      case Kind::INT32:
         return func.template operator()<Int32Column>(std::forward<Args>(args)...);
      case Kind::INT64:
         return func.template operator()<Int64Column>(std::forward<Args>(args)...);
      case Kind::FLOAT:
         return func.template operator()<FloatColumn>(std::forward<Args>(args)...);
      case Kind::NUCLEOTIDE_SEQUENCE:
         return func.template operator()<SequenceColumn<Nucleotide>>(std::forward<Args>(args)...);
      case Kind::AMINO_ACID_SEQUENCE:
         return func.template operator()<SequenceColumn<AminoAcid>>(std::forward<Args>(args)...);
      case Kind::ZSTD_COMPRESSED_STRING:
         return func.template operator()<ZstdCompressedStringColumn>(std::forward<Args>(args)...);
   }
   RHYDB_UNREACHABLE();
}

}  // namespace rhydb::storage::column
