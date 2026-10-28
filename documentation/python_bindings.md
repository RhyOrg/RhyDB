# Python Bindings (`rhydb`)

`rhydb` is a Python package that embeds RhyDB in-process, exposing the `Database` class. It lets you build or load a RhyDB database, run [SaneQL](query_documentation.md) queries, update column values, and persist state — all without running the HTTP API server.

Queries return [Apache Arrow](https://arrow.apache.org/) tables (`pyarrow`), and filter results are returned as [Roaring bitmaps](https://roaringbitmap.org/) (`pyroaring`), so results integrate directly with the scientific Python stack.

## Installation

The package is built from this repository with a C++ toolchain (the same one used to build RhyDB) and Cython. From the repository root:

```bash
make python-tests   # builds rhydb into a virtualenv and runs the test suite
```

or build a wheel directly:

```bash
uv build --wheel
```

The bindings depend on `pyarrow` and `pyroaring`, which are installed automatically as dependencies.

## Quick Start

```python
from rhydb import Database

# Load a preprocessed database state from a data directory
db = Database("path/to/data-directory")

# Run a SaneQL query; results come back as a pyarrow.Table
table = db.query("data.filter(region = 'Europe').project({primaryKey, age})")
print(table.to_pydict())

# Overwrite a column for the matching rows
db.query("update(data, set := {age := 0}, where := age = 4)")
```

## The `Database` Class

`from rhydb import Database`

### Constructing and loading

**`Database(file_name=None)`**
Creates an empty database, or loads an existing one when `file_name` is given.

`file_name` is a **data directory**: the directory that holds one or more versioned database states (see [Data Directories](data_directories.md)). The most recent compatible state is loaded automatically.

```python
empty = Database()                       # in-memory, no tables
loaded = Database("path/to/rhydb-dir")    # loads the most recent state
```

Raises `FileNotFoundError` if the path does not exist, and `RuntimeError` if no valid state can be loaded (for example, an incompatible serialization version).

`Database` is also a context manager:

```python
with Database("path/to/rhydb-dir") as db:
    result = db.query("data")
```

### Preprocessing

**`preprocess(database_config, reference_genomes, input_file, lineage_definitions=(), phylogenetic_tree=None, without_unaligned_sequences=False)`**
Does what the `preprocessing` command does, but into this in-memory database instead of a saved
state. It takes the same initialization files as the command, given as paths (`str` or
`os.PathLike`). It creates the default table `data` from the database config and the reference
genomes, and fills the built-in `reference_genomes` and `lineage_definitions` tables. It then builds
the lineage relation tables, appends the NDJSON input and checks that the primary key is unique. A
column's `generateLineageIndex` names its lineage definition file, with or without the `.yaml`
extension.

```python
db = Database()
db.preprocess(
    database_config="database_config.yaml",
    reference_genomes="reference_genomes.json",
    input_file="input.ndjson",
    lineage_definitions=["lineage_definition.yaml"],
)
db.save_checkpoint("path/to/rhydb-dir")
```

The built-in tables must still be empty, so `preprocess` only runs once on a fresh database. Raises
`FileNotFoundError` for a missing file and `RuntimeError` for invalid input. Invalid input may leave
the database partially filled.

### Building a database in memory

Tables are created with the SaneQL [`createTable`](query_documentation.md#createtabletable-symbol-columns-record-primarykey-symbol)
statement, run through `query()` like any other query. All column types of `createTable` are
available. Sequence columns take their reference from the built-in `reference_genomes` table, so
append the reference there first:

```python
db = Database()
db.append_data_from_string(
    "reference_genomes",
    '{"name": "main", "type": "nucleotide", "sequence": "ACGT..."}',
)
db.query("""
createTable(sequences, {
   primary_key := string,
   main := nucleotideSequence(reference := main),
   country := string(generateIndex := true),
   age := int32
}, primaryKey := primary_key)
""")
```

Data is then appended in [NDJSON format](input_format.md):

**`append_data_from_file(table_name, file_name)`** — append records from an NDJSON file.
**`append_data_from_string(table_name, json_string)`** — append records from an NDJSON string.

```python
db.append_data_from_file("sequences", "input.ndjson")
db.append_data_from_string(
    "sequences",
    '{"primary_key": "s1", "main": {"sequence": "ACGT", "insertions": []}, "country": "CH"}',
)
```

### Querying

**`query(query_string)`** → `pyarrow.Table`
Executes a [SaneQL](query_documentation.md) query. The leading identifier is the table name.
Write statements (`createTable(...)`, `<query>.insertInto(<table>)`, `update(...)`, `delete(...)`) are applied to
the in-memory database in place and return a single-row summary, e.g. `{createdTable: "sequences"}`.

```python
result = db.query("data.filter(age >= 18).project({primaryKey, age, country})")
data = result.to_pydict()      # dict of columns
df = result.to_pandas()        # pandas DataFrame
```

**`get_filtered_bitmap(table_name, filter_expression="")`** → `pyroaring.BitMap`
Returns the row indices matching a SaneQL filter expression. An empty or `None` filter defaults to `true` (all rows). Useful for cheap counting and set operations.

```python
matching = db.get_filtered_bitmap("data", "region = 'Europe'")
print(len(matching))                       # number of matching rows
```

### Inspecting the database

**`get_nucleotide_reference_sequence(table_name, sequence_name)`** → `str`.
**`get_amino_acid_reference_sequence(table_name, sequence_name)`** → `str`.
**`print_all_data(table_name)`** — prints all rows of a table to stdout (debugging aid).

### Updating columns

Columns are updated with the SaneQL
[`update`](query_documentation.md#updatetable-symbol-set-record-where-expression) statement, run
through `query()` like any other query. It returns the number of updated rows, e.g.
`{updatedRows: 2}`. See its documentation for which values can be assigned to which column types.

```python
db = Database("path/to/rhydb-dir")

# Set every row's age to 0
db.query("update(data, set := {age := 0})")

# Set age to 100 only for rows currently equal to 4
db.query("update(data, set := {age := 100}, where := age = 4)")

# Assign several columns at once, computing a value from the row
db.query("update(data, set := {date := '2000-01-01'::date, region := at(country, 1)})")

# Clear a boolean column to null for a subset of rows
db.query("update(data, set := {test_boolean_column := null}, where := region = 'Asia')")
```

The update mutates only the **in-memory** database. On-disk state is left untouched until you call `save_checkpoint`. Because the update bumps the data version, a subsequently saved checkpoint is written as a new versioned state.

**Not thread-safe.** `update` mutates the in-memory database in place with no internal locking.

### Deleting rows

Rows are deleted with the SaneQL
[`delete`](query_documentation.md#deletetable-symbol-where-expression) statement, run through
`query()`. It returns the number of deleted rows, e.g. `{deletedRows: 2}`. Like `update`, it only
changes the in-memory database until `save_checkpoint` is called, and it is not thread-safe.

```python
# Delete the rows currently equal to 4
db.query("delete(data, where := age = 4)")

# Delete every row
db.query("delete(data)")
```

### Persisting state

**`save_checkpoint(save_directory)`**
Writes the current database state into `save_directory` as a new versioned state, which can later be reloaded with `Database(save_directory)`.

```python
db.query("update(data, set := {age := 0})")
db.save_checkpoint("out/rhydb-dir")
reloaded = Database("out/rhydb-dir")
```

## Error Handling

The bindings translate C++ exceptions into Python ones:

- **`ValueError`** — invalid query or update: an unknown column, an unsupported column type, a literal that does not match the column type, or a malformed SaneQL expression. Empty required arguments (such as `table_name`) also raise `ValueError`.
- **`FileNotFoundError`** — a path passed to the loader or `append_data_from_file` does not exist.
- **`RuntimeError`** — other failures (for example, a failed load or save).

```python
try:
    db.query("update(data, set := {does_not_exist := 'B.1'})")
except ValueError as e:
    print(e)   # the column does not exist
```

## Complete Example

```python
from rhydb import Database

with Database("path/to/rhydb-dir") as db:
    # Count the rows we are about to change
    before = len(db.get_filtered_bitmap("data", "age = 4"))

    # Reset those rows' age to null
    db.query("update(data, set := {age := null}, where := age = 4)")

    # Verify via a query
    remaining = len(db.get_filtered_bitmap("data", "age = 4"))
    cleared = len(db.get_filtered_bitmap("data", "age.isNull()"))
    assert remaining == 0
    assert cleared >= before

    # Persist the edited database as a new state
    db.save_checkpoint("out/rhydb-dir")
```
