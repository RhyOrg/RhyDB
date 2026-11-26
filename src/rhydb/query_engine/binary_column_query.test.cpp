#include <filesystem>
#include <sstream>
#include <string>
#include <tuple>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "rhydb/database.h"
#include "rhydb/query_engine/illegal_query_exception.h"
#include "rhydb/query_engine/planner.h"
#include "rhydb/schema/database_schema.h"
#include "rhydb/test/query_fixture.test.h"

using rhydb::query_engine::IllegalQueryException;
using rhydb::schema::TableName;
using ::testing::HasSubstr;
using ::testing::ThrowsMessage;

namespace {

nlohmann::json executeWrite(rhydb::Database& database, const std::string& statement) {
   return rhydb::test::writeResultToJson(
      database.executeWrite(statement, rhydb::config::QueryOptions{}, "test_request_id")
   );
}

nlohmann::json executeQuery(rhydb::Database& database, const std::string& query) {
   auto query_plan = rhydb::query_engine::Planner::planSaneqlQuery(
      query, database.tables, rhydb::config::QueryOptions{}, "test_request_id"
   );
   return rhydb::test::executeQueryToJsonArray(query_plan);
}

void expectQueryError(
   rhydb::Database& database,
   const std::string& query,
   const std::string& expected_message_part
) {
   EXPECT_THAT(
      [&]() { std::ignore = executeQuery(database, query); },
      ThrowsMessage<IllegalQueryException>(HasSubstr(expected_message_part))
   );
}

// Binary values are given and output base64 encoded: "AP8=" is the bytes 0x00 0xff (not valid
// UTF-8), "Zm9v" is "foo", "YmFy" is "bar" and "" the empty byte string.
rhydb::Database makeDatabaseWithBinaryTable() {
   rhydb::Database database;
   std::ignore = executeWrite(
      database, "createTable(blobs, {key := string, payload := binary}, primaryKey := key)"
   );
   std::stringstream data;
   data << R"({"key":"a","payload":"AP8="})" << "\n"
        << R"({"key":"b","payload":"Zm9v"})" << "\n"
        << R"({"key":"c","payload":null})" << "\n"
        << R"({"key":"d","payload":""})" << "\n";
   database.appendData(TableName{"blobs"}, data);
   return database;
}

const auto ALL_ROWS = nlohmann::json::parse(R"([
   {"key": "a", "payload": "AP8="},
   {"key": "b", "payload": "Zm9v"},
   {"key": "c", "payload": null},
   {"key": "d", "payload": ""}
])");

}  // namespace

TEST(BinaryColumnQuery, isIngestedAndOutputBase64Encoded) {
   auto database = makeDatabaseWithBinaryTable();
   EXPECT_EQ(executeQuery(database, "blobs.project({key, payload})"), ALL_ROWS);
}

TEST(BinaryColumnQuery, isReportedBySchema) {
   auto database = makeDatabaseWithBinaryTable();
   EXPECT_EQ(executeQuery(database, "blobs.schema()"), nlohmann::json::parse(R"([
         {"fieldName": "key", "type": "STRING"},
         {"fieldName": "payload", "type": "BINARY"}
      ])"));
}

TEST(BinaryColumnQuery, rejectsInvalidBase64Input) {
   auto database = makeDatabaseWithBinaryTable();
   std::stringstream data;
   data << R"({"key":"e","payload":"not base64!"})" << "\n";
   EXPECT_THAT(
      [&]() { database.appendData(TableName{"blobs"}, data); }, ::testing::Throws<std::exception>()
   );
}

TEST(BinaryColumnQuery, canBeFilteredOnNulls) {
   auto database = makeDatabaseWithBinaryTable();
   EXPECT_EQ(
      executeQuery(database, "blobs.filter(payload.isNull()).project({key})"),
      nlohmann::json::parse(R"([{"key": "c"}])")
   );
   EXPECT_EQ(
      executeQuery(database, "blobs.filter(payload.isNotNull()).project({key})"),
      nlohmann::json::parse(R"([{"key": "a"}, {"key": "b"}, {"key": "d"}])")
   );
}

TEST(BinaryColumnQuery, canBeComparedToBinaryLiterals) {
   auto database = makeDatabaseWithBinaryTable();
   EXPECT_EQ(
      executeQuery(database, "blobs.filter(payload = 'foo'::binary).project({key})"),
      nlohmann::json::parse(R"([{"key": "b"}])")
   );
   // Null rows match no comparison
   EXPECT_EQ(
      executeQuery(database, "blobs.filter(payload <> 'foo'::binary).project({key})"),
      nlohmann::json::parse(R"([{"key": "a"}, {"key": "d"}])")
   );
   // Byte-wise lexicographic: "" < 0x00ff < "foo"
   EXPECT_EQ(
      executeQuery(database, "blobs.filter('foo'::binary > payload).project({key})"),
      nlohmann::json::parse(R"([{"key": "a"}, {"key": "d"}])")
   );
}

