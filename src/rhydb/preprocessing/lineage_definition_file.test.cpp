#include "rhydb/preprocessing/lineage_definition_file.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <yaml-cpp/exceptions.h>

#include "rhydb/preprocessing/preprocessing_exception.h"

using rhydb::preprocessing::LineageDefinitionFile;

TEST(LineageDefinitionFile, correctlyParsesFromYAML) {
   auto lineage_definition_file = LineageDefinitionFile::fromYAMLString(R"(
BASE:
  parents: []
CHILD:
  parents:
    - BASE
)");
   ASSERT_EQ(lineage_definition_file.lineages.size(), 2);
   ASSERT_EQ(lineage_definition_file.lineages.at(0).lineage_name.string, "BASE");
   ASSERT_TRUE(lineage_definition_file.lineages.at(0).parents.empty());
   ASSERT_EQ(lineage_definition_file.lineages.at(1).lineage_name.string, "CHILD");
   ASSERT_EQ(lineage_definition_file.lineages.at(1).parents.size(), 1);
   ASSERT_EQ(lineage_definition_file.lineages.at(1).parents.at(0).string, "BASE");
}

TEST(LineageDefinitionFile, unparsableOnBadFormat) {
   ASSERT_THROW(
      LineageDefinitionFile::fromYAMLString(R"(
X
SOME_lineage: "
-:
)"),
      YAML::Exception
   );
}

TEST(LineageDefinitionFile, errorOnMisspelledParents) {
   auto throwing_lambda = [] {
      LineageDefinitionFile::fromYAMLString(R"(
some_lineage:
  parent:
  - anything
some_other_lineage:
  parent:
  - also_anything)");
   };

   EXPECT_THAT(
      throwing_lambda,
      ThrowsMessage<rhydb::preprocessing::PreprocessingException>(::testing::HasSubstr(
         "The definition of lineage 'some_lineage' may only contain the "
         "fields 'parents' and 'aliases', it also contains invalid fields"
      ))
   );
}

TEST(LineageDefinitionFile, noErrorOnEmptyMap) {
   EXPECT_NO_THROW(LineageDefinitionFile::fromYAMLString(R"(
some_lineage: {}
some_other_lineage:
  parents:
  - some_lineage)"));
}

TEST(LineageDefinitionFile, noErrorOnNull) {
   EXPECT_NO_THROW(LineageDefinitionFile::fromYAMLString(R"(
some_lineage:
some_other_lineage:
  parents:
  - some_lineage)"));
}

TEST(LineageDefinitionFile, errorOnExtraFields) {
   auto throwing_lambda = [] {
      LineageDefinitionFile::fromYAMLString(R"(
some_lineage:
  parents: []
  some_extra_field: "some_value"
some_other_lineage:
  parents:
  - also_anything)");
   };

   EXPECT_THAT(
      throwing_lambda,
      ThrowsMessage<rhydb::preprocessing::PreprocessingException>(::testing::HasSubstr(
         "The definition of lineage 'some_lineage' may only contain the fields 'parents' and "
         "'aliases', it also contains invalid fields:\nparents: []\nsome_extra_field: "
         "some_value"
      ))
   );
}
