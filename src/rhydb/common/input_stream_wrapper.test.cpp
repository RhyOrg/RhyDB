#include "rhydb/common/input_stream_wrapper.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <tuple>

#include <gtest/gtest.h>

#include "rhydb/preprocessing/preprocessing_exception.h"

using rhydb::InputStreamWrapper;

namespace {

const std::filesystem::path ZST_FILE{"testBaseData/ndjsonFiles/twolines.ndjson.zst"};

std::string readAll(const InputStreamWrapper& wrapper) {
   auto& stream = wrapper.getInputStream();
   return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

size_t countLines(const std::string& content) {
   return static_cast<size_t>(std::count(content.begin(), content.end(), '\n'));
}

class InputStreamWrapperTest : public ::testing::Test {
  protected:
   std::filesystem::path directory;

   void SetUp() override {
      directory = std::filesystem::temp_directory_path() /
                  ("rhydb_input_stream_wrapper_test_" +
                   std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
      std::filesystem::remove_all(directory);
      std::filesystem::create_directories(directory);
   }

   void TearDown() override { std::filesystem::remove_all(directory); }
};

}  // namespace

TEST_F(InputStreamWrapperTest, openExactFileDecompressesZstFile) {
   const auto content = readAll(InputStreamWrapper::openExactFile(ZST_FILE));
   EXPECT_EQ(countLines(content), 2);
   EXPECT_TRUE(content.starts_with('{'));
}

TEST_F(InputStreamWrapperTest, openExactFileReadsUncompressedFileRaw) {
   const auto file = directory / "data.ndjson";
   std::ofstream{file} << "{\"key\": 1}\n";

   EXPECT_EQ(readAll(InputStreamWrapper::openExactFile(file)), "{\"key\": 1}\n");
}

TEST_F(InputStreamWrapperTest, openExactFileDoesNotLookUpFilesWithAdditionalEndings) {
   const auto file = directory / "data.ndjson";
   std::ofstream{file} << "{\"key\": 1}\n";
   std::filesystem::copy_file(ZST_FILE, directory / "data.ndjson.zst");

   EXPECT_EQ(readAll(InputStreamWrapper::openExactFile(file)), "{\"key\": 1}\n");
   // In contrast, openFileWithEndingLookup prefers the file with the .zst ending
   EXPECT_EQ(countLines(readAll(InputStreamWrapper::openFileWithEndingLookup(file))), 2);
}

TEST_F(InputStreamWrapperTest, openExactFileThrowsOnMissingFile) {
   std::filesystem::copy_file(ZST_FILE, directory / "data.ndjson.zst");

   EXPECT_THROW(
      std::ignore = InputStreamWrapper::openExactFile(directory / "data.ndjson"),
      rhydb::preprocessing::PreprocessingException
   );
}
