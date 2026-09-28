# RhyDB Input Format Documentation

This document describes the NDJSON input format used by RhyDB for ingesting genomic sequence data and associated metadata.

## Overview

RhyDB expects input data in **NDJSON format** (Newline Delimited JSON), where each line contains a complete JSON object representing one record/sequence. The format is parsed using the simdjson library for high-performance streaming.

## File Structure

- **Format**: NDJSON (one JSON object per line)
- **Line Delimiters**: Standard newline characters (`\n`)
- **Empty Lines**: Automatically skipped
- **Trailing Newline**: Optional (files without trailing newlines are supported)

### Example

```json
{"primaryKey":"seq_001","date":"2021-03-18","country":"Switzerland","age":54,"main":{"sequence":"ACGT","insertions":[]}}
{"primaryKey":"seq_002","date":"2021-04-13","country":"Germany","age":null,"main":{"sequence":"AAGN","insertions":["2:AC"]}}
{"primaryKey":"seq_003","date":"2021-05-01","country":"France","age":30,"main":{"sequenceCompressed":"<base64-zstd>","insertions":[]}}
```

## Configuration Files

### database_config.yaml

Defines the schema for your RhyDB database:

```yaml
schema:
  instanceName: my_database
  metadata:
    - name: primaryKey
      type: string
    - name: date
      type: date
    - name: country
      type: string
      generateIndex: true
    - name: pango_lineage
      type: string
      generateLineageIndex: pango.yaml
    - name: age
      type: int
    - name: qc_value
      type: float
    - name: is_complete
      type: boolean
  primaryKey: primaryKey
```

**Metadata Field Options**:
- `name`: Field name (must match JSON key)
- `type`: One of `string`, `int` (alias `int32`, 32-bit integer), `int64` (64-bit integer), `float`, `date`, `boolean`
- `generateIndex`: Set to `true` to create a bitmap index for fast equality lookups. This is only valid for `string` columns
- `generateLineageIndex`: Path to lineage definition file for hierarchical queries. This is only possible if `generateIndex` is also set
- `lineageIndexType`: How the lineage definition is made available for querying. One of:
  - `columnMetadata` (default) — the lineage tree is attached to the column itself, which is what the `lineage(...)` filter uses
  - `table` — preprocessing materializes a separate relation table holding the lineage edges, and the column carries no lineage tree (so `lineage(...)` is *not* available on it)
  - `both` — both of the above

  Only valid together with `generateLineageIndex`; setting it on a column without a lineage definition is a config error. See [lineage_definitions.md](lineage_definitions.md) for the relation table's schema.
- `treatUnknownLineagesAsNull`: Treats unknown lineage values as null when adding them to the lineage index
- `isPhyloTreeField`: Mark this column as a phyloTreeField, which enables the phylogenetic queries. See [phylogenetic_queries.md](phylogenetic_queries.md)

**Schema Options**:
- `instanceName`: Name of the database instance
- `metadata`: The list of metadata fields described above
- `primaryKey`: Selects the metadata field that uniquely identifies a record. Optional — omit it to declare no primary key

### reference_genomes.json

Defines reference sequences for alignment:

```json
{
  "nucleotideSequences": [
    {
      "name": "main",
      "sequence": "ATTAAAGGTTTATACCTTCCCAG..."
    }
  ],
  "genes": [
    {
      "name": "E",
      "sequence": "MYSFVSEETGTLIVNSVLLFLAFVVFLLVTLAILTA..."
    }
  ]
}
```

Preprocessing also stores these sequences in the built-in table `reference_genomes`, which is
queried like any other table. Being built-in, it is present in every database (empty if the
database was not initialized with reference genomes):

```
reference_genomes
  .filter(type = 'nucleotide')
```

| Column | Type | Description |
|--------|------|-------------|
| `name` | string | The sequence name. A nucleotide and an amino acid sequence may share a name, so only `name` and `type` together identify a row; the table therefore has no primary key |
| `type` | string | `nucleotide` or `amino_acid` |
| `sequence` | string | The reference sequence |

## Column Types and Value Semantics

Each field in the NDJSON must correspond to a metadata field defined in `database_config.yaml`. 

Currently, but subject to change in the future, the sequence column types are inferred from a `reference_genomes.json`:
- SequenceColumn<Nucleotide> for each entry in `nucleotideSequences`
- SequenceColumn<AminoAcid> for each entry in `aminoAcidSequences`
- ZstdCompressedStringColumn for each entry in `nucleotideSequences`, prefixed with 'unaligned_'

The following column types are supported:

### STRING

String values for text data.

| Property | Value |
|----------|-------|
| JSON Type | String |
| Null | `null` |

```json
{"country": "Switzerland"}
{"country": null}
```
### ZSTD_COMPRESSED_STRING

String values for textual data which is stored compressed by a common dictionary.

| Property | Value |
|----------|-------|
| JSON Type | String |
| Null | `null` |

```json
{"unaligned_main": "ACGTACGTACGTACGT"}
{"unaligned_main": null}
```

### INT32

32-bit signed integers. Configured as `int32` (or the alias `int`).

| Property | Value |
|----------|-------|
| JSON Type | Integer |
| Null | `null` |
| Range | -2,147,483,648 to 2,147,483,647 |

```json
{"age": 54}
{"age": null}
```

### INT64

64-bit signed integers. Configured as `int64`. Use this when values may exceed the 32-bit `int` range.

