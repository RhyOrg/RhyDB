#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "rhydb/append/ndjson_line_reader.h"
#include "rhydb/common/phylo_tree.h"
#include "rhydb/config/database_config.h"
#include "rhydb/database.h"
#include "rhydb/initialize/initializer.h"
#include "rhydb/storage/reference_genomes.h"

// Header-only utilities shared between the performance benchmarks, which are all translation units
// of the same rhydb_benchmark binary, so the definitions here are inline.

// --- Filesystem helpers ---

inline void changeCwdToTestFolder() {
   size_t search_depth = 4;
   std::filesystem::path candidate = std::filesystem::current_path();
   for (size_t i = 0; i < search_depth; ++i, candidate = candidate / "..") {
      if (std::filesystem::exists(candidate / "testBaseData/exampleDataset")) {
         std::filesystem::current_path(candidate);
         return;
      }
   }
   throw std::runtime_error(fmt::format(
      "Should be run from the repository root; could not find it from {}",
      std::filesystem::current_path().string()
   ));
}

inline std::string readReferenceFromFile() {
   auto reference_genomes =
      rhydb::ReferenceGenomes::readFromFile("testBaseData/exampleDataset/reference_genomes.json");
   if (reference_genomes.raw_nucleotide_sequences.empty()) {
      throw std::runtime_error("No nucleotide sequences found in reference_genomes.json");
   }
   return reference_genomes.raw_nucleotide_sequences.at(0);
}

// --- Test-data files ---
//
// A benchmark reads its dataset back rather, than regenerating it on every run,
// addressing it by the file name the build declares.

constexpr std::string_view BENCHMARK_DATA_DIR = "localTestData/performance";

constexpr std::string_view SHORT_READ_SMALL_NDJSON = "short_reads_100k.ndjson";
constexpr std::string_view SHORT_READ_LARGE_NDJSON = "short_reads_5m.ndjson";
// Amplicon-coverage short reads for many_short_read_filters: the same set of reads in emission
// (amplicon-sorted) order and in a random order. Comparing ingestion of the two, with and without
// clustered buffering, is what that benchmark measures.
constexpr std::string_view SHORT_READ_AMPLICON_SORTED_NDJSON =
   "short_reads_amplicon_sorted_5m.ndjson";
constexpr std::string_view SHORT_READ_AMPLICON_SHUFFLED_NDJSON =
   "short_reads_amplicon_shuffled_5m.ndjson";
constexpr std::string_view FULL_SEQUENCE_NDJSON = "full_sequences_100k.ndjson";
constexpr std::string_view SEQUENCE_COLUMN_NDJSON = "sequence_column_sequences.ndjson";
constexpr std::string_view MUTATION_READS_NDJSON = "mutation_reads.ndjson";
constexpr std::string_view STRING_EQUALS_NDJSON = "string_equals_records.ndjson";
constexpr std::string_view CO_OCCURRENCE_NDJSON = "co_occurrence_sequences.ndjson";
// The one downloaded dataset: a `.zst` that InputStreamWrapper decompresses on the fly.
constexpr std::string_view WASAP_MUTATION_COVERAGE_ZST = "wasap_mutation_coverage.ndjson.zst";

inline std::filesystem::path benchmarkDataPath(std::string_view dataset) {
   return std::filesystem::path{BENCHMARK_DATA_DIR} / dataset;
}

// Open a dataset for writing, creating the data directory as needed.
inline std::ofstream openTestDataOutput(std::string_view dataset) {
   const std::filesystem::path out_path = benchmarkDataPath(dataset);
   std::filesystem::create_directories(out_path.parent_path());
   std::ofstream out{out_path, std::ios::binary | std::ios::trunc};
   if (!out) {
      throw std::runtime_error(fmt::format("Could not open {} for writing", out_path.string()));
   }
   return out;
}

// Open a dataset for reading, with a hint to run the generator if it is missing.
inline std::ifstream openTestDataInput(std::string_view dataset) {
   const std::filesystem::path in_path = benchmarkDataPath(dataset);
   std::ifstream in{in_path, std::ios::binary};
   if (!in) {
      throw std::runtime_error(fmt::format(
         "Could not open {}. Prepare benchmark data first with `make generateTestData`.",
         in_path.string()
      ));
   }
   return in;
}

// --- Sequence evolution model ---

constexpr double DEFAULT_MUTATION_RATE = 0.001;
constexpr double DEFAULT_DEATH_RATE = 0.1;
constexpr size_t DEFAULT_GENERATIONS = 5;
constexpr size_t DEFAULT_CHILDREN_PER_NODE = 3;

