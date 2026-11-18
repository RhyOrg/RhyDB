#include <chrono>
#include <string>

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "rhydb/config/runtime_config.h"
#include "rhydb/database.h"
#include "rhydb/schema/database_schema.h"
#include "sequence_generator.h"

namespace {

double toMs(
   std::chrono::steady_clock::time_point start,
   std::chrono::steady_clock::time_point end
) {
   return std::chrono::duration<double, std::milli>(end - start).count();
}

// Deletes half of the rows, spread over every chunk, of a table with a full-length sequence column.
// Its vertical index holds a container per position and symbol in every chunk, which all have to be
// renumbered, so this measures how deleting scales with the number of containers per chunk. The
// dataset is the one of SequenceColumnInsert, generated once by `make generateTestData`.
void run() {
   const std::string reference = readReferenceFromFile();
   auto database = initializeDatabaseWithFullSequenceSchema(reference);
   auto input = openTestDataInput(SEQUENCE_COLUMN_NDJSON);
   database->appendData(rhydb::schema::TableName::getDefault(), input);
   SPDLOG_INFO("sequences appended: {}", SEQUENCE_COLUMN_SEQUENCE_COUNT);

   const auto start = std::chrono::steady_clock::now();
   const auto result = database->executeWrite(
      "delete(data, where := key.like('[02468]$'))",
      rhydb::config::QueryOptions{},
      "delete_rows_benchmark"
   );
   const auto end = std::chrono::steady_clock::now();

   SPDLOG_INFO("delete half of the rows: {:.1f} ms", toMs(start, end));
   SPDLOG_INFO("{}", result->ToString());
}

}  // namespace

TEST(DeleteRows, deleteHalfOfTheSequences) {
   run();
}
