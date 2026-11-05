#include "rhydb/common/date32.h"

#include <gtest/gtest.h>

TEST(Date32, correctlyParsesDate) {
   EXPECT_EQ(rhydb::common::stringToDate32("2020-01-01").value(), 18262);
   EXPECT_EQ(rhydb::common::stringToDate32("1970-01-01").value(), 0);
   EXPECT_EQ(rhydb::common::stringToDate32("2010-12-03").value(), 14946);
   EXPECT_EQ(rhydb::common::stringToDate32("1969-12-31").value(), -1);
}

TEST(Date32, parsesPreEpochDates) {
   EXPECT_TRUE(rhydb::common::stringToDate32("1900-01-01").has_value());
   EXPECT_LT(rhydb::common::stringToDate32("1900-01-01").value(), 0);
   EXPECT_EQ(rhydb::common::stringToDate32("1969-12-31").value(), -1);
   EXPECT_EQ(rhydb::common::stringToDate32("1969-12-30").value(), -2);
}

TEST(Date32, parsesLeapYearDates) {
   EXPECT_TRUE(rhydb::common::stringToDate32("2024-02-29").has_value());
   EXPECT_TRUE(rhydb::common::stringToDate32("2000-02-29").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-02-29").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("1900-02-29").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2100-02-29").has_value());
}

TEST(Date32, parsesBoundaryDaysPerMonth) {
   EXPECT_TRUE(rhydb::common::stringToDate32("2023-01-31").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-01-32").has_value());
   EXPECT_TRUE(rhydb::common::stringToDate32("2023-03-31").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-03-32").has_value());
   EXPECT_TRUE(rhydb::common::stringToDate32("2023-04-30").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-04-31").has_value());
   EXPECT_TRUE(rhydb::common::stringToDate32("2023-02-28").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-02-29").has_value());
}

TEST(Date32, rejectsWrongFormat) {
   EXPECT_FALSE(rhydb::common::stringToDate32("").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("?").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("----").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-1-01").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("12-12-12").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("-1-").has_value());
}

TEST(Date32, rejectsWrongSeparators) {
   EXPECT_FALSE(rhydb::common::stringToDate32("2023/01/15").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023.01.15").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023 01 15").has_value());
}

TEST(Date32, rejectsWrongFieldWidths) {
   EXPECT_FALSE(rhydb::common::stringToDate32("23-01-15").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-1-15").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-01-1").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("20230-01-15").has_value());
}

TEST(Date32, rejectsNonNumericFields) {
   EXPECT_FALSE(rhydb::common::stringToDate32("abcd-01-15").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-ab-15").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-01-ab").has_value());
}

TEST(Date32, rejectsSignedFields) {
   EXPECT_FALSE(rhydb::common::stringToDate32("-999-01-01").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("+999-01-01").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023--1-01").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-01--1").has_value());
}

TEST(Date32, rejectsTrailingAndLeadingContent) {
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-01-15 ").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32(" 2023-01-15").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-01-15T00:00:00").has_value());
}

TEST(Date32, rejectsInvalidCalendarDates) {
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-02-30").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-13-01").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-00-01").has_value());
   EXPECT_FALSE(rhydb::common::stringToDate32("2023-01-00").has_value());
}

TEST(Date32, errorMessagesAreDescriptive) {
   auto format_error = rhydb::common::stringToDate32("not-a-date");
   ASSERT_FALSE(format_error.has_value());
   EXPECT_NE(format_error.error().find("not-a-date"), std::string::npos);

   auto calendar_error = rhydb::common::stringToDate32("2023-02-30");
   ASSERT_FALSE(calendar_error.has_value());
   EXPECT_NE(calendar_error.error().find("2023-02-30"), std::string::npos);
}

TEST(Date32, correctlyRoundTrips) {
   const std::vector<std::string> dates = {
      "2020-01-01",
      "2010-12-03",
      "1970-01-01",
      "1969-12-31",
      "2000-02-29",
      "2024-12-31",
      "1900-01-01",
      "2099-06-15",
   };
   for (const auto& date_string : dates) {
      auto parsed = rhydb::common::stringToDate32(date_string);
      ASSERT_TRUE(parsed.has_value()) << "Failed to parse: " << date_string;
      EXPECT_EQ(rhydb::common::date32ToString(parsed.value()), date_string);
   }
}

TEST(Date32, dateToStringFormatsWithLeadingZeros) {
   EXPECT_EQ(rhydb::common::date32ToString(0), "1970-01-01");
   EXPECT_EQ(rhydb::common::date32ToString(-1), "1969-12-31");
}

TEST(Date32, epochIsZero) {
   EXPECT_EQ(rhydb::common::stringToDate32("1970-01-01").value(), 0);
}

TEST(Date32, daysAreMonotonicallyIncreasing) {
   auto jan1 = rhydb::common::stringToDate32("2023-01-01").value();
   auto jan2 = rhydb::common::stringToDate32("2023-01-02").value();
   auto feb1 = rhydb::common::stringToDate32("2023-02-01").value();
   auto dec31 = rhydb::common::stringToDate32("2023-12-31").value();
   EXPECT_EQ(jan2 - jan1, 1);
   EXPECT_LT(jan1, feb1);
   EXPECT_LT(feb1, dec31);
}

TEST(Date32, yearBoundaryIsOneDay) {
   auto dec31 = rhydb::common::stringToDate32("2022-12-31").value();
   auto jan1 = rhydb::common::stringToDate32("2023-01-01").value();
   EXPECT_EQ(jan1 - dec31, 1);
}
