# Sequence Storage

This document explains how sequences are stored in RhyDB.

## Overview

A sequence column (`storage::column::SequenceColumn<SymbolType>`, with `SymbolType` `Nucleotide` or
`AminoAcid`) stores its rows in **sequence chunks**: one immutable `SequenceChunk` per ingestion
chunk of up to 2^16 rows (see `RowId`: the global row id of row `r` of chunk `k` is `k << 16 | r`).
The chunks, their indexes and the algorithms on them live in `storage/sequence/` (namespace
`storage::sequence`).
Every chunk stores its rows as differences from a **local reference**:

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
├── shared_ptr<const SequenceReference>   alphabet + local reference text, shared by all chunks
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
  unchanged buffers (`withMutations`, `rebasedOnto`, `compacted`).
- The vertical index never stores a row under the reference symbol of its position, and only stores
  non-empty containers. A stored symbol may be the missing symbol (after reference adaptation, when
  the old reference symbol was the missing symbol).
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

The column-level operations call them chunk by chunk: the mutations operator sums `addSymbolCounts`
over the chunks of the filter, the `seq.at(p)` dimension index of the bitmap aggregation calls
`groupRowsBySymbolAt`, `IsInCoveredRegion` uses `coveredRows`, symbol filters assemble their row
sets from `storedSymbolsAt` views (`SequenceColumn::storedRowsWithSymbolsAt`), mutation profiles
reconstruct single rows, and the table scan reconstructs the texts of the scanned rows per chunk.

## Sequence Column

`SequenceColumn<SymbolType>` holds `std::vector<std::shared_ptr<const SequenceChunk>>`, one per
appended ingestion chunk and indexed by chunk id, plus the local reference they are all stored
against and the insertion index.

- **Appending** (`appendChunk`): the table chunk builder diffs every inserted sequence against the
  column's current local reference (`BufferedSequence`); the column turns the buffered rows into
  one new `SequenceChunk`.
- **Reference adaptation** (`finalize`): for every position, the symbol carried by most covered rows
  (counted from the coverage cardinalities and the stored containers' cardinalities) becomes the
  local reference symbol, ties keeping the current one. If any position changes, the column creates
  the adapted `SequenceReference` and replaces every chunk by `chunk.rebasedOnto(reference)`: at the
  changed positions, the rows that carried the old reference symbol (covered and storing nothing)
  come to store it, and the rows storing the new reference symbol no longer do. Chunks keep their
  coverage and null rows buffers, and their vertical index buffer if none of their positions change.
- **Deleting rows** (`deleteRows`, see `RowCompaction`): a chunk that loses all rows is dropped, a
  chunk that loses some is rebuilt through the builders without them (`SequenceChunk::compacted`),
  and every other chunk is kept as it is (the same object).
- **Serialization**: the local reference text is written once, then per chunk the two index blobs as
  their size followed by their raw bytes (as `Buffer`s), and the null rows. Loading reads each blob
  into a new 64-byte aligned `Buffer` and validates the chunk once (`SequenceChunk::validate`);
  nothing is rebuilt. The
  loaded chunks share the loaded local reference.

## Reading Sequences in Queries

The table scan reconstructs the texts of the scanned rows chunk by chunk: the non-null rows of each
chunk are passed to `reconstructSequences` as one container, and each text is compressed with zstd
against the reference sequence (the null rows are output as nulls).
