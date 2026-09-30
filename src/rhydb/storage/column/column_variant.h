#pragma once

#include <variant>

#include "rhydb/common/aa_symbols.h"
#include "rhydb/common/nucleotide_symbols.h"
#include "rhydb/storage/column/bool_column.h"
#include "rhydb/storage/column/date32_column.h"
#include "rhydb/storage/column/dictionary_encoded_column.h"
#include "rhydb/storage/column/float_column.h"
#include "rhydb/storage/column/int_column.h"
#include "rhydb/storage/column/sequence_column.h"
#include "rhydb/storage/column/string_column.h"
#include "rhydb/storage/column/zstd_compressed_string_column.h"

namespace rhydb::storage::column {

/// Any column a table can hold. The alternative always matches the column's `schema::ColumnType`.
using ColumnVariant = std::variant<
   StringColumn,
   DictionaryEncodedColumn,
   BoolColumn,
   Int32Column,
   Int64Column,
   FloatColumn,
   Date32Column,
   SequenceColumn<Nucleotide>,
   SequenceColumn<AminoAcid>,
   ZstdCompressedStringColumn>;

}  // namespace rhydb::storage::column
