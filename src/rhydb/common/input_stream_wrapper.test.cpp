#include "rhydb/common/input_stream_wrapper.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <tuple>

#include <gtest/gtest.h>

#include "rhydb/append/ndjson_line_reader.h"
#include "rhydb/preprocessing/preprocessing_exception.h"

using rhydb::InputStreamWrapper;

namespace {

const std::filesystem::path ZST_FILE{"testBaseData/ndjsonFiles/twolines.ndjson.zst"};

std::string readAll(const InputStreamWrapper& wrapper) {
   auto& stream = wrapper.getInputStream();
   return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::string readFileBytes(const std::filesystem::path& file) {
   std::ifstream stream{file, std::ios::binary};
   return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void writeFileBytes(const std::filesystem::path& file, const std::string& bytes) {
   std::ofstream{file, std::ios::binary} << bytes;
}

size_t countReadNdjsonLines(const InputStreamWrapper& wrapper) {
   rhydb::append::NdjsonLineReader reader{wrapper.getInputStream()};
   size_t count = 0;
   for (auto it = reader.begin(); it != rhydb::append::NdjsonLineReader::end(); ++it) {
      ++count;
   }
   return count;
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

TEST_F(InputStreamWrapperTest, readingCorruptedZstFileThrows) {
   auto bytes = readFileBytes(ZST_FILE);
   ASSERT_GT(bytes.size(), 100);
   const size_t middle = bytes.size() / 2;
   for (size_t i = middle; i < middle + 32; ++i) {
      bytes[i] = static_cast<char>(~static_cast<unsigned char>(bytes[i]));
   }
   const auto file = directory / "corrupted.ndjson.zst";
   writeFileBytes(file, bytes);

   EXPECT_ANY_THROW(countReadNdjsonLines(InputStreamWrapper::openExactFile(file)));
}