TEST(BinaryColumnQuery, comparisonsAboveAMapAreEvaluatedByArrow) {
   auto database = makeDatabaseWithBinaryTable();
   EXPECT_EQ(
      executeQuery(
         database, "blobs.map({p := payload}).filter(p = 'foo'::binary).project({key, p})"
      ),
      nlohmann::json::parse(R"([{"key": "b", "p": "Zm9v"}])")
   );
   EXPECT_EQ(
      executeQuery(database, "blobs.map({p := payload}).filter(p < 'foo'::binary).project({key})"),
      nlohmann::json::parse(R"([{"key": "a"}, {"key": "d"}])")
   );
}

TEST(BinaryColumnQuery, castsFromAndToStrings) {
   auto database = makeDatabaseWithBinaryTable();
   EXPECT_EQ(
      executeQuery(
         database,
         "blobs.filter(key = 'b').map({s := payload::string, l := 'bar'::binary})"
         ".project({s, l})"
      ),
      nlohmann::json::parse(R"([{"s": "foo", "l": "YmFy"}])")
   );
   // 0x00ff is not valid UTF-8
   EXPECT_THAT(
      [&]() {
         std::ignore = executeQuery(
            database, "blobs.filter(key = 'a').map({s := payload::string}).project({s})"
         );
      },
      ::testing::Throws<std::exception>()
   );
   expectQueryError(
      database, "blobs.map({i := payload::int32}).project({i})", "cannot cast from BINARY to INT32"
   );
}

TEST(BinaryColumnQuery, rejectsComparisonsWithNonBinaryLiterals) {
   auto database = makeDatabaseWithBinaryTable();
   expectQueryError(
      database,
      "blobs.filter(payload = 'foo').project({key})",
      "The column 'payload' is not of type string"
   );
   expectQueryError(
      database,
      "blobs.filter(payload > 3).project({key})",
      "The column 'payload' is not of type int"
   );
   expectQueryError(
      database,
      "blobs.filter(key = 'foo'::binary).project({key})",
      "The column 'key' is not of type binary"
   );
}

TEST(BinaryColumnQuery, canBeUpdated) {
   auto database = makeDatabaseWithBinaryTable();
   std::ignore = executeWrite(
      database, "update(blobs, set := {payload := 'bar'::binary}, where := key = 'c')"
   );
   std::ignore =
      executeWrite(database, "update(blobs, set := {payload := null}, where := key = 'a')");
   EXPECT_EQ(executeQuery(database, "blobs.project({key, payload})"), nlohmann::json::parse(R"([
         {"key": "a", "payload": null},
         {"key": "b", "payload": "Zm9v"},
         {"key": "c", "payload": "YmFy"},
         {"key": "d", "payload": ""}
      ])"));
   EXPECT_THAT(
      [&]() { std::ignore = executeWrite(database, "update(blobs, set := {payload := 'bar'})"); },
      ThrowsMessage<IllegalQueryException>(HasSubstr("cannot be assigned to a column of type"))
   );
}

TEST(BinaryColumnQuery, survivesDeletingRows) {
   auto database = makeDatabaseWithBinaryTable();
   std::ignore = executeWrite(database, "delete(blobs, where := key = 'a' || key = 'c')");
   EXPECT_EQ(
      executeQuery(database, "blobs.project({key, payload})"),
      nlohmann::json::parse(R"([{"key": "b", "payload": "Zm9v"}, {"key": "d", "payload": ""}])")
   );
}

TEST(BinaryColumnQuery, roundTripsThroughInsertInto) {
   auto database = makeDatabaseWithBinaryTable();
   std::ignore = executeWrite(database, "createTable(copy, {key := string, payload := binary})");
   std::ignore = executeWrite(database, "blobs.project({key, payload}).insertInto(copy)");
   EXPECT_EQ(executeQuery(database, "copy.project({key, payload})"), ALL_ROWS);
}

TEST(BinaryColumnQuery, survivesSaveAndLoad) {
   auto database = makeDatabaseWithBinaryTable();
   const auto directory = std::filesystem::temp_directory_path() / "rhydb_binary_column_test";
   std::filesystem::remove_all(directory);
   database.saveDatabaseState(directory);
   auto loaded = rhydb::Database::loadDatabaseStateFromPath(directory);
   std::filesystem::remove_all(directory);
   ASSERT_TRUE(loaded.has_value());
   EXPECT_EQ(executeQuery(*loaded, "blobs.project({key, payload})"), ALL_ROWS);
}

TEST(BinaryColumnQuery, canBeOrderedAndGroupedByArrow) {
   auto database = makeDatabaseWithBinaryTable();
   EXPECT_EQ(
      executeQuery(database, "blobs.filter(payload.isNotNull()).order(by:={payload.desc()})"),
      nlohmann::json::parse(R"([
         {"key": "b", "payload": "Zm9v"},
         {"key": "a", "payload": "AP8="},
         {"key": "d", "payload": ""}
      ])")
   );
   EXPECT_EQ(
      executeQuery(
         database, "blobs.group(by:={payload}, aggs:={count:=count()}).order(by:={count, payload})"
      ),
      nlohmann::json::parse(R"([
         {"count": 1, "payload": null},
         {"count": 1, "payload": ""},
         {"count": 1, "payload": "AP8="},
         {"count": 1, "payload": "Zm9v"}
      ])")
   );
}