class SequenceTreeGenerator {
   std::mt19937 rng;
   const std::string& reference;
   double mutation_rate;
   double death_rate;
   size_t generations;
   size_t children_per_node;

   char mutateBase(char base) {
      std::uniform_int_distribution<size_t> dist(0, 3);
      char new_base;
      do {
         rhydb::Nucleotide::Symbol new_symbol = rhydb::Nucleotide::SYMBOLS.at(dist(rng));
         new_base = rhydb::Nucleotide::symbolToChar(new_symbol);
      } while (new_base == base);
      return new_base;
   }

   std::string mutateSequence(std::string_view sequence) {
      std::string mutated{sequence};
      std::binomial_distribution<size_t> num_mutations_dist(sequence.size(), mutation_rate);
      const size_t num_mutations = num_mutations_dist(rng);
      std::uniform_int_distribution<size_t> pos_dist(0, sequence.size() - 1);
      for (size_t i = 0; i < num_mutations; ++i) {
         const size_t pos = pos_dist(rng);
         mutated[pos] = mutateBase(mutated[pos]);
      }
      return mutated;
   }

  public:
   SequenceTreeGenerator(
      const std::string& ref,
      uint64_t seed = 42,
      double mut_rate = DEFAULT_MUTATION_RATE,
      double death = DEFAULT_DEATH_RATE,
      size_t gens = DEFAULT_GENERATIONS,
      size_t children = DEFAULT_CHILDREN_PER_NODE
   )
       : rng(seed),
         reference(ref),
         mutation_rate(mut_rate),
         death_rate(death),
         generations(gens),
         children_per_node(children) {}

   std::vector<std::string> generateEvolvedSequences() {
      std::vector<std::string> all_sequences = {reference};
      std::vector<size_t> current_gen = {0};
      std::bernoulli_distribution survives(1.0 - death_rate);
      for (size_t gen = 0; gen < generations; ++gen) {
         std::vector<size_t> next_gen;
         for (size_t seq_index : current_gen) {
            for (size_t child = 0; child < children_per_node; ++child) {
               if (survives(rng)) {
                  all_sequences.push_back(mutateSequence(all_sequences.at(seq_index)));
                  next_gen.push_back(all_sequences.size() - 1);
               }
            }
         }
         if (next_gen.empty()) {
            next_gen.push_back(all_sequences.size() - 1);
         }
         current_gen = std::move(next_gen);
      }
      return all_sequences;
   }
};

// --- Short-read generation ---

constexpr size_t DEFAULT_READ_COUNT = 5'000'000;
constexpr size_t DEFAULT_READ_LENGTH = 200;
// Real amplicon / targeted-panel sequencing produces reads from a fixed set of primer-defined
// windows, not uniformly across the genome. Modeling that yields concrete coverage classes: every
// read of a given amplicon shares the same covered range. This is the coverage structure clustered
// ingestion is designed for, so the amplicon datasets are generated with this many coverage
// classes.
constexpr size_t DEFAULT_NUM_AMPLICONS = 100;

struct ShortRead {
   size_t id;
   size_t offset;
   std::string sequence;
};

class ShortReadGenerator {
   std::vector<std::string> evolved_sequences;
   std::mt19937 rng;
   std::uniform_int_distribution<size_t> seq_dist;
   size_t count;
   size_t read_length;
   size_t num_positions;
   // Engaged => amplicon mode: reads are drawn only from these fixed window start offsets instead
   // of tiling every position uniformly. Empty => uniform whole-genome tiling.
   std::vector<size_t> amplicon_starts;

  public:
   class iterator {
      ShortReadGenerator* generator;
      size_t current_id;

     public:
      using iterator_category = std::input_iterator_tag;
      using value_type = ShortRead;
      using difference_type = std::ptrdiff_t;

      iterator(ShortReadGenerator* gen, size_t id)
          : generator(gen),
            current_id(id) {}

      ShortRead operator*() { return generator->generateAt(current_id); }
      iterator& operator++() {
         ++current_id;
         return *this;
      }
      iterator operator++(int) {
         iterator tmp = *this;
         ++current_id;
         return tmp;
      }
      bool operator==(const iterator& other) const { return current_id == other.current_id; }
      bool operator!=(const iterator& other) const { return current_id != other.current_id; }
   };

