#include <algorithm>
#include <array>
#include <cstdlib>
#include <exception>
#include <ostream>
#include <ranges>
#include <string_view>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

#include "sequence_generator.h"

// Writes one synthetic benchmark dataset with fixed parameters, named by its file name:
//
//   generate_test_data short_reads_5m.ndjson
//
// Every dataset declared GENERATE in performance/CMakeLists.txt needs an entry in DATASETS below;
// the `benchmark_data` target invokes this tool once per dataset. Use `make generateTestData`.

namespace {

struct Dataset {
   std::string_view name;
   void (*write)(std::ostream&);
};

// Read on first use, so a dataset that does not need the reference does not pay for reading it.
const std::string& reference() {
   static const std::string reference = [] {
      std::string read = readReferenceFromFile();
      SPDLOG_INFO("Read reference sequence of length {}", read.size());
      return read;
   }();
   return reference;
}

constexpr auto DATASETS = std::to_array<Dataset>({
   // Short reads: 100k for nof_sequence_filter, 5M shared by many_short_read_filters and the large
   // nof_sequence_filter case.
   {

      .name = SHORT_READ_SMALL_NDJSON,
      .write = [](std::ostream& out
               ) { writeShortReadNdjson(out, reference(), DEFAULT_FULL_SEQ_COUNT); },
   },
   {
      .name = SHORT_READ_LARGE_NDJSON,
      .write = [](std::ostream& out
               ) { writeShortReadNdjson(out, reference(), DEFAULT_READ_COUNT); },
   },
   // Amplicon-coverage short reads for many_short_read_filters, emitted both amplicon-sorted and
   // randomly shuffled. The two files hold the same reads, so ingesting either builds the same
   // database; only their on-disk order (and hence the coverage layout ingestion sees) differs.
   {
      .name = SHORT_READ_AMPLICON_SORTED_NDJSON,
      .write = [](std::ostream& out
               ) { writeAmpliconShortReadNdjson(out, reference(), /*shuffle=*/false); },
   },
   {
      .name = SHORT_READ_AMPLICON_SHUFFLED_NDJSON,
      .write = [](std::ostream& out
               ) { writeAmpliconShortReadNdjson(out, reference(), /*shuffle=*/true); },
   },
   // Full-length sequences for nof_sequence_filter, and the same with N runs for
   // sequence_column_insert.
   {
      .name = FULL_SEQUENCE_NDJSON,
      .write = [](std::ostream& out) { writeFullSequenceNdjson(out, reference()); },
   },
   {
      .name = SEQUENCE_COLUMN_NDJSON,
      .write = [](std::ostream& out) { writeNRunSequenceNdjson(out, reference()); },
   },
   // Synthetic short reads for mutation_benchmark (uses its own repeated ACGT reference).
   {
      .name = MUTATION_READS_NDJSON,
      .write = [](std::ostream& out) { writeMutationBenchmarkNdjson(out); },
   },
   // Accession/country records for many_string_equals.
   {
      .name = STRING_EQUALS_NDJSON,
      .write = [](std::ostream& out) { writeStringEqualsNdjson(out); },
   },
   // Random sequences for co_occurrence_benchmark (uses its own short random reference).
   {
      .name = CO_OCCURRENCE_NDJSON,
      .write = [](std::ostream& out) { writeCoOccurrenceNdjson(out, makeCoOccurrenceReference()); },
   },
});

void writeDataset(std::string_view name) {
   const auto* const dataset = std::ranges::find(DATASETS, name, &Dataset::name);
   if (dataset == DATASETS.end()) {
      throw std::runtime_error(fmt::format(
         "Unknown benchmark dataset '{}'. Known datasets: {}",
         name,
         fmt::join(std::views::transform(DATASETS, &Dataset::name), ", ")
      ));
   }
   auto out = openTestDataOutput(name);
   dataset->write(out);
   out.flush();
   if (!out) {
      throw std::runtime_error(fmt::format("Failed while writing {}", name));
   }
   SPDLOG_INFO("Wrote {}", benchmarkDataPath(name).string());
}

}  // namespace

int main(int argc, char** argv) {
   try {
      if (argc != 2) {
         throw std::runtime_error("Usage: generate_test_data <dataset-file-name>");
      }
      changeCwdToTestFolder();
      writeDataset(argv[1]);
   } catch (const std::exception& e) {
      SPDLOG_ERROR(e.what());
      return EXIT_FAILURE;
   }
}
