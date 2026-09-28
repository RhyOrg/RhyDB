#include "admin_query_handler.h"

#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#include <Poco/Net/HTTPResponse.h>
#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <rhydb/common/rhydb_directory.h>
#include <rhydb/config/runtime_config.h>
#include <rhydb/database.h>
#include <rhydb/schema/database_schema.h>
#include <rhydb/storage/column/string_column.h>

#include "active_database.h"
#include "manual_poco_mocks.test.h"
#include "request_handler_factory.h"

namespace {

using rhydb::schema::ColumnIdentifier;
using rhydb::schema::ColumnType;
using rhydb::schema::TableName;
using rhydb::schema::TableSchema;
using rhydb::storage::column::ColumnMetadata;
using rhydb::storage::column::StringColumnMetadata;

// A metadata-only schema (string primary key + a string and an int32 column) shared by the source
// and target tables of the test fixture.
std::shared_ptr<TableSchema> makeValueSchema() {
   const ColumnIdentifier key{.name = "key", .type = ColumnType::STRING};
   const ColumnIdentifier country{.name = "country", .type = ColumnType::STRING};
   const ColumnIdentifier age{.name = "age", .type = ColumnType::INT32};
   std::map<ColumnIdentifier, std::shared_ptr<ColumnMetadata>> column_metadata{
      {key, std::make_shared<StringColumnMetadata>(key.name)},
      {country, std::make_shared<StringColumnMetadata>(country.name)},
      {age, std::make_shared<ColumnMetadata>(age.name)},
   };
   return std::make_shared<TableSchema>(std::move(column_metadata), key);
}

// A data directory of its own for each test: the admin endpoint loads the state it writes to from
// there and saves the result back, so the tests need a real, writable directory.
class TemporaryDataDirectory {
   std::filesystem::path directory;

  public:
   TemporaryDataDirectory() {
      static std::atomic<size_t> next_id{0};
      directory = std::filesystem::temp_directory_path() /
                  fmt::format("rhydb_admin_query_test_{}", next_id++);
      std::filesystem::remove_all(directory);
      std::filesystem::create_directories(directory);
   }

   TemporaryDataDirectory(const TemporaryDataDirectory&) = delete;
   TemporaryDataDirectory(TemporaryDataDirectory&&) = delete;
   TemporaryDataDirectory& operator=(const TemporaryDataDirectory&) = delete;
   TemporaryDataDirectory& operator=(TemporaryDataDirectory&&) = delete;

   ~TemporaryDataDirectory() { std::filesystem::remove_all(directory); }

   [[nodiscard]] const std::filesystem::path& path() const { return directory; }

   /// The data versions saved in the directory, oldest first. One entry per saved state, so this
   /// also tells whether a request wrote a new state at all.
   [[nodiscard]] std::vector<std::string> savedDataVersions() const {
      std::vector<std::string> versions;
      for (const auto& entry : std::filesystem::directory_iterator{directory}) {
         versions.push_back(entry.path().filename().string());
      }
      std::ranges::sort(versions);
      return versions;
   }
};

// Active database holding a `source` table with three rows (two country='CH', one 'US') and an
// empty `archive` table with the same schema. It is saved to `data_directory` as well, because that
// is the state the admin endpoint applies its write to.
std::shared_ptr<rhydb_app::ActiveDatabase> makeActiveDatabaseWithSourceData(
   const std::filesystem::path& data_directory
) {
   rhydb::Database database;
   database.createTable(TableName{"source"}, makeValueSchema());
   database.createTable(TableName{"archive"}, makeValueSchema());

   std::stringstream source_data;
   source_data << R"({"key":"a","country":"CH","age":1})" << "\n"
               << R"({"key":"b","country":"US","age":2})" << "\n"
               << R"({"key":"c","country":"CH","age":3})" << "\n";
   database.appendData(TableName{"source"}, source_data);

   database.saveDatabaseState(data_directory);

   auto handle = std::make_shared<rhydb_app::ActiveDatabase>();
   handle->setActiveDatabase(std::move(database));
   return handle;
}

// The runtime config the API is served with in these tests: the write-enabled admin endpoint is
// opt-in, so it has to be switched on explicitly.
rhydb::config::RuntimeConfig configWithAdminEndpoint(
   const std::filesystem::path& data_directory,
   bool allow_admin_endpoint
) {
   auto runtime_config = rhydb::config::RuntimeConfig::withDefaults();
   runtime_config.data_directory = data_directory;
   runtime_config.api_options.allow_admin_endpoint = allow_admin_endpoint;
   return runtime_config;
}

// Routes `POST /admin/query` with the given body through the full handler factory (so request-id
// assignment and exception-to-status mapping are exercised) and returns the populated response.
void postAdminQuery(
   const std::shared_ptr<rhydb_app::ActiveDatabase>& handle,
   const std::filesystem::path& data_directory,
   const std::string& query,
   rhydb_app::test::MockResponse& response,
   bool allow_admin_endpoint = true
) {
   rhydb_app::test::MockRequest request(response);
   request.setMethod("POST");
   request.setURI("/admin/query");
   request.in_stream << query;

   rhydb_app::RhyDBRequestHandlerFactory factory{
      configWithAdminEndpoint(data_directory, allow_admin_endpoint), handle,
   };
   std::unique_ptr<Poco::Net::HTTPRequestHandler> handler{factory.createRequestHandler(request)};
   handler->handleRequest(request, response);
}

// The state the data directory would be reloaded from on a restart.
rhydb::Database reloadFromDataDirectory(const std::filesystem::path& data_directory) {
   return rhydb::Database::loadDatabaseState(
      rhydb::RhyDBDirectory{data_directory}.getMostRecentDataDirectory().value()
   );
}

}  // namespace