   // num_amplicons == 0 selects uniform whole-genome tiling (every position is covered). A positive
   // value draws reads from that many evenly-spaced fixed amplicon windows instead, modelling
   // targeted/amplicon sequencing.
   ShortReadGenerator(
      const std::string& reference,
      size_t count,
      size_t read_length,
      size_t num_amplicons = 0,
      uint64_t seed = 42
   )
       : count(count),
         read_length(read_length) {
      if (read_length > reference.size()) {
         throw std::invalid_argument(fmt::format(
            "read_length ({}) exceeds reference length ({})", read_length, reference.size()
         ));
      }
      SequenceTreeGenerator tree_gen(reference, seed);
      evolved_sequences = tree_gen.generateEvolvedSequences();
      num_positions = reference.size() - read_length + 1;
      if (num_amplicons == 0) {
         SPDLOG_INFO("Generated {} evolved sequences from tree model", evolved_sequences.size());
      } else {
         // Evenly tile num_amplicons fixed windows across [0, reference - read_length]. These are
         // the concrete coverage classes: reads are drawn only from these window starts.
         const size_t max_start = reference.size() - read_length;
         amplicon_starts.reserve(num_amplicons);
         for (size_t i = 0; i < num_amplicons; ++i) {
            amplicon_starts.push_back(
               num_amplicons == 1 ? 0 : (i * max_start) / (num_amplicons - 1)
            );
         }
         SPDLOG_INFO(
            "Generated {} evolved sequences from tree model; tiling {} amplicons of length {}",
            evolved_sequences.size(),
            num_amplicons,
            read_length
         );
      }
      rng.seed(seed + 1000);
      seq_dist = std::uniform_int_distribution<size_t>(0, evolved_sequences.size() - 1);
   }

   ShortRead generateAt(size_t read_id) {
      size_t offset;
      if (amplicon_starts.empty()) {
         offset = (read_id * num_positions) / count;
      } else {
         // Assign reads to amplicons monotonically in read_id, so the default emission order is
         // amplicon- (hence offset-) sorted. All reads of an amplicon share its covered window.
         const size_t amplicon =
            std::min((read_id * amplicon_starts.size()) / count, amplicon_starts.size() - 1);
         offset = amplicon_starts[amplicon];
      }
      const auto& source_seq = evolved_sequences[seq_dist(rng)];
      return {read_id, offset, source_seq.substr(offset, read_length)};
   }

   iterator begin() { return iterator(this, 0); }
   iterator end() { return iterator(this, count); }
   [[nodiscard]] size_t size() const { return count; }
};

// --- NDJSON generators ---
//
// The generators stream directly to an ostream rather than returning a buffer, so datasets of any
// size can be written to disk without being held in memory.

inline void writeShortReadNdjson(
   std::ostream& out,
   const std::string& reference,
   size_t count = DEFAULT_READ_COUNT,
   size_t read_length = DEFAULT_READ_LENGTH
) {
   ShortReadGenerator generator(reference, count, read_length);
   for (const auto& read : generator) {
      out
         << fmt::format(
               R"({{"readId":"read_{}","samplingDate":"2024-01-01","locationName":"generated","main":{{"insertions":[],"offset":{},"sequence":"{}"}}}})",
               read.id,
               read.offset,
               read.sequence
            )
         << "\n";
   }
}

// Writes amplicon-coverage short reads (see DEFAULT_NUM_AMPLICONS). With shuffle=false the reads
// are emitted in amplicon-sorted order, so ingestion sees coverage windows contiguously; with
// shuffle=true the exact same reads are emitted in a random order, scattering every amplicon across
// all ingestion chunks. The two orderings share the same seed, so the shuffled file is a true
// permutation of the sorted one and the databases they build are identical.
inline void writeAmpliconShortReadNdjson(
   std::ostream& out,
   const std::string& reference,
   bool shuffle,
   size_t count = DEFAULT_READ_COUNT,
   size_t read_length = DEFAULT_READ_LENGTH,
   size_t num_amplicons = DEFAULT_NUM_AMPLICONS
) {
   ShortReadGenerator generator(reference, count, read_length, num_amplicons);
   const auto formatRead = [](const ShortRead& read) {
      return fmt::format(
         R"({{"readId":"read_{}","samplingDate":"2024-01-01","locationName":"generated","main":{{"insertions":[],"offset":{},"sequence":"{}"}}}})",
         read.id,
         read.offset,
         read.sequence
      );
   };
   if (!shuffle) {
      for (const auto& read : generator) {
         out << formatRead(read) << "\n";
      }
      return;
   }
   std::vector<std::string> lines;
   lines.reserve(count);
   for (const auto& read : generator) {
      lines.push_back(formatRead(read));
   }
   std::mt19937 shuffle_rng(12345);
   std::ranges::shuffle(lines, shuffle_rng);
   for (const auto& line : lines) {
      out << line << "\n";
   }
}

