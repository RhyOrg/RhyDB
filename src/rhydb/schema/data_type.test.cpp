#include "rhydb/schema/data_type.h"

#include <optional>
#include <sstream>

#include <gtest/gtest.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>

using rhydb::schema::DataType;
using rhydb::schema::DataTypePtr;
using rhydb::schema::DictionaryType;
using rhydb::schema::SequenceType;
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
   EXPECT_EQ(
      rhydb::schema::cast<DictionaryType>(dictionary_string)->getValueType(), DataType::STRING
   );
   EXPECT_EQ(dictionary_string->getPlainType(), DataType::STRING);
   EXPECT_EQ(DataType::INT32->getPlainType(), DataType::INT32);

   EXPECT_EQ(dictionary_string, DictionaryType::STRING);
   EXPECT_EQ(DataType::dictionaryEncoded(*DataType::DATE32), DictionaryType::DATE32);
   EXPECT_NE(*dictionary_string, *DataType::STRING);
   EXPECT_NE(*DictionaryType::STRING, *DictionaryType::DATE32);
}

TEST(DataType, isAnInstanceOfTheSubclassOfItsKind) {
   using rhydb::schema::AminoAcidSequenceType;
   using rhydb::schema::cast;
   using rhydb::schema::dynCast;
   using rhydb::schema::Int32Type;
   using rhydb::schema::isA;
   using rhydb::schema::NucleotideSequenceType;
   using rhydb::schema::StringType;

   EXPECT_TRUE(isA<Int32Type>(DataType::INT32));
   EXPECT_FALSE(isA<Int32Type>(DataType::INT64));
   EXPECT_FALSE(isA<StringType>(DictionaryType::STRING));
   EXPECT_TRUE(isA<DictionaryType>(DictionaryType::STRING));
   EXPECT_FALSE(isA<DictionaryType>(DataTypePtr{}));

   EXPECT_TRUE(isA<SequenceType>(DataType::nucleotideSequence(3)));
   EXPECT_TRUE(isA<SequenceType>(DataType::aminoAcidSequence(3)));
   EXPECT_FALSE(isA<SequenceType>(DataType::STRING));
   EXPECT_TRUE(isA<NucleotideSequenceType>(DataType::nucleotideSequence(3)));
   EXPECT_FALSE(isA<AminoAcidSequenceType>(DataType::nucleotideSequence(3)));

   EXPECT_EQ(cast<DictionaryType>(DictionaryType::DATE32)->getValueType(), DataType::DATE32);
   EXPECT_EQ(dynCast<DictionaryType>(DictionaryType::DATE32), DictionaryType::DATE32.get());
   EXPECT_EQ(dynCast<DictionaryType>(DataType::DATE32), nullptr);
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
   for (const Kind kind : {
           Kind::STRING,
           Kind::DATE32,
           Kind::BOOL,
           Kind::INT32,
           Kind::INT64,
           Kind::FLOAT,
           Kind::NUCLEOTIDE_SEQUENCE,
           Kind::AMINO_ACID_SEQUENCE,
           Kind::ZSTD_COMPRESSED_STRING,
        }) {
      EXPECT_EQ(fromSchemaTypeName(toSchemaTypeName(kind)), kind);
   }
   EXPECT_EQ(toSchemaTypeName(Kind::INT32), "int32");
   EXPECT_EQ(fromSchemaTypeName("int"), Kind::INT32);
   EXPECT_EQ(fromSchemaTypeName("unknown"), std::nullopt);
}

TEST(DataType, onlyNonParameterizedKindsHaveATypeOfTheirOwn) {
   using rhydb::schema::nonParameterizedDataType;
   EXPECT_EQ(nonParameterizedDataType(Kind::INT64), DataType::INT64);
   EXPECT_EQ(nonParameterizedDataType(Kind::NUCLEOTIDE_SEQUENCE), nullptr);
   EXPECT_EQ(nonParameterizedDataType(Kind::DICTIONARY_ENCODED), nullptr);
}

TEST(SequenceType, hasItsLength) {
   const DataTypePtr nucleotide = DataType::nucleotideSequence(29903);
   EXPECT_EQ(nucleotide->getKind(), Kind::NUCLEOTIDE_SEQUENCE);
   ASSERT_TRUE(rhydb::schema::isA<SequenceType>(nucleotide));
   EXPECT_EQ(rhydb::schema::cast<SequenceType>(nucleotide)->getLength(), 29903U);
   EXPECT_EQ(DataType::aminoAcidSequence(7)->getKind(), Kind::AMINO_ACID_SEQUENCE);
   EXPECT_FALSE(rhydb::schema::isA<SequenceType>(DataType::STRING));
   EXPECT_FALSE(rhydb::schema::isA<SequenceType>(DictionaryType::STRING));
}

TEST(SequenceType, existsOncePerKindAndLength) {
   EXPECT_EQ(DataType::nucleotideSequence(10), DataType::nucleotideSequence(10));
   EXPECT_NE(DataType::nucleotideSequence(10), DataType::nucleotideSequence(11));
   EXPECT_NE(DataType::nucleotideSequence(10), DataType::aminoAcidSequence(10));
}

TEST(SequenceType, isOrderedByKindAndThenByLength) {
   EXPECT_LT(*DataType::aminoAcidSequence(100), *DataType::nucleotideSequence(1));
   EXPECT_LT(*DataType::nucleotideSequence(1), *DataType::nucleotideSequence(2));
   EXPECT_EQ(*DataType::nucleotideSequence(2), *DataType::nucleotideSequence(2));
}

TEST(SequenceType, survivesSerialization) {
   EXPECT_EQ(roundTrip(DataType::nucleotideSequence(29903)), DataType::nucleotideSequence(29903));
   EXPECT_EQ(roundTrip(DataType::aminoAcidSequence(1273)), DataType::aminoAcidSequence(1273));
}

TEST(SequenceType, hasDisplayNames) {
   EXPECT_EQ(dataTypeToString(DataType::nucleotideSequence(5)), "NUCLEOTIDE_SEQUENCE(5)");
   EXPECT_EQ(dataTypeToString(DataType::aminoAcidSequence(3)), "AMINO_ACID_SEQUENCE(3)");
}
