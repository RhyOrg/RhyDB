#include "rhydb/preprocessing/lineage_definition_file.h"

#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <yaml-cpp/exceptions.h>
#include <yaml-cpp/yaml.h>

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

namespace {

/// The lineages of `yaml` as `fromYAMLString` reads them (with the streaming parser where it
/// can), or the error message, next to those of the node-based `fromYAML`
std::pair<std::string, std::string> parsedBothWays(const std::string& yaml) {
   const auto describe = [](const LineageDefinitionFile& file) {
      std::string description;
      for (const auto& lineage : file.lineages) {
         description += lineage.lineage_name.string + " parents:";
         for (const auto& parent : lineage.parents) {
            description += ' ' + parent.string;
         }
         description += " aliases:";
         for (const auto& alias : lineage.aliases) {
            description += ' ' + alias.string;
         }
         description += '\n';
      }
      return description;
   };
   const auto parse = [&](const auto& parse_function) -> std::string {
      try {
         return describe(parse_function());
      } catch (const std::exception& exception) {
         return std::string{"error: "} + exception.what();
      }
   };
   return {
      parse([&] { return LineageDefinitionFile::fromYAMLString(yaml); }),
      parse([&] { return LineageDefinitionFile::fromYAML(YAML::Load(yaml)); }),
   };
}

}  // namespace

TEST(LineageDefinitionFile, readsDefinitionsLikeTheNodeBasedParser) {
   const std::vector<std::string> definitions{
      "",
      "A:\n",
      "A: {}\nB:\n  parents: [A]\n",
      "A:\n  aliases:\n    - a\n    - \"quoted: alias\"\nB:\n  aliases: [b]\n  parents:\n  - A\n",
      "{A: {parents: []}, B: {parents: [A], aliases: [x, y]}}",
      "'1':\n  parents: []\n\"2\":\n  parents: ['1']\n3:\n  parents: [\"2\"]\n",
      "A:\n  parents: []\nA:\n  parents: []\n",
      // Not of the plain form: anchors and aliases, tags, nulls, scalars and invalid fields
      "A: &anchor\n  parents: []\nB: *anchor\n",
      "A:\n  parents: [&p X]\nB:\n  parents: [*p]\n",
      "!!str A:\n  parents: []\n",
      "A:\n  parents: [!!str 1]\n",
      "A:\n  parents:\n",
      "A:\n  parents: null\n",
      "A:\n  parents: [~]\n",
      "~:\n  parents: []\n",
      "null:\n  parents: []\n",
      "A: B\n",
      "A:\n  parents: B\n",
      "A:\n  parents: [[B]]\n",
      "A:\n  parents: []\n  parents: [B]\n",
      "A:\n  parent: []\n",
      "- A\n- B\n",
      "A\n",
      "A:\n  parents: [B]\n---\nC:\n  parents: []\n",
   };
   for (const auto& definition : definitions) {
      const auto [streamed, node_based] = parsedBothWays(definition);
      EXPECT_EQ(streamed, node_based) << definition;
   }
}
