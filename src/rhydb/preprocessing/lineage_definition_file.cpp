#include "rhydb/preprocessing/lineage_definition_file.h"

#include <fstream>
#include <istream>
#include <iterator>
#include <optional>
#include <streambuf>
#include <utility>

#include <fmt/format.h>
#include <yaml-cpp/eventhandler.h>
#include <yaml-cpp/yaml.h>

#include "rhydb/preprocessing/preprocessing_exception.h"

namespace YAML {
using rhydb::common::LineageName;
using rhydb::preprocessing::LineageDefinition;
using rhydb::preprocessing::LineageDefinitionFile;

template <>
struct convert<LineageName> {
   static bool decode(const Node& node, LineageName& lineage_name) {
      try {
         lineage_name.string = node.as<std::string>();
      } catch (const YAML::BadConversion& e) {
         throw rhydb::preprocessing::PreprocessingException(
            fmt::format(
               "Could not parse Lineage definition name '{}', as it is not a string.",
               YAML::Dump(node)
            )
         );
      }
      return true;
   }
   static YAML::Node encode(const LineageName& lineage_name) {
      Node node;
      node = lineage_name.string;
      return node;
   }
};

namespace {

LineageDefinition entryToLineageDefinition(const YAML::detail::iterator_value& entry) {
   auto lineage_name = entry.first.as<LineageName>();
   if (entry.second.IsNull()) {
      return {
         .lineage_name = lineage_name,
         .aliases = std::vector<LineageName>{},
         .parents = std::vector<LineageName>{},
      };
   }
   if (!entry.second.IsMap()) {
      throw rhydb::preprocessing::PreprocessingException(
         fmt::format(
            "The lineage '{}' is not defined as a valid YAML Map in its definition: {}",
            lineage_name,
            YAML::Dump(entry.second)
         )
      );
   }
   if (std::ranges::any_of(entry.second, [](const auto& element) {
          const auto field_name = element.first.template as<std::string>();
          return field_name != "parents" && field_name != "aliases";
       })) {
      throw rhydb::preprocessing::PreprocessingException(
         fmt::format(
            "The definition of lineage '{}' may only contain the fields 'parents' and 'aliases', "
            "it "
            "also contains invalid fields:\n{}",
            lineage_name,
            YAML::Dump(entry.second)
         )
      );
   }
   std::vector<LineageName> parents;
   if (entry.second["parents"]) {
      if (!entry.second["parents"].IsSequence()) {
         throw rhydb::preprocessing::PreprocessingException(
            fmt::format(
               "The parents of lineage '{}' are not defined as a YAML Sequence", lineage_name
            )
         );
      }
      parents = entry.second["parents"].as<std::vector<LineageName>>();
   }
   std::vector<LineageName> aliases;
   if (entry.second["aliases"]) {
      if (!entry.second["aliases"].IsSequence()) {
         throw rhydb::preprocessing::PreprocessingException(
            fmt::format(
               "The aliases of lineage '{}' are not defined as a YAML Sequence", lineage_name
            )
         );
      }
      aliases = entry.second["aliases"].as<std::vector<LineageName>>();
   }
   return {.lineage_name = lineage_name, .aliases = aliases, .parents = parents};
}
}  // namespace

template <>
struct convert<LineageDefinitionFile> {
   static bool decode(const Node& node, LineageDefinitionFile& lineage_definition_file) {
      std::vector<LineageDefinition> lineage_definitions;
      for (const auto& entry : node) {
         lineage_definitions.emplace_back(entryToLineageDefinition(entry));
      }
      lineage_definition_file = LineageDefinitionFile{.lineages = std::move(lineage_definitions)};
      return true;
   }
};
}  // namespace YAML

namespace rhydb::preprocessing {

namespace {

/// Thrown by `StreamingLineageDefinitionParser` for a document it leaves to the node-based parser
class NotStreamable : public std::exception {};

/// Reads a lineage definition from the events of the YAML parser, without building the YAML node
/// tree, which for a definition with millions of lineages takes many times the memory and time of
/// the definitions themselves. It only reads definitions of the plain form: a map from lineage
/// names to either nothing or a map with the sequences `parents` and `aliases` of strings. It
/// throws `NotStreamable` for everything else, including anchors, aliases, tags, nulls and
/// invalid definitions, which the node-based parser then reads (or rejects with a precise error).
class StreamingLineageDefinitionParser : public YAML::EventHandler {
  public:
   std::vector<LineageDefinition> lineages;

   void OnDocumentStart(const YAML::Mark& /*mark*/) override {}

   void OnDocumentEnd() override {
      if (state != State::DONE && state != State::DOCUMENT) {
         throw NotStreamable{};
      }
   }

   void OnNull(const YAML::Mark& /*mark*/, YAML::anchor_t anchor) override {
      expectNoAnchor(anchor);
      if (state == State::DOCUMENT) {
         state = State::DONE;
      } else if (state == State::LINEAGE_VALUE) {
         finishLineage();
      } else {
         throw NotStreamable{};
      }
   }

   void OnAlias(const YAML::Mark& /*mark*/, YAML::anchor_t /*anchor*/) override {
      throw NotStreamable{};
   }

   void OnAnchor(const YAML::Mark& /*mark*/, const std::string& /*anchor_name*/) override {
      throw NotStreamable{};
   }

