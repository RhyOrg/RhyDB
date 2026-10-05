#include "rhydb/common/bidirectional_string_map.h"

#include <gtest/gtest.h>

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>

namespace rhydb::common {

TEST(BidirectionalMap, correctStdStringDict) {
   BidirectionalStringMap under_test;
   EXPECT_EQ(under_test.getId("Not in dict"), std::nullopt);

   EXPECT_EQ(under_test.getOrCreateId("Now in dict"), 0);
   EXPECT_EQ(under_test.getOrCreateId("Now in dict"), 0);
   EXPECT_EQ(under_test.getOrCreateId("Second in dict"), 1);

   EXPECT_EQ(under_test.getId("Now in dict"), 0);
   EXPECT_EQ(under_test.getId("Still not in dict"), std::nullopt);
   EXPECT_EQ(under_test.getId("Second in dict"), 1);

   EXPECT_EQ(under_test.getValue(0), "Now in dict");
   EXPECT_EQ(under_test.getValue(1), "Second in dict");
}

TEST(BidirectionalMap, correctStdStringDictWithExplicitInitialization) {
   const std::string not_in_dict{"Not in dict"};
   const std::string now_in_dict{"Now in dict"};
   const std::string second_in_dict{"Second in dict"};
   const std::string still_not_in_dict{"Still not in dict"};

   BidirectionalStringMap under_test;
   EXPECT_EQ(under_test.getId(not_in_dict), std::nullopt);

   EXPECT_EQ(under_test.getOrCreateId(now_in_dict), 0);
   EXPECT_EQ(under_test.getOrCreateId(now_in_dict), 0);
   EXPECT_EQ(under_test.getOrCreateId(second_in_dict), 1);

   EXPECT_EQ(under_test.getId(now_in_dict), 0);
   EXPECT_EQ(under_test.getId(still_not_in_dict), std::nullopt);
   EXPECT_EQ(under_test.getId(second_in_dict), 1);

   EXPECT_EQ(under_test.getValue(0), std::string{"Now in dict"});
   EXPECT_EQ(under_test.getValue(1), std::string{"Second in dict"});
}

TEST(BidirectionalMap, correctRoundtripOfNonUtf8Data) {
   BidirectionalStringMap original_map;
   std::string example_string = "ü:";
   std::string non_utf8_string = example_string.substr(1);
   original_map.getOrCreateId(non_utf8_string);

   std::ostringstream oss;
   boost::archive::binary_oarchive oarchive(oss);
   oarchive << original_map;

   std::istringstream iss(oss.str());
   boost::archive::binary_iarchive iarchive(iss);
   BidirectionalStringMap under_test;
   iarchive >> under_test;

   EXPECT_TRUE(under_test.getId(non_utf8_string).has_value());
}

}  // namespace rhydb::common