TEST(AdminQueryHandler, insertsQueryResultAndReportsRowCount) {
   const TemporaryDataDirectory data_directory;
   auto handle = makeActiveDatabaseWithSourceData(data_directory.path());

   rhydb_app::test::MockResponse response;
   postAdminQuery(
      handle, data_directory.path(), "source.filter(country='CH').insertInto(archive)", response
   );

   EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_OK);

   const auto body = nlohmann::json::parse(response.out_stream.str());
   EXPECT_EQ(body.at("insertedRows").get<size_t>(), 2);

   // A successful write reports the data version it produced, so that a client can tell which
   // version it has to wait for to query the data it just wrote.
   ASSERT_TRUE(response.has("data-version"));
   EXPECT_THAT(response.get("data-version"), testing::MatchesRegex("[0-9]{10}"));

   // The rows really landed in the target table, and the source table is untouched.
   auto written = reloadFromDataDirectory(data_directory.path());
   EXPECT_EQ(written.getDataVersionTimestamp().value, response.get("data-version"));
   EXPECT_EQ(written.tables.at(TableName{"archive"})->row_layout.numRows(), 2U);
   EXPECT_EQ(written.tables.at(TableName{"source"})->row_layout.numRows(), 3U);
}

// The write is not only applied in memory: it is saved to the data directory as a new data version
// before it is published, so it is still there after a restart.
TEST(AdminQueryHandler, persistsTheWriteToTheDataDirectory) {
   const TemporaryDataDirectory data_directory;
   auto handle = makeActiveDatabaseWithSourceData(data_directory.path());
   const auto versions_before = data_directory.savedDataVersions();
   ASSERT_EQ(versions_before.size(), 1U);

   rhydb_app::test::MockResponse response;
   postAdminQuery(
      handle, data_directory.path(), "source.filter(country='CH').insertInto(archive)", response
   );
   ASSERT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_OK);

   // A second state was saved, under the data version the response reports, and the state that was
   // written to is left as it was.
   const auto versions_after = data_directory.savedDataVersions();
   ASSERT_EQ(versions_after.size(), 2U);
   EXPECT_EQ(versions_after.at(0), versions_before.at(0));
   EXPECT_EQ(versions_after.at(1), response.get("data-version"));

   // Reloading the directory - what a restarted server does - gets the inserted rows back.
   auto reloaded = reloadFromDataDirectory(data_directory.path());
   EXPECT_EQ(reloaded.getDataVersionTimestamp().value, response.get("data-version"));
   EXPECT_EQ(reloaded.tables.at(TableName{"archive"})->row_layout.numRows(), 2U);
   EXPECT_EQ(reloaded.tables.at(TableName{"source"})->row_layout.numRows(), 3U);
}