| Property | Value |
|----------|-------|
| JSON Type | Integer |
| Null | `null` |
| Range | -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807 |

```json
{"count": 5000000000}
{"count": null}
```

### FLOAT

64-bit floating-point numbers.

| Property | Value |
|----------|-------|
| JSON Type | Number (integer or decimal) |
| Null | `null` |

```json
{"qc_value": 0.98}
{"qc_value": null}
```

### DATE

Date values strictly in ISO-8601 format.

| Property | Value |
|----------|-------|
| JSON Type | String |
| Format | `YYYY-MM-DD` |
| Null | `null` |

```json
{"date": "2021-03-18"}
{"date": null}
{"date": "2021-02-28"}
``` 

### BOOLEAN

Boolean true/false values.

| Property | Value |
|----------|-------|
| JSON Type | Boolean |
| Null | `null` |

```json
{"is_complete": true}
{"is_complete": false}
{"is_complete": null}
```

### NUCLEOTIDE_SEQUENCE

DNA/RNA sequence data with optional insertions.

| Property | Value |
|----------|-------|
| JSON Type | Object |
| Null | `null` |

**Structure**:
```json
{
  "main": {
    "sequence": "ACGTACGT",
    "insertions": ["214:EPE", "185:AC"],
    "offset": 0
  }
}
```

**Fields**:
- `sequence`: The nucleotide sequence string
- `sequenceCompressed` (alternative to `sequence`): A base64-encoded, ZSTD-compressed nucleotide sequence. The ZSTD compression must use the column's reference genome string as a dictionary. When present, `sequenceCompressed` takes precedence over `sequence`.
- `insertions`: Array of insertions in format `position:sequence`. `sequence` is inserted _after_ `position`. `position` 0 inserts _before the first symbol_. Valid positions are therefore in the range `[0, n]` where `n` is the length of the reference sequence.
- `offset` (optional): Integer offset into reference genome (default: 0)

**Valid Nucleotide Characters**:

| Character | Meaning |
|-----------|---------|
| `A` | Adenine |
| `C` | Cytosine |
| `G` | Guanine |
| `T` | Thymine |
| `N` | Unknown/ambiguous |
| `-` | Gap (deletion) |

**Insertion Format**:
- Format: `position:insertion_sequence`
- Example: `"214:ACGT"` means insertion of "ACGT" after position 214
- Position is 1-indexed
- Invalid insertions cause parsing errors

**Unaligned Sequences**:
Raw unaligned sequences can be provided with an `unaligned_` prefix:
```json
{
  "main": {"sequence": "ACGT", "insertions": []},
  "unaligned_main": "ACGTACGT"
}
```

### AMINO_ACID_SEQUENCE

Protein sequence data with optional insertions.

| Property | Value |
|----------|-------|
| JSON Type | Object |
| Null | `null` |

**Structure** (identical to nucleotide):
```json
{
  "E": {
    "sequence": "MYSF*",
    "insertions": ["214:EPE"],
    "offset": 0
  }
}
```

**Fields**:
- `sequence`: The amino acid sequence string
- `sequenceCompressed` (alternative to `sequence`): A base64-encoded, ZSTD-compressed amino acid sequence. The ZSTD compression must use the column's reference sequence string as a dictionary. When present, `sequenceCompressed` takes precedence over `sequence`.
- `insertions`: Array of insertions in format `position:sequence`
- `offset` (optional): Integer offset into reference sequence (default: 0)

**Valid Amino Acid Characters**:

| Character | Meaning |
|-----------|---------|
| `A-Z` | Standard amino acids (21 total) |
| `*` | Stop codon |
| `X` | Unknown/ambiguous |
| `-` | Gap (deletion) |

## Complete Example

### database_config.yaml

```yaml
schema:
  instanceName: example_database
  metadata:
    - name: accession
      type: string
    - name: date
      type: date
    - name: country
      type: string
      generateIndex: true
    - name: age
      type: int
    - name: qc_score
      type: float
  primaryKey: accession
```

### Input NDJSON

```json
{"accession":"SEQ001","date":"2021-03-18","country":"Switzerland","age":54,"qc_score":0.98,"main":{"sequence":"ACGTACGT","insertions":[]}}
{"accession":"SEQ002","date":"2021-04-13","country":"Germany","age":null,"qc_score":0.95,"main":{"sequence":"AAGNAAGN","insertions":["4:CC"]}}
{"accession":"SEQ003","date":null,"country":"France","age":32,"qc_score":null,"main":{"sequence":"ACGTNNNN","insertions":[]}}
```

## Error Handling

| Error | Cause | Behavior |
|-------|-------|----------|
| Missing required field | Field in schema not present in JSON | Error with field name |
| Type mismatch | JSON value type doesn't match schema | Error with details |
| Invalid date format | Date string not in YYYY-MM-DD format | Error with details |
| Invalid sequence character | Unknown nucleotide/amino acid | Error with position |
| Invalid insertion format | Malformed insertion string | Error with details |
| Invalid base64 in `sequenceCompressed` | Illegal characters or wrong padding | Error with details |
| Invalid ZSTD data in `sequenceCompressed` | Wrong dictionary or corrupt bytes | Error with details |
| Duplicate primary key | Same primary key appears twice | Error at validation. Only checked when `primaryKey` is declared; skipped when it is null |
| Unknown field | Field in JSON not in schema | Warning, ignored |