   void OnScalar(
      const YAML::Mark& /*mark*/,
      const std::string& tag,
      YAML::anchor_t anchor,
      const std::string& value
   ) override {
      expectNoAnchor(anchor);
      // The tags of untagged plain and quoted scalars
      if (tag != "?" && tag != "!") {
         throw NotStreamable{};
      }
      switch (state) {
         case State::LINEAGE_KEY:
            current = LineageDefinition{.lineage_name = {value}, .aliases = {}, .parents = {}};
            state = State::LINEAGE_VALUE;
            return;
         case State::FIELD_KEY:
            if (value == "parents" && !has_parents) {
               has_parents = true;
               current_field = &current.parents;
            } else if (value == "aliases" && !has_aliases) {
               has_aliases = true;
               current_field = &current.aliases;
            } else {
               throw NotStreamable{};
            }
            state = State::FIELD_VALUE;
            return;
         case State::FIELD_SEQUENCE:
            current_field->push_back({value});
            return;
         default:
            throw NotStreamable{};
      }
   }

   void OnSequenceStart(
      const YAML::Mark& /*mark*/,
      const std::string& tag,
      YAML::anchor_t anchor,
      YAML::EmitterStyle::value /*style*/
   ) override {
      expectNoAnchor(anchor);
      if (state != State::FIELD_VALUE || (tag != "?" && tag != "!")) {
         throw NotStreamable{};
      }
      state = State::FIELD_SEQUENCE;
   }

   void OnSequenceEnd() override {
      if (state != State::FIELD_SEQUENCE) {
         throw NotStreamable{};
      }
      state = State::FIELD_KEY;
   }

   void OnMapStart(
      const YAML::Mark& /*mark*/,
      const std::string& tag,
      YAML::anchor_t anchor,
      YAML::EmitterStyle::value /*style*/
   ) override {
      expectNoAnchor(anchor);
      if (tag != "?" && tag != "!") {
         throw NotStreamable{};
      }
      if (state == State::DOCUMENT) {
         state = State::LINEAGE_KEY;
      } else if (state == State::LINEAGE_VALUE) {
         has_parents = false;
         has_aliases = false;
         state = State::FIELD_KEY;
      } else {
         throw NotStreamable{};
      }
   }

   void OnMapEnd() override {
      if (state == State::LINEAGE_KEY) {
         state = State::DONE;
      } else if (state == State::FIELD_KEY) {
         finishLineage();
      } else {
         throw NotStreamable{};
      }
   }

  private:
   enum class State : uint8_t {
      /// Before the top-level map
      DOCUMENT,
      LINEAGE_KEY,
      LINEAGE_VALUE,
      FIELD_KEY,
      FIELD_VALUE,
      FIELD_SEQUENCE,
      DONE,
   };

   static void expectNoAnchor(YAML::anchor_t anchor) {
      if (anchor != YAML::NullAnchor) {
         throw NotStreamable{};
      }
   }

   void finishLineage() {
      lineages.push_back(std::move(current));
      state = State::LINEAGE_KEY;
   }

   State state = State::DOCUMENT;
   LineageDefinition current;
   std::vector<LineageName>* current_field = nullptr;
   bool has_parents = false;
   bool has_aliases = false;
};

/// A read-only stream buffer over memory, to parse a string without copying it into a stream
class MemoryBuffer : public std::streambuf {
  public:
   explicit MemoryBuffer(const std::string& contents) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): the buffer is only read
      char* begin = const_cast<char*>(contents.data());
      setg(begin, begin, begin + contents.size());
   }
};

/// The lineages of a definition of the plain form, or std::nullopt if the definition is of
/// another form, or invalid
std::optional<std::vector<LineageDefinition>> parseStreaming(const std::string& yaml_string) {
   MemoryBuffer buffer{yaml_string};
   std::istream stream{&buffer};
   StreamingLineageDefinitionParser parser;
   try {
      YAML::Parser yaml_parser{stream};
      yaml_parser.HandleNextDocument(parser);
   } catch (const NotStreamable&) {
      return std::nullopt;
   } catch (const YAML::Exception&) {
      return std::nullopt;
   }
   return std::move(parser.lineages);
}

}  // namespace

LineageDefinitionFile LineageDefinitionFile::fromYAMLFile(const std::filesystem::path& yaml_path) {
   std::ifstream file(yaml_path, std::ios::in | std::ios::binary);
   if (!file) {
      throw rhydb::preprocessing::PreprocessingException(
         fmt::format("Could not open the YAML file: '{}'", yaml_path.string())
      );
   }

   std::string contents{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
   if (file.bad()) {
      throw rhydb::preprocessing::PreprocessingException(
         fmt::format("Error when reading the YAML file: '{}'", yaml_path.string())
      );
   }
   try {
      return fromYAMLString(std::move(contents));
   } catch (const YAML::ParserException& parser_exception) {
      throw rhydb::preprocessing::PreprocessingException(
         fmt::format("The YAML file '{}' does not contain valid YAML.", yaml_path.string())
      );
   }
}

// If the string does not contain valid YAML, it will throw a YAML::ParserException.
// If the YAML is not of the expected structure, it will throw a different Error about what exactly
// was not of the expected form.
LineageDefinitionFile LineageDefinitionFile::fromYAMLString(std::string yaml_string) {
   LineageDefinitionFile file;
   if (auto lineages = parseStreaming(yaml_string)) {
      file.lineages = std::move(lineages.value());
   } else {
      const YAML::Node yaml = YAML::Load(yaml_string);
      YAML::convert<LineageDefinitionFile>::decode(yaml, file);
   }
   file.raw_file = std::move(yaml_string);
   return file;
}

// If the string does not contain valid YAML, it will throw a YAML::ParserException.
// If the YAML is not of the expected structure, it will throw a different Error about what exactly
// was not of the expected form.
LineageDefinitionFile LineageDefinitionFile::fromYAML(const YAML::Node& yaml_node) {
   LineageDefinitionFile file;
   YAML::convert<LineageDefinitionFile>::decode(yaml_node, file);
   file.raw_file = YAML::Dump(yaml_node);
   return file;
}

}  // namespace rhydb::preprocessing
