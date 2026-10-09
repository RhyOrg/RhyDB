#include "rhydb/common/rhydb_directory.h"

#include <gtest/gtest.h>

using rhydb::RhyDBDirectory;

TEST(DatabaseDirectoryWatcher, validBackwardsCompatible) {
   auto under_test =
      rhydb::RhyDBDataSource::checkValidDataSource("testBaseData/dataDirectories/1234");
   ASSERT_FALSE(under_test.data_version.isCompatibleVersion());
   ASSERT_EQ(
      under_test.data_version.getTimestamp(), rhydb::DataVersion::Timestamp::fromString("1234")
   );
}

TEST(DatabaseDirectoryWatcher, validNewFormatOldVersion) {
   auto under_test =
      rhydb::RhyDBDataSource::checkValidDataSource("testBaseData/dataDirectories/1235");
   ASSERT_FALSE(under_test.data_version.isCompatibleVersion());
   ASSERT_EQ(
      under_test.data_version.getTimestamp(), rhydb::DataVersion::Timestamp::fromString("1235")
   );
}

TEST(DatabaseDirectoryWatcher, validNewFormatCurrentVersion) {
   auto most_recent = rhydb::DataVersion::mineDataVersion();

   const std::filesystem::path test_data_directories_dir{"testBaseData/dataDirectories"};
   auto most_recent_save_dir = test_data_directories_dir / most_recent.getTimestamp().value;
   std::filesystem::create_directory(most_recent_save_dir);

   most_recent.saveToFile(most_recent_save_dir / "data_version.silo");

   auto under_test = rhydb::RhyDBDataSource::checkValidDataSource(most_recent_save_dir);
   ASSERT_TRUE(under_test.data_version.isCompatibleVersion());
   ASSERT_EQ(under_test.data_version.getTimestamp(), most_recent.getTimestamp());

   std::filesystem::remove_all(most_recent_save_dir);
}

TEST(DatabaseDirectoryWatcher, validNewFormatIncompatible) {
   auto under_test =
      rhydb::RhyDBDataSource::checkValidDataSource("testBaseData/dataDirectories/9999999999991234");
   ASSERT_FALSE(under_test.data_version.isCompatibleVersion());
   ASSERT_EQ(
      under_test.data_version.getTimestamp(),
      rhydb::DataVersion::Timestamp::fromString("9999999999991234")
   );
}

TEST(DatabaseDirectoryWatcher, invalidFormat) {
   ASSERT_ANY_THROW(
      rhydb::RhyDBDataSource::checkValidDataSource("testBaseData/dataDirectories/3123")
   );
}

TEST(DatabaseDirectoryWatcher, invalidUInt32) {
   ASSERT_ANY_THROW(
      rhydb::RhyDBDataSource::checkValidDataSource("testBaseData/dataDirectories/3124")
   );
}

TEST(DatabaseDirectoryWatcher, invalidYAML) {
   ASSERT_ANY_THROW(
      rhydb::RhyDBDataSource::checkValidDataSource("testBaseData/dataDirectories/3125")
   );
}

TEST(DatabaseDirectoryWatcher, getsMostRecentCompatible) {
   auto most_recent = rhydb::DataVersion::mineDataVersion();

   const std::filesystem::path test_data_directories_dir{"testBaseData/dataDirectories"};
   auto most_recent_save_dir = test_data_directories_dir / most_recent.getTimestamp().value;
   std::filesystem::create_directory(most_recent_save_dir);

   most_recent.saveToFile(most_recent_save_dir / "data_version.silo");

   auto under_test = RhyDBDirectory{"testBaseData/dataDirectories"};
   auto under_test_most_recent = under_test.getMostRecentDataDirectory();
   ASSERT_TRUE(under_test_most_recent.has_value());
   ASSERT_EQ(under_test_most_recent.value().path, most_recent_save_dir);
   ASSERT_TRUE(under_test_most_recent.value().data_version.isCompatibleVersion());
   ASSERT_EQ(
      under_test_most_recent.value().data_version.getTimestamp(), most_recent.getTimestamp()
   );

   std::filesystem::remove_all(most_recent_save_dir);
}
