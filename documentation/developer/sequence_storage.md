# Sequence Storage

This document explains how sequences are stored in RhyDB.

## Overview

A sequence column (`storage::column::SequenceColumn<SymbolType>`, with `SymbolType` `Nucleotide` or
`AminoAcid`) stores its rows in **sequence chunks**: one immutable `SequenceChunk` per ingestion
chunk of up to 2^16 rows (see `RowId`: the global row id of row `r` of chunk `k` is `k << 16 | r`).
The chunks, their indexes and the algorithms on them live in `storage/sequence/` (namespace
`storage::sequence`).
Every chunk stores its rows as differences from its own **local reference**, which the chunk
chooses from its rows when it is built (see [Local References](#local-references)):

- **Columnar storage** for mutations: per position, the rows whose symbol differs from the local
  reference, one roaring container per symbol (the vertical sequence index).
- **Row-wise storage** for coverage: per row, the covered range and the missing (N symbols in
  nucleotides, X in amino acids) positions within it (the horizontal coverage index).

These structures avoid storing entire bitmaps for all positions, most of which would be empty,
while keeping roaring containers for all variation so that set operations stay fast.

The insertions are not part of the chunks; the column holds them in a column-wide `InsertionIndex`
(one part per chunk).

## Roaring Bitmap Containers

A roaring bitmap container represents a bitmap with a domain of `[0, 2^16)`. The roaring library's
`roaring::Roaring` is an array of such containers, each stored with the high 16 bits of its values,
which lets it represent the full 32-bit domain. A sequence chunk addresses its rows by their
16-bit index in the chunk, so a single container holds any set of the chunk's rows.

`roaring_util::RoaringContainerView` is a non-owning view of a container: it stores the container's
type (`roaring_util::ContainerType`), cardinality, run count and a pointer to its payload, and
rebuilds roaring's container header on the stack whenever it calls into roaring. It can therefore
view a container held by a `RoaringContainer` or a `roaring::Roaring` as well as a bare payload in a
flat buffer (`RoaringContainerView::fromPayload`), which is how the chunk indexes are read in place.
Code outside `roaring_util` never touches roaring's raw containers.

## Sequence Chunks

```
SequenceChunk (immutable value; copying it copies reference counts)
├── shared_ptr<const SequenceReference>   alphabet + the chunk's local reference text, shared by
│                                         the chunks of a column with an equal local reference
├── shared_ptr<const Buffer>              the vertical index blob
│   └── VerticalSequenceIndex             a view of that blob, held next to it
├── shared_ptr<const Buffer>              the horizontal coverage index blob
│   └── HorizontalCoverageIndex           a view of that blob, held next to it
└── shared_ptr<const RoaringContainer>    the chunk's null rows
```

The text of a non-null row is the reference, with every symbol the vertical index stores for the
row written over it, and with the missing symbol written over every position the row does not
cover. A null row is stored with an empty covered range and is listed in the null rows.

Invariants:

- A finished chunk is never modified. `SequenceChunkBuilder` creates chunks row by row (from a
  coverage and a list of `(position, symbol)` mutations, from coverage ranges, or by diffing a
  text against the reference with `addText`); changed chunks are new chunk objects that share the
  unchanged buffers (`withMutations`, `rebasedOnto`, `compacted`). A chunk stored in a column is
  never replaced by appends.
- The vertical index never stores a row under the reference symbol of its position, and only stores
  non-empty containers. A stored symbol may be the missing symbol (when the chunk's local reference
  differs from the column's reference at a position where the latter has the missing symbol).
- Positions and covered ranges lie within the reference; the null rows are rows of the chunk with an
  empty covered range (checked when a chunk is constructed).
- Symbols are stored as their id, the `uint8_t` value of `SymbolType::Symbol`; the
  `SequenceReference` maps ids to characters, so `SequenceChunk` is not a template.

Since a chunk is a value that only holds reference-counted, immutable buffers, the stored chunks of
a column can be handed to their readers without copying anything, and the algorithms below only need
the chunk, not the column it belongs to.

### Buffers and Ownership

Each index is stored as one pointer-free blob in a `common::Buffer`: an immutable run of bytes whose
first byte is 64-byte aligned. A `Buffer` owns its bytes and is move-only; moving it does not move
the bytes. It is created by a `BufferBuilder` (which allocates the aligned, zeroed bytes, lets them
be written with the bounds-checked `write` and hands them over with `finish()`), or read back from an
archive, which writes a buffer as its size followed by its raw bytes. `Buffer::view<T>(offset,
count)` views values of a trivially copyable type in place and panics if they lie outside the
buffer or `offset` is misaligned for `T`; the indexes read their sections only through it. Where the
bytes live is an implementation detail of `Buffer`, so a memory-mapped buffer can be added later
without changing the indexes.

Ownership is explicit at the chunk: it holds each blob as a `std::shared_ptr<const Buffer>`
(`mutationsBuffer()`, `coverageBuffer()`) and nothing else that points into it.
`VerticalSequenceIndex` and `HorizontalCoverageIndex` are non-owning views of a buffer, like
`RoaringContainerView` of a container: the parsed header and spans into the blob. `mutations()` and
`coverage()` create them on demand with `viewTrusted(buffer)`, which only reads the header (O(1));
a view (and anything viewed through it) is valid as long as the buffer lives. Keeping no views in
the chunk prepares it for a buffer manager that evicts a blob and reloads it elsewhere.

Viewing is separate from validation. `fromBuffer(buffer)` checks a blob in O(entries) and
`SequenceChunk::validate()` checks that the parts of a chunk fit together (stored positions and
symbols within the reference and its alphabet, null rows with an empty covered range). Blobs that
come from outside are validated once, when `SequenceChunk::load` reads them. Blobs built in this
process (by the index builders, `withPositionsReplaced`, rebasing or compaction) are trusted, and
only validated in debug builds, when a builder finishes and when a chunk is constructed. Both
`fromBuffer` and `viewTrusted` are deleted for rvalue buffers, so a view cannot outlive a temporary
buffer. Chunks derived from a chunk share every blob that does not change by copying its
`shared_ptr`.

### Vertical Sequence Index

The vertical index of a chunk is one immutable, position-independent blob:

```
VerticalSequenceIndexHeader                 magic "RVIS", version, num_entries, payload section
VerticalSequenceIndexEntry[num_entries]     24 bytes each, sorted by (position, symbol)
payload section                             64-byte aligned
```

An entry is `(position, cardinality, payload_offset, payload_size, symbol, type)`. The payload
section holds every container's data in roaring's own in-memory representation (the 1024 words of a
bitset container, the sorted `uint16_t` values of an array container, or the runs of a run container),
each bitset 64-byte aligned and every other payload 8-byte aligned, in native byte order.
`VerticalSequenceIndex::container(entry)` views a container in place; `entriesAtPosition(position)`
finds a position's entries by binary search. `VerticalSequenceIndex::fromBuffer` validates a blob
(header, every entry's bounds, container type, payload size and alignment, sorting) without
touching the payloads. `VerticalSequenceIndexBuilder` writes a blob from containers added in
`(position, symbol)` order; `withPositionsReplaced` writes a copy with the containers of some
positions replaced.

### Horizontal Coverage Index

The coverage index of a chunk is one immutable, position-independent blob:

```
HorizontalCoverageIndexHeader     magic "RHCI", version, num_rows, envelope (min/max start/end)
starts                            uint32_t[num_rows]
ends                              uint32_t[num_rows]
rows_with_missing                 uint32_t[num_rows_with_missing]      ascending row indexes
missing_range_offsets             uint32_t[num_rows_with_missing + 1]  into missing_ranges
missing_ranges                    MissingRange[num_missing_ranges]     ascending, disjoint per row
```

Each section starts at a 64-byte aligned offset. A row covers a position iff the position lies in
the row's `[start, end)` and in none of its missing ranges. `coveredRows(position)` scans the
starts and ends (with an AVX-512 kernel where available) unless the envelope shows that no row
(`noRowCovers`) or every row (`everyRowCovers`) covers the position. `fromBuffer` validates the
section lengths, the envelope and the missing ranges.

## Per-Chunk Algorithms

`storage/sequence/sequence_chunk_algorithms.h` holds the algorithms on the rows of one chunk. Each
takes the chunk and, where it works on a selection, the selected rows as a roaring container of
row indexes in the chunk (`RoaringContainerView`):

| Function | Result |
| --- | --- |
| `reconstructSequences(chunk, rows, texts)` | the texts of the selected rows (reference, then stored symbols via subset ranks, then the missing symbol outside the coverage) |
| `symbolsAt(chunk, position, rows, symbols)` | the character at one position of each selected row, without reconstructing the texts |
| `storedSymbolsAt(chunk, position)` | the stored `(symbol, rows)` containers at a position, as views into the chunk |
| `rowsWithSymbolsAt(chunk, position, symbols, rows)` | the selected non-null rows whose text has one of `symbols` at the position (the reference and the missing symbol included) |
| `coveredRows(chunk, position[, rows])` | the (selected) rows covering a position |
| `addSymbolCounts(chunk, [rows,] counts)` | adds, per position and symbol, the number of (selected) rows covering the position with that symbol (mutation counting) |
| `groupRowsBySymbolAt(chunk, position, rows)` | the selected rows grouped by their symbol at a position, stored symbols as views, a uniform chunk as one symbol, the null rows apart (bitmap aggregation by `seq.at(p)`) |

The selected rows may include null rows, which cover no position (`reconstructSequences` writes
the missing symbol everywhere for them); callers that distinguish them consult
`SequenceChunk::nullRows()`.

Every algorithm treats the reference symbol of a position as the chunk's own local reference symbol
there, so the column-level operations work on chunks with different local references by calling
them chunk by chunk: the mutations operator sums `addSymbolCounts` over the chunks of the filter,
the `seq.at(p)` dimension index of the bitmap aggregation calls `groupRowsBySymbolAt`,
`IsInCoveredRegion` uses `coveredRows`, mutation profiles reconstruct single rows, and the table
scan reconstructs the texts of the scanned rows per chunk.

A symbol filter (`SymbolInSet`, which `SymbolEquals`, `HasMutation`, ambiguity handling and
mutation profiles compile to) selects the rows whose symbol at a position is in a set. If all
chunks agree on whether their local reference symbol at the position is in the set, it is compiled
as before from `storedSymbolsAt` views (`SequenceColumn::storedRowsWithSymbolsAt`), complemented or
combined with `IsInCoveredRegion` when the reference symbol is in the set. Otherwise, it is the
`HasSymbolInSet` predicate, which evaluates `rowsWithSymbolsAt` on every chunk against the chunk's
own local reference.

## Sequence Column

`SequenceColumn<SymbolType>` holds `std::vector<std::shared_ptr<const SequenceChunk>>`, one per
appended ingestion chunk and indexed by chunk id, the column's reference (the reference sequence of
the column's metadata) and the insertion index.

- **Appending** (`appendChunk`): the table chunk builder diffs every inserted sequence against the
  column's reference (`BufferedSequence`); the column turns the buffered rows into one new
  `SequenceChunk` stored against the chunk's local reference (see below). Appending never touches
  the stored chunks, and `finalize` only updates the column info.
- **Deleting rows** (`deleteRows`, see `RowCompaction`): a chunk that loses all rows is dropped, a
  chunk that loses some is rebuilt through the builders without them (`SequenceChunk::compacted`),
  and every other chunk is kept as it is (the same object). A compacted chunk keeps its local
  reference, even though another reference might suit its remaining rows better.
- **Serialization**: the distinct local references of the chunks are written once each, as their
  differences from the column's reference, then per chunk the index of its local reference, the two
  index blobs as their size followed by their raw bytes (as `Buffer`s), and the null rows. Loading
  reads each blob into a new 64-byte aligned `Buffer` and validates the chunk once
  (`SequenceChunk::validate`); nothing is rebuilt. The loaded chunks share their references as before (a reference without differences is
  the column's reference object).

### Local References

`SequenceChunkBuilder::finishWithLocalReference` builds the chunk against the column's reference,
then chooses the chunk's local reference (`mostCommonSymbols`): at every position, the symbol most
of the chunk's covered rows carry there (counted by `addSymbolCounts`). On a tie, the column's
reference symbol is kept, and among other symbols the one with the lowest id wins; a position no row
covers keeps the column's reference symbol. If any position changes, the chunk is rebased onto the
local reference (`rebasedOnto`): at the changed positions, the rows that carried the old reference
symbol (covered and storing nothing) come to store it, and the rows storing the new reference
symbol no longer do.

Choosing the reference per chunk instead of per column has two effects:

- A chunk's representation depends only on its own rows, so appending never rebases (or
  otherwise changes) the stored chunks.
- The local reference adapts to local variation: when similar sequences are ingested together
  (e.g. ordered by lineage or date), a chunk's local reference is close to its rows, and its
  vertical index can be smaller than with one reference for the whole column. Datasets that do not
  cluster variants by chunk keep about the same index size.

A `SequenceReference` is a base text (with its symbol ids) and the sorted `(position, symbol)` pairs
where the reference differs from it. `withSymbolsReplaced` derives a reference that shares the base,
so every local reference of a column shares the column's reference text and only holds its own
differences (8 bytes each); `symbolAt` looks a position up among them by binary search, and the
algorithms that visit every position materialize the symbols once (`symbols()`). Chunks whose local
reference equals the column's reference or the local reference of a stored chunk also share that
`SequenceReference` object. On real wastewater short reads (173 chunks of SARS-CoV-2 reads), the
chunks have 112 distinct local references that differ from the column's reference at 969 positions
in total, so the local references take about 18 KB beside the shared 60 KB reference, where full
copies would take 6.7 MB.

## Reading Sequences in Queries

The table scan reconstructs the texts of the scanned rows chunk by chunk: the non-null rows of each
chunk are passed to `reconstructSequences` as one container, and each text is compressed with zstd
against the column's reference sequence, whatever the chunk's local reference (the null rows are
output as nulls).