// Publishing the new state is left to the directory watcher, the only writer of the active
// database. The handler must not swap it in itself: the watcher decides whether to load before it
// loads, so a load that started before such a swap would publish its stale database over the state
// the write just saved. The served database is therefore untouched when the response is sent (no
// watcher runs in this test) and is only replaced on the watcher's next poll.
TEST(AdminQueryHandler, leavesPublishingTheNewStateToTheDirectoryWatcher) {
   const TemporaryDataDirectory data_directory;
   auto handle = makeActiveDatabaseWithSourceData(data_directory.path());
   const auto database_before = handle->getActiveDatabase();
   const auto data_version_before = database_before->getDataVersionTimestamp();

   rhydb_app::test::MockResponse response;
   postAdminQuery(
      handle, data_directory.path(), "source.filter(country='CH').insertInto(archive)", response
   );
   ASSERT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_OK);

   EXPECT_EQ(handle->getActiveDatabase(), database_before);
   EXPECT_EQ(
      handle->getActiveDatabase()->getDataVersionTimestamp().value, data_version_before.value
   );
   EXPECT_EQ(database_before->tables.at(TableName{"archive"})->row_layout.numRows(), 0U);
   // The state the watcher will pick up is a newer one, and it holds the inserted rows.
   EXPECT_GT(response.get("data-version"), data_version_before.value);
   EXPECT_EQ(
      reloadFromDataDirectory(data_directory.path())
         .tables.at(TableName{"archive"})
         ->row_layout.numRows(),
      2U
   );
}

// A statement that only fails once the append is under way must leave no trace: it ran against the
// loaded database, which is then dropped instead of being saved.
TEST(AdminQueryHandler, discardsAFailedWriteWithoutSavingIt) {
   const TemporaryDataDirectory data_directory;
   auto handle = makeActiveDatabaseWithSourceData(data_directory.path());
   const auto database_before = handle->getActiveDatabase();
   const auto data_version_before = database_before->getDataVersionTimestamp();
   const auto versions_before = data_directory.savedDataVersions();

   rhydb_app::test::MockResponse response;
   // The projection drops `age`, which the target table requires, so the append rejects the batch
   // after the statement has been parsed and planned.
   postAdminQuery(
      handle, data_directory.path(), "source.project({key}).insertInto(archive)", response
   );

   EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
   EXPECT_THAT(
      response.out_stream.str(),
      testing::HasSubstr("the column 'age' is not contained in the object")
   );
   // Still the very same database, at the very same data version, and nothing new on disk.
   EXPECT_EQ(handle->getActiveDatabase(), database_before);
   EXPECT_EQ(
      handle->getActiveDatabase()->getDataVersionTimestamp().value, data_version_before.value
   );
   EXPECT_EQ(
      handle->getActiveDatabase()->tables.at(TableName{"archive"})->row_layout.numRows(), 0U
   );
   EXPECT_EQ(data_directory.savedDataVersions(), versions_before);
}

// The endpoint writes through the data directory, so it cannot serve a database that is not in
// there. Rather than writing to a state that is not the one being served, it reports the problem.
TEST(AdminQueryHandler, failsWhenTheDataDirectoryHoldsNoState) {
   const TemporaryDataDirectory data_directory;
   const TemporaryDataDirectory empty_directory;
   auto handle = makeActiveDatabaseWithSourceData(data_directory.path());

   rhydb_app::test::MockResponse response;
   postAdminQuery(
      handle, empty_directory.path(), "insertInto(source.filter(country='CH'), archive)", response
   );

   EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR);
   EXPECT_THAT(response.out_stream.str(), testing::HasSubstr("holds no loadable database state"));
   EXPECT_EQ(
      handle->getActiveDatabase()->tables.at(TableName{"archive"})->row_layout.numRows(), 0U
   );
}

TEST(AdminQueryHandler, rejectsPlainReadQueryWithBadRequest) {
   const TemporaryDataDirectory data_directory;
   auto handle = makeActiveDatabaseWithSourceData(data_directory.path());

   rhydb_app::test::MockResponse response;
   postAdminQuery(handle, data_directory.path(), "source.filter(country='CH')", response);

   EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
   EXPECT_THAT(response.out_stream.str(), testing::HasSubstr("expected a write statement"));
   // Nothing was inserted.
   EXPECT_EQ(
      handle->getActiveDatabase()->tables.at(TableName{"archive"})->row_layout.numRows(), 0U
   );
}

TEST(AdminQueryHandler, rejectsUnknownTargetTableWithBadRequest) {
   const TemporaryDataDirectory data_directory;
   auto handle = makeActiveDatabaseWithSourceData(data_directory.path());

   rhydb_app::test::MockResponse response;
   postAdminQuery(handle, data_directory.path(), "source.insertInto(does_not_exist)", response);

   EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
   EXPECT_THAT(
      response.out_stream.str(), testing::HasSubstr("target table 'does_not_exist' not found")
   );
}