constexpr size_t DEFAULT_FULL_SEQ_COUNT = 100'000;

inline void writeFullSequenceNdjson(
   std::ostream& out,
   const std::string& reference,
   size_t count = DEFAULT_FULL_SEQ_COUNT
) {
   SequenceTreeGenerator tree_gen(reference);
   const auto evolved = tree_gen.generateEvolvedSequences();
   SPDLOG_INFO(
      "Repeating {} evolved sequences to fill {} full-sequence entries", evolved.size(), count
   );
   for (size_t i = 0; i < count; ++i) {
      const auto& seq = evolved[i % evolved.size()];
      out << fmt::format(R"({{"key":"{}","main":{{"sequence":"{}","insertions":[]}}}})", i, seq)
          << "\n";
   }
}

// --- Sequence-column insert data (full-length sequences with realistic N runs) ---

constexpr size_t SEQUENCE_COLUMN_SEQUENCE_COUNT = 100'000;

// Full-length evolved sequences with realistic-ish N runs at both ends plus a few internal
// stretches, emitted as full-sequence NDJSON so the benchmark can ingest them through appendData.
inline void writeNRunSequenceNdjson(std::ostream& out, const std::string& reference) {
   SequenceTreeGenerator tree_gen(reference, 42, 0.001, 0.1, 12, 3);
   auto evolved = tree_gen.generateEvolvedSequences();
   SPDLOG_INFO("generated {} distinct evolved sequences", evolved.size());

   std::mt19937 rng(7);
   std::uniform_int_distribution<size_t> pick(0, evolved.size() - 1);
   // realistic-ish N runs at both ends plus a few internal N stretches
   std::uniform_int_distribution<size_t> head_n(0, 300);
   std::uniform_int_distribution<size_t> tail_n(0, 300);
   std::uniform_int_distribution<size_t> internal_runs(0, 5);
   std::uniform_int_distribution<size_t> run_len(1, 100);
   std::uniform_int_distribution<size_t> pos_dist(0, reference.size() - 200);

   for (size_t i = 0; i < SEQUENCE_COLUMN_SEQUENCE_COUNT; ++i) {
      std::string sequence = evolved.at(pick(rng));
      const size_t head = std::min(head_n(rng), sequence.size());
      const size_t tail = std::min(tail_n(rng), sequence.size());
      for (size_t j = 0; j < head; ++j) {
         sequence[j] = 'N';
      }
      for (size_t j = 0; j < tail; ++j) {
         sequence[sequence.size() - 1 - j] = 'N';
      }
      const size_t runs = internal_runs(rng);
      for (size_t r = 0; r < runs; ++r) {
         const size_t start = pos_dist(rng);
         const size_t length = run_len(rng);
         const size_t end = std::min(start + length, sequence.size());
         for (size_t j = start; j < end; ++j) {
            sequence[j] = 'N';
         }
      }
      out << fmt::format(
                R"({{"key":"{}","main":{{"sequence":"{}","insertions":[]}}}})", i, sequence
             )
          << "\n";
   }
}

// --- Mutation benchmark data (synthetic short reads over a repeated ACGT reference) ---

constexpr size_t MUTATION_REFERENCE_REPEATS = 1000;  // "ACGT" repeated -> 4000nt reference

inline std::string buildMutationBenchmarkReference() {
   std::string reference;
   reference.reserve(4 * MUTATION_REFERENCE_REPEATS);
   for (size_t i = 0; i < MUTATION_REFERENCE_REPEATS; ++i) {
      reference += "ACGT";
   }
   return reference;
}

// Fixed distribution of 3.2M short (4nt) reads across a handful of offsets, matching the original
// inline generation (running key, sequence always "ACGT").
inline void writeMutationBenchmarkNdjson(std::ostream& out) {
   size_t current_id = 0;
   const auto emitBatches = [&](size_t batches, size_t offset) {
      for (size_t batch = 0; batch < batches; ++batch) {
         for (size_t i = 0; i < 1000; ++i) {
            out << fmt::format(
                      R"({{"key":"{}","main":{{"sequence":"ACGT","offset":{},"insertions":[]}}}})",
                      current_id++,
                      offset
                   )
                << "\n";
         }
      }
   };
   emitBatches(1000, 0);
   emitBatches(1000, 4);
   emitBatches(100, 99);
   for (size_t i = 0; i < 100; ++i) {
      emitBatches(1, 100 + i);
   }
   emitBatches(1000, 2000);
}

// --- String-equals benchmark data ---

