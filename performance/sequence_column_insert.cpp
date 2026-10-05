#include <chrono>
#include <cstdlib>
#include <exception>
#include <string>

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "sequence_generator.h"
#include "rhydb/database.h"
#include "rhydb/schema/database_schema.h"

namespace {

double toMs(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end) {
   return std::chrono::duration<double, std::milli>(end - start).count();
}

// Ingest full-length sequences with realistic N runs through the regular appendData path (NDJSON ->
// column-group builder -> sequence column), measuring the end-to-end append time. The dataset is
// generated once by `make generateTestData` (writeNRunSequenceNdjson).
void run() {
   const std::string reference = readReferenceFromFile();
   SPDLOG_INFO("reference length {}", reference.size());

   auto database = initializeDatabaseWithFullSequenceSchema(reference);
   auto input = openTestDataInput(SEQUENCE_COLUMN_NDJSON);

   const auto start = std::chrono::steady_clock::now();
   database->appendData(rhydb::schema::TableName::getDefault(), input);
   const auto end = std::chrono::steady_clock::now();

   SPDLOG_INFO("sequences appended: {}", SEQUENCE_COLUMN_SEQUENCE_COUNT);
   SPDLOG_INFO("appendData:         {:.1f} ms", toMs(start, end));
}

}  // namespace

TEST(SequenceColumnInsert, appendSequencesWithNRuns) {
   run();
}
