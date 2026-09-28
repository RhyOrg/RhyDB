#include "config/source/environment_variables.h"

#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

using rhydb::config::ConfigKeyPath;
using rhydb::config::EnvironmentVariables;

TEST(EnvironmentVariables, correctPrefixedUppercase) {
   ASSERT_EQ(
      EnvironmentVariables::configKeyPathToString(ConfigKeyPath::tryFrom({{"a"}}).value()),
      "RHYDB_A"
   );
   ASSERT_EQ(
      EnvironmentVariables::configKeyPathToString(ConfigKeyPath::tryFrom({{"abc"}}).value()),
      "RHYDB_ABC"
   );
   ASSERT_EQ(
      EnvironmentVariables::configKeyPathToString(
         ConfigKeyPath::tryFrom({{"some", "snake", "case"}}).value()
      ),
      "RHYDB_SOME_SNAKE_CASE"
   );
   ASSERT_EQ(
      EnvironmentVariables::configKeyPathToString(
         ConfigKeyPath::tryFrom({{"some"}, {"subsectioned", "sequence"}}).value()
      ),
      "RHYDB_SOME_SUBSECTIONED_SEQUENCE"
   );
   ASSERT_EQ(
      EnvironmentVariables::configKeyPathToString(
         ConfigKeyPath::tryFrom({{"some"}, {"more"}, {"sections"}}).value()
      ),
      "RHYDB_SOME_MORE_SECTIONS"
   );
}

TEST(EnvironmentVariables, successfullyIgnoreTheAllowList) {
   const std::vector<std::string> allow_list = {"RHYDB_DEBUG"};
   const char* env_var = "RHYDB_DEBUG=1";
   const std::vector<const char*> var_vector = {env_var, nullptr};
   auto env_vars = EnvironmentVariables::newWithAllowListAndEnv(allow_list, var_vector.data());
   ASSERT_NO_THROW((void)env_vars.verify({}));
}

TEST(EnvironmentVariables, errorsIfRhydbDebugIsProvidedButNotAllowed) {
   const std::vector<std::string> allow_list;
   const char* env_var = "RHYDB_DEBUG=1";
   const std::vector<const char*> var_vector = {env_var, nullptr};
   auto env_vars = EnvironmentVariables::newWithAllowListAndEnv(allow_list, var_vector.data());
   EXPECT_THAT(
      [&]() { (void)env_vars.verify({.program_name = "some_binary_name"}); },
      ThrowsMessage<rhydb::config::ConfigException>(::testing::HasSubstr(
         "in environment variables: unknown variable RHYDB_DEBUG for 'some_binary_name'"
      ))
   );
}

TEST(EnvironmentVariables, doesNotErrorWhenThePrefixIsNotRHYDB_) {
   const std::vector<std::string> allow_list;
   const char* env_var = "RHYDBDEBUG=1";
   const std::vector<const char*> var_vector = {env_var, nullptr};
   auto env_vars = EnvironmentVariables::newWithAllowListAndEnv(allow_list, var_vector.data());
   ASSERT_NO_THROW((void)env_vars.verify({}));
}

TEST(EnvironmentVariables, errorsOnWrongType) {
   const std::vector<std::string> allow_list{"RHYDB_FOO"};
   const char* env_var = "RHYDB_FOO=bar";
   const std::vector<const char*> var_vector = {env_var, nullptr};
   auto env_vars = EnvironmentVariables::newWithAllowListAndEnv(allow_list, var_vector.data());
   EXPECT_THAT(
      [&]() {
         (void)env_vars.verify(rhydb::config::ConfigSpecification{
            .program_name = "test",
            .attribute_specifications =
               {rhydb::config::ConfigAttributeSpecification::createWithoutDefault(
                  ConfigKeyPath::tryFrom({{"foo"}}).value(),
                  rhydb::config::ConfigValueType::INT32,
                  "some help text"
               ),},
         });
      },
      ThrowsMessage<rhydb::config::ConfigException>(::testing::HasSubstr("cannot parse 'bar' as i32"
      ))
   );
}

TEST(EnvironmentVariables, parsesVariables) {
   const std::vector<std::string> allow_list{"RHYDB_FOO", "RHYDB_FOO_INT"};
   const char* env_var1 = "RHYDB_FOO=bar";
   const char* env_var2 = "RHYDB_FOO_INT=1";
   const std::vector<const char*> var_vector = {env_var1, env_var2, nullptr};
   auto env_vars = EnvironmentVariables::newWithAllowListAndEnv(allow_list, var_vector.data());
   ASSERT_NO_THROW((void)env_vars.verify(rhydb::config::ConfigSpecification{
      .program_name = "test",
      .attribute_specifications =
         {rhydb::config::ConfigAttributeSpecification::createWithoutDefault(
             ConfigKeyPath::tryFrom({{"foo"}}).value(),
             rhydb::config::ConfigValueType::STRING,
             "some help text"
          ),
          rhydb::config::ConfigAttributeSpecification::createWithoutDefault(
             ConfigKeyPath::tryFrom({{"foo"}, {"int"}}).value(),
             rhydb::config::ConfigValueType::INT32,
             "some help text"
          ),},
   }));
}

TEST(EnvironmentVariables, parsesVariablesWithDoubleEquals) {
   const std::vector<std::string> allow_list{"RHYDB_FOO"};
   const char* env_var1 = "RHYDB_FOO=bar=baz";
   const std::vector<const char*> var_vector = {env_var1, nullptr};
   auto env_vars = EnvironmentVariables::newWithAllowListAndEnv(allow_list, var_vector.data());
   ASSERT_EQ(
      env_vars
         .verify(rhydb::config::ConfigSpecification{
            .program_name = "test",
            .attribute_specifications =
               {rhydb::config::ConfigAttributeSpecification::createWithoutDefault(
                  ConfigKeyPath::tryFrom({{"foo"}}).value(),
                  rhydb::config::ConfigValueType::STRING,
                  "some help text"
               ),},
         })
         .getString(ConfigKeyPath::tryFrom({{"foo"}}).value()),
      "bar=baz"
   );
}
