# Performance Benchmarks

This folder holds the performance benchmarks. They are GTest tests in a single `rhydb_benchmark`
binary, since they take minutes and read multi-gigabyte inputs, they are not part of `make test`.

Build and run all of them (preparing their input data first if needed):

```shell
make benchmarks
```

Or build once and pick a scope, which is also how to profile a single benchmark:

```shell
cmake --build build/Release --target rhydb_benchmark

./build/Release/performance/rhydb_benchmark --gtest_list_tests
./build/Release/performance/rhydb_benchmark --gtest_filter='Mutations*'
```

Each benchmark prints its own timings and GTest reports its wall time; a benchmark that throws (for
instance because its dataset is missing) is reported as a failed test without stopping the others,
and the binary exits non-zero.

## Test data

Benchmark datasets are produced under `localTestData/performance/` (gitignored); each benchmark reads
its dataset back from there. Prepare the data once before running any benchmark:

```shell
make generateTestData
```

Every dataset is declared in `performance/CMakeLists.txt`, and `make generateTestData` builds the
`benchmark_data` CMake target that materializes them:

```cmake
benchmark_dataset(short_reads_5m.ndjson GENERATE)

benchmark_dataset(wasap_mutation_coverage.ndjson.zst
    DOWNLOAD <url>
    SHA256   <hex>)
```

`GENERATE` datasets are written locally by the `generate_test_data` tool (several gigabytes of
NDJSON), which maps each file name to its writer; the `DOWNLOAD` one is fetched and
checksum-verified. So what data exists and where it comes from is defined in the build rather than in
C++, and it is change-detected: a dataset is produced only when it is missing, when its declaration
changes, or -- for the generated ones -- when `generate_test_data.cpp`, `sequence_generator.h`, or
the reference genome they are built from changes. Re-running `make generateTestData` does nothing when nothing 
changed. If a benchmark is run before its data exists, it fails with a message pointing
back to `make generateTestData`. Benchmarks address a dataset by the same file name the build
declares (the `*_NDJSON` constants in `sequence_generator.h`) and open it with `openTestDataInput()`.

## Clustered ingestion (`clustered_ingestion_benchmark`)

`clustered_ingestion_benchmark` demonstrates what N-way clustered ingestion buffering buys. It models
amplicon/targeted sequencing: `generate_test_data` writes its reads as a fixed set of primer-defined
coverage windows (`DEFAULT_NUM_AMPLICONS` classes) in two on-disk orderings — amplicon-sorted and
randomly shuffled — that hold the exact same reads. A nucleotide-position filter only matches reads
of the one amplicon covering that position, so the query set is fast when those reads sit
contiguously in the ingested chunks (the coverage filter can skip chunks) and slow when they are
scattered across every chunk.

The benchmark builds the database three ways in one run and times the identical query set against
each:

1. amplicon-sorted input, clustering off — the ideal layout, for free;
2. amplicon-shuffled input, clustering off — worst case, coverage scattered everywhere;
3. amplicon-shuffled input, 128-way clustered ingestion — clustered buffering reorders it back.

The point is that (3) recovers the query performance of (1) from the same scattered input as (2). It
prints a summary of ingestion and query time per scenario; no environment variables or rebuilds are
needed to switch between them.

## Mutation-coverage query (`real_data_mutations_benchmark`)

`real_data_mutations_benchmark` times a co-occurrence `group` over the ~141 real SARS-CoV-2 mutation
positions in `performance/mutations.csv`. It exercises the coverage-scan / per-chunk bitmap-aggregation
path over **short reads with partial coverage** (each read covers a small genome window, so most
grouped positions are not-covered for any given read) — the path that whole-genome-sequence datasets
do not stress.

It runs against real wastewater short-read data: a fixed slice of 5 whole samples (~11.3M reads,
in RhyDB ingest format) from the GenSpectrum W-ASAP dataset — the RhyDB instance behind
`db.wasap.genspectrum.org`. The slice is hosted publicly on Hetzner Object Storage and declared in
`performance/CMakeLists.txt`:

```cmake
benchmark_dataset(wasap_mutation_coverage.ndjson.zst
    DOWNLOAD <url>
    SHA256   <hex>)
```

`make generateTestData` builds the `benchmark_data` target, which fetches it through CMake's script
mode (the download runs `performance/BenchmarkData.cmake` as a script).