// The admin endpoint is opt-in: with `api.allowAdminEndpoint` left at its default the path is not
// routed at all, so an instance that does not enable it stays read-only.
TEST(AdminQueryHandler, isNotServedWhenNotEnabled) {
   const TemporaryDataDirectory data_directory;
   auto handle = makeActiveDatabaseWithSourceData(data_directory.path());

   rhydb_app::test::MockResponse response;
   postAdminQuery(
      handle,
      data_directory.path(),
      "source.filter(country='CH').insertInto(archive)",
      response,
      /*allow_admin_endpoint=*/false
   );

   EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_NOT_FOUND);
   EXPECT_EQ(
      handle->getActiveDatabase()->tables.at(TableName{"archive"})->row_layout.numRows(), 0U
   );
}

// Two admin requests must never write at the same time. All requests go through one factory here
// (as they do in the server), so they share the write mutex; without it the concurrent writes would
// load the same state and the last swap would drop everything the others inserted.
TEST(AdminQueryHandler, serializesConcurrentWrites) {
   const TemporaryDataDirectory data_directory;
   auto handle = makeActiveDatabaseWithSourceData(data_directory.path());

   rhydb_app::RhyDBRequestHandlerFactory factory{
      configWithAdminEndpoint(data_directory.path(), true), handle,
   };

   constexpr size_t NUMBER_OF_THREADS = 4;
   constexpr size_t REQUESTS_PER_THREAD = 3;

   std::vector<std::thread> writers;
   writers.reserve(NUMBER_OF_THREADS);
   for (size_t thread_index = 0; thread_index < NUMBER_OF_THREADS; ++thread_index) {
      writers.emplace_back([&factory]() {
         for (size_t request_index = 0; request_index < REQUESTS_PER_THREAD; ++request_index) {
            rhydb_app::test::MockResponse response;
            rhydb_app::test::MockRequest request(response);
            request.setMethod("POST");
            request.setURI("/admin/query");
            request.in_stream << "source.filter(country='CH').insertInto(archive)";

            std::unique_ptr<Poco::Net::HTTPRequestHandler> handler{
               factory.createRequestHandler(request),
            };
            handler->handleRequest(request, response);

            EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_OK);
         }
      });
   }
   for (auto& writer : writers) {
      writer.join();
   }

   // Every request built on the state that the one before it saved, so the rows accumulate and
   // none of them are lost to a race.
   EXPECT_EQ(
      reloadFromDataDirectory(data_directory.path())
         .tables.at(TableName{"archive"})
         ->row_layout.numRows(),
      NUMBER_OF_THREADS * REQUESTS_PER_THREAD * 2
   );
   // Each of them saved a state of its own: the data versions of writes within the same second do
   // not collide, which they would if the second-resolution timestamp were used as-is.
   EXPECT_EQ(
      data_directory.savedDataVersions().size(), 1 + (NUMBER_OF_THREADS * REQUESTS_PER_THREAD)
   );
}

// The read-only `/query` endpoint must reject a write (insert) statement (it parses to a
// WriteCommand, not a QueryNode), respond 400, and leave the target table untouched.
TEST(QueryHandler, rejectsInsertQueryOnReadOnlyEndpointWithBadRequest) {
   const TemporaryDataDirectory data_directory;
   auto handle = makeActiveDatabaseWithSourceData(data_directory.path());

   rhydb_app::test::MockResponse response;
   rhydb_app::test::MockRequest request(response);
   request.setMethod("POST");
   request.setURI("/query");
   request.in_stream << "source.filter(country='CH').insertInto(archive)";

   rhydb_app::RhyDBRequestHandlerFactory factory{
      configWithAdminEndpoint(data_directory.path(), true), handle,
   };
   std::unique_ptr<Poco::Net::HTTPRequestHandler> handler{factory.createRequestHandler(request)};
   handler->handleRequest(request, response);

   EXPECT_EQ(response.getStatus(), Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
   EXPECT_THAT(response.out_stream.str(), testing::HasSubstr("POST /admin/query"));
   // The guard fires before planning, so nothing is written.
   EXPECT_EQ(
      handle->getActiveDatabase()->tables.at(TableName{"archive"})->row_layout.numRows(), 0U
   );
}
