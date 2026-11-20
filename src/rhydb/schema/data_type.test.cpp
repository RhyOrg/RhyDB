#include "rhydb/schema/data_type.h"

#include <optional>
#include <sstream>

#include <gtest/gtest.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>

using rhydb::schema::DataType;
using rhydb::schema::DataTypePtr;
using rhydb::schema::DictionaryType;
using Kind = rhydb::schema::DataType::Kind;
using rhydb::schema::dataTypeToString;

namespace {

DataTypePtr roundTrip(const DataTypePtr& type) {
   std::stringstream buffer;
   {
      boost::archive::binary_oarchive output{buffer};
      rhydb::schema::saveDataType(output, *type);
   }
   boost::archive::binary_iarchive input{buffer};
   return rhydb::schema::loadDataType(input);
}

}  // namespace

TEST(DataType, dictionaryEncodedTypeHasItsValueType) {
   const DataTypePtr& dictionary_string = DataType::dictionaryEncoded(*DataType::STRING);
   EXPECT_EQ(dictionary_string->getKind(), Kind::DICTIONARY_ENCODED);
   EXPECT_EQ(dictionary_string->as<DictionaryType>().getValueType(), DataType::STRING);
   EXPECT_EQ(dictionary_string->getPlainType(), DataType::STRING);
   EXPECT_EQ(DataType::INT32->getPlainType(), DataType::INT32);

   EXPECT_EQ(dictionary_string, DictionaryType::STRING);
   EXPECT_EQ(DataType::dictionaryEncoded(*DataType::DATE32), DictionaryType::DATE32);
   EXPECT_NE(*dictionary_string, *DataType::STRING);
   EXPECT_NE(*DictionaryType::STRING, *DictionaryType::DATE32);
}

TEST(DataType, isOrderedByKindAndThenByValueType) {
   EXPECT_LT(*DataType::STRING, *DictionaryType::STRING);
   EXPECT_LT(*DictionaryType::STRING, *DictionaryType::DATE32);
   EXPECT_LT(*DictionaryType::DATE32, *DataType::DATE32);
}

TEST(DataType, canOnlyDictionaryEncodeStringsAndDates) {
   EXPECT_TRUE(DataType::canBeDictionaryEncoded(*DataType::STRING));
   EXPECT_TRUE(DataType::canBeDictionaryEncoded(*DataType::DATE32));
   EXPECT_FALSE(DataType::canBeDictionaryEncoded(*DataType::INT32));
   EXPECT_FALSE(DataType::canBeDictionaryEncoded(*DictionaryType::STRING));
}

TEST(DataType, survivesSerialization) {
   EXPECT_EQ(roundTrip(DataType::DATE32), DataType::DATE32);
   EXPECT_EQ(roundTrip(DictionaryType::STRING), DictionaryType::STRING);
   EXPECT_EQ(roundTrip(DictionaryType::DATE32), DictionaryType::DATE32);
}

TEST(DataType, hasDisplayNames) {
   EXPECT_EQ(dataTypeToString(DataType::ZSTD_COMPRESSED_STRING), "ZSTD_COMPRESSED_STRING");
   EXPECT_EQ(dataTypeToString(DictionaryType::STRING), "DICTIONARY_ENCODED(STRING)");
}

TEST(DataType, hasSchemaTypeNames) {
   using rhydb::schema::fromSchemaTypeName;
   using rhydb::schema::toSchemaTypeName;
   for (const DataTypePtr& type : {
           DataType::STRING,
           DataType::DATE32,
           DataType::BOOL,
           DataType::INT32,
           DataType::INT64,
           DataType::FLOAT,
           DataType::NUCLEOTIDE_SEQUENCE,
           DataType::AMINO_ACID_SEQUENCE,
           DataType::ZSTD_COMPRESSED_STRING,
        }) {
      EXPECT_EQ(fromSchemaTypeName(toSchemaTypeName(*type)), type);
   }
   EXPECT_EQ(toSchemaTypeName(*DataType::INT32), "int32");
   EXPECT_EQ(fromSchemaTypeName("int"), DataType::INT32);
   EXPECT_EQ(fromSchemaTypeName("unknown"), std::nullopt);
}