constexpr size_t STRING_EQUALS_RECORD_COUNT = 100'000;
constexpr std::array<std::string_view, 6> STRING_EQUALS_COUNTRIES =
   {"USA", "Germany", "France", "UK", "China", "Japan"};

inline void writeStringEqualsNdjson(std::ostream& out) {
   for (size_t i = 0; i < STRING_EQUALS_RECORD_COUNT; ++i) {
      out << fmt::format(
                R"({{"accession":"ACC{:06}","country":"{}"}})",
                i,
                STRING_EQUALS_COUNTRIES[i % STRING_EQUALS_COUNTRIES.size()]
             )
          << "\n";
   }
}

// --- Co-occurrence benchmark data (random sequences over a short random reference) ---

constexpr size_t CO_OCCURRENCE_REFERENCE_LENGTH = 100;
constexpr size_t CO_OCCURRENCE_NUM_SEQUENCES = 2'000'000;
constexpr double CO_OCCURRENCE_MUTATION_RATE = 0.1;

inline std::string makeCoOccurrenceReference() {
   constexpr std::array<char, 4> bases{'A', 'C', 'G', 'T'};
   std::mt19937 rng{42};
   std::uniform_int_distribution<size_t> base_dist(0, bases.size() - 1);
   std::string reference(CO_OCCURRENCE_REFERENCE_LENGTH, 'A');
   for (char& base : reference) {
      base = bases.at(base_dist(rng));
   }
   return reference;
}

inline void writeCoOccurrenceNdjson(std::ostream& out, const std::string& reference) {
   constexpr std::array<char, 4> bases{'A', 'C', 'G', 'T'};
   std::mt19937 rng{1234};
   std::uniform_int_distribution<size_t> base_dist(0, bases.size() - 1);
   std::binomial_distribution<size_t> mutation_count(
      CO_OCCURRENCE_REFERENCE_LENGTH, CO_OCCURRENCE_MUTATION_RATE
   );
   std::uniform_int_distribution<size_t> pos_dist(0, CO_OCCURRENCE_REFERENCE_LENGTH - 1);

   std::string sequence;
   sequence.reserve(CO_OCCURRENCE_REFERENCE_LENGTH);
   for (size_t row = 0; row < CO_OCCURRENCE_NUM_SEQUENCES; ++row) {
      sequence.assign(reference);
      const size_t mutations = mutation_count(rng);
      for (size_t i = 0; i < mutations; ++i) {
         sequence[pos_dist(rng)] = bases.at(base_dist(rng));
      }
      out << fmt::format(
                R"({{"primaryKey":"id_{}","main":{{"sequence":"{}","insertions":[]}}}})",
                row,
                sequence
             )
          << '\n';
   }
}

// --- Database initializers ---

inline std::shared_ptr<rhydb::Database> initializeDatabaseWithShortReadSchema(
   const std::string& reference
) {
   auto database_config = rhydb::config::DatabaseConfig::getValidatedConfig(R"(
schema:
  instanceName: test
  metadata:
    - name: readId
      type: string
    - name: samplingDate
      type: date
    - name: locationName
      type: string
  primaryKey: readId
)");
   rhydb::ReferenceGenomes reference_genomes{{{"main", reference}}, {}};
   auto database = std::make_shared<rhydb::Database>();
   database->createTable(
      rhydb::schema::TableName::getDefault(),
      rhydb::initialize::Initializer::createSchemaFromConfigFiles(
         std::move(database_config),
         std::move(reference_genomes),
         {},
         // NOLINTNEXTLINE(readability-trailing-comma): false positive on empty brace, llvm#223646
         rhydb::common::PhyloTree{},
         /*without_unaligned_sequences=*/true
      )
   );
   return database;
}

inline std::shared_ptr<rhydb::Database> initializeDatabaseWithFullSequenceSchema(
   const std::string& reference
) {
   auto database_config = rhydb::config::DatabaseConfig::getValidatedConfig(R"(
schema:
  instanceName: test
  metadata:
    - name: key
      type: string
  primaryKey: key
)");
   rhydb::ReferenceGenomes reference_genomes{{{"main", reference}}, {}};
   auto database = std::make_shared<rhydb::Database>();
   database->createTable(
      rhydb::schema::TableName::getDefault(),
      rhydb::initialize::Initializer::createSchemaFromConfigFiles(
         std::move(database_config),
         std::move(reference_genomes),
         {},
         // NOLINTNEXTLINE(readability-trailing-comma): false positive on empty brace, llvm#223646
         rhydb::common::PhyloTree{},
         /*without_unaligned_sequences=*/true
      )
   );
   return database;
}
