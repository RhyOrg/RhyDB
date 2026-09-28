#include "config/source/command_line_arguments.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "config/config_exception.h"
#include "config/source/yaml_file.h"

using rhydb::config::CommandLineArguments;
using rhydb::config::ConfigException;
using rhydb::config::ConfigKeyPath;

TEST(CommandLineArguments, correctUnixOptionString) {
   ASSERT_EQ(
      CommandLineArguments::configKeyPathToString(ConfigKeyPath::tryFrom({{"a"}}).value()), "--a"
   );
   ASSERT_EQ(
      CommandLineArguments::configKeyPathToString(ConfigKeyPath::tryFrom({{"abc"}}).value()),
      "--abc"
   );
   ASSERT_EQ(
      CommandLineArguments::configKeyPathToString(ConfigKeyPath::tryFrom({{"ab2c"}}).value()),
      "--ab2c"
   );
   ASSERT_EQ(
      CommandLineArguments::configKeyPathToString(
         ConfigKeyPath::tryFrom({{"some", "camel", "case"}}).value()
      ),
      "--some-camel-case"
   );
   ASSERT_EQ(
      CommandLineArguments::configKeyPathToString(
         ConfigKeyPath::tryFrom({{"some"}, {"subsectioned", "sequence"}}).value()
      ),
      "--some-subsectioned-sequence"
   );
   ASSERT_EQ(
      CommandLineArguments::configKeyPathToString(
         ConfigKeyPath::tryFrom({{"some", "more", "sections"}}).value()
      ),
      "--some-more-sections"
   );
}

TEST(CommandLineArguments, shouldFailAppropriatelyOnInvalidOptions) {
   const std::vector<std::string> invalid_options{
      "no-starting-minus",
      "---too-many-starting-minuses",
      "--NOT-LOWER-CASE",
      "--not&alphanumeric",
      "",
      "-",
      "--",
      "&",
      "\n",
      "--wrong_delimiter"
   };
   for (const auto& invalid_option : invalid_options) {
      EXPECT_THAT(
         [&invalid_option]() { CommandLineArguments::stringToConfigKeyPath(invalid_option); },
         ThrowsMessage<ConfigException>(::testing::HasSubstr(fmt::format(
            "the provided option '{}' is not a valid command line option", invalid_option
         )))
      );
   }
}

TEST(CommandLineArguments, stringToConfigKeyPath) {
   ASSERT_EQ(
      CommandLineArguments::stringToConfigKeyPath("--foo"),
      rhydb::config::AmbiguousConfigKeyPath::tryFrom({"foo"})
   );
   ASSERT_EQ(
      CommandLineArguments::stringToConfigKeyPath("--some-option"),
      rhydb::config::AmbiguousConfigKeyPath::tryFrom({"some", "option"})
   );
   ASSERT_EQ(
      CommandLineArguments::stringToConfigKeyPath("--some-longer-option"),
      rhydb::config::AmbiguousConfigKeyPath::tryFrom({"some", "longer", "option"})
   );
}

using rhydb::config::ConfigAttributeSpecification;
using rhydb::config::ConfigSpecification;
using rhydb::config::ConfigValue;
using rhydb::config::ConfigValueType;
using rhydb::config::YamlFile;

namespace {

auto foo_key = YamlFile::stringToConfigKeyPath("foo");
auto bar_key = YamlFile::stringToConfigKeyPath("bar");
ConfigSpecification specification{
   .program_name = "test",
   .attribute_specifications =
      {ConfigAttributeSpecification::createWithDefault(
          foo_key,
          ConfigValue::fromBool(false),
          "help"
       ),
       ConfigAttributeSpecification::createWithoutDefault(bar_key, ConfigValueType::INT32, "help"),},
};

}  // namespace

TEST(CommandLineArguments, verifyOneArgument) {
   std::vector<std::string> arguments{"--foo"};
   const CommandLineArguments under_test{{arguments.begin(), arguments.end()}};

   {
      auto verified = under_test.verify(specification);
      ASSERT_TRUE(verified.positional_arguments.empty());
      ASSERT_TRUE(verified.config_values.contains(foo_key));
      ASSERT_EQ(verified.config_values.at(foo_key).getValueType(), ConfigValueType::BOOL);
      ASSERT_EQ(get<bool>(verified.config_values.at(foo_key).value), true);
   }
}

TEST(CommandLineArguments, failsIfNotInSpecification) {
   std::vector<std::string> arguments{"--foo"};
   const CommandLineArguments under_test{{arguments.begin(), arguments.end()}};
   EXPECT_THAT(
      [&]() { (void)under_test.verify(rhydb::config::ConfigSpecification{}); },
      ThrowsMessage<rhydb::config::ConfigException>(
         ::testing::HasSubstr("in command line arguments: unknown option --foo")
      )
   );
}

TEST(CommandLineArguments, failsIfWrongType) {
   std::vector<std::string> arguments{"--bar", "true"};
   const CommandLineArguments under_test{{arguments.begin(), arguments.end()}};
   EXPECT_THAT(
      [&]() { (void)under_test.verify(specification); },
      ThrowsMessage<rhydb::config::ConfigException>(
         ::testing::HasSubstr("cannot parse 'true' as i32")
      )
   );
}

TEST(CommandLineArguments, testPositionalArguments) {
   std::vector<std::string> arguments{"positional_argument"};
   const CommandLineArguments under_test{{arguments.begin(), arguments.end()}};

   {
      auto verified = under_test.verify({});
      ASSERT_EQ(verified.positional_arguments.size(), 1);
      ASSERT_EQ(verified.positional_arguments.at(0), "positional_argument");
   }
}

TEST(CommandLineArguments, testPositionalArgumentsStartingWithMinus) {
   std::vector<std::string> arguments{"--", "-positional_argument_with_minus"};
   const CommandLineArguments under_test{{arguments.begin(), arguments.end()}};

   {
      auto verified = under_test.verify({});
      ASSERT_EQ(verified.positional_arguments.size(), 1);
      ASSERT_EQ(verified.positional_arguments.at(0), "-positional_argument_with_minus");
   }
}

TEST(CommandLineArguments, verboseFlagShortForm) {
   std::vector<std::string> arguments{"-v"};
   const auto verified = CommandLineArguments{{arguments.begin(), arguments.end()}}.verify({});
   ASSERT_EQ(verified.verbose_count, 1U);
   ASSERT_TRUE(verified.positional_arguments.empty());
}

TEST(CommandLineArguments, verboseFlagLongForm) {
   std::vector<std::string> arguments{"--verbose"};
   const auto verified = CommandLineArguments{{arguments.begin(), arguments.end()}}.verify({});
   ASSERT_EQ(verified.verbose_count, 1U);
}

TEST(CommandLineArguments, verboseFlagRepeated) {
   std::vector<std::string> arguments{"-v", "--verbose", "-v"};
   const auto verified = CommandLineArguments{{arguments.begin(), arguments.end()}}.verify({});
   ASSERT_EQ(verified.verbose_count, 3U);
}

TEST(CommandLineArguments, verboseFlagCluster) {
   std::vector<std::string> arguments{"-vvv"};
   const auto verified = CommandLineArguments{{arguments.begin(), arguments.end()}}.verify({});
   ASSERT_EQ(verified.verbose_count, 3U);
}

TEST(CommandLineArguments, verboseFlagDoesNotCountAsUnknownOption) {
   std::vector<std::string> arguments{"-v"};
   // Even with an empty specification (no known options), -v should not throw.
   ASSERT_NO_THROW(((void)CommandLineArguments{{arguments.begin(), arguments.end()}}.verify({})));
}
