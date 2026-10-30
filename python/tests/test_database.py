import pytest
import os
import json
import datetime
import tempfile
import shutil
import pyroaring
import pyarrow as pa


# Path to test data
TEST_DATA_DIR = os.path.join(os.path.dirname(__file__), '..', '..', 'testBaseData', 'exampleDataset')
REFERENCE_GENOMES_FILE = os.path.join(TEST_DATA_DIR, 'reference_genomes.json')
INPUT_FILE = os.path.join(TEST_DATA_DIR, 'input_file.ndjson')
DATABASE_CONFIG_FILE = os.path.join(TEST_DATA_DIR, 'database_config.yaml')
LINEAGE_DEFINITION_FILE = os.path.join(TEST_DATA_DIR, 'lineage_definition.yaml')
PHYLOGENETIC_TREE_FILE = os.path.join(TEST_DATA_DIR, 'phylogenetic_tree.nwk')

# A serialized database (checked into the repo) whose schema has scalar value columns
# (age:int, qc_value:float, date:date, test_boolean_column:bool), filled with real data.
SERIALIZED_STATE_DIR = os.path.join(
    os.path.dirname(__file__), '..', '..', 'testBaseData', 'rhydbSerializedState'
)


@pytest.fixture
def reference_genomes():
    """Load reference genomes from test data."""
    with open(REFERENCE_GENOMES_FILE, 'r') as f:
        return json.load(f)


@pytest.fixture
def main_reference_sequence(reference_genomes):
    """Get the main nucleotide reference sequence."""
    for seq in reference_genomes['nucleotideSequences']:
        if seq['name'] == 'main':
            return seq['sequence']
    raise ValueError("No 'main' sequence found in reference genomes")


def add_reference(database, name, sequence, sequence_type="nucleotide"):
    """Add a reference sequence to the built-in reference_genomes table."""
    database.append_data_from_string(
        "reference_genomes",
        json.dumps({"name": name, "type": sequence_type, "sequence": sequence}),
    )


def create_sequence_table(
    database,
    table_name,
    primary_key_name,
    sequence_name,
    reference_sequence,
    extra_columns=(),
    sequence_type="nucleotide",
):
    """Create a table with a string primary key, one sequence column and extra string columns.

    The sequence column takes its reference from a reference_genomes row named after the column.
    """
    add_reference(database, sequence_name, reference_sequence, sequence_type)
    sequence_column_type = (
        "nucleotideSequence" if sequence_type == "nucleotide" else "aminoAcidSequence"
    )
    columns = [
        f"{primary_key_name} := string",
        f"{sequence_name} := {sequence_column_type}(reference := {sequence_name})",
    ] + [f"{column} := string" for column in extra_columns]
    return database.query(
        f"createTable({table_name}, {{{', '.join(columns)}}}, primaryKey := {primary_key_name})"
    )


class TestDatabaseImport:
    """Test that the rhydb module can be imported correctly."""

    def test_import_database(self):
        """Test that Database can be imported from rhydb."""
        from rhydb import Database
        assert Database is not None

    def test_import_pydatabase(self):
        """Test that PyDatabase can be imported from rhydb.database."""
        from rhydb.database import PyDatabase
        assert PyDatabase is not None


class TestDatabaseCreation:
    """Test database creation and basic operations."""

    def test_create_empty_database(self):
        """Test creating an empty database."""
        from rhydb import Database
        db = Database()
        assert db is not None

    def test_database_has_expected_methods(self, empty_database):
        """Test that the database has all expected methods."""
        expected_methods = [
            'preprocess',
            'append_data_from_file',
            'append_data_from_string',
            'query',
            'get_filtered_bitmap',
            'get_nucleotide_reference_sequence',
            'get_amino_acid_reference_sequence',
            'print_all_data',
            'save_checkpoint',
        ]
        for method in expected_methods:
            assert hasattr(empty_database, method), f"Missing method: {method}"

    def test_database_repr(self, empty_database):
        """Test database string representation."""
        assert repr(empty_database) == "Database()"

    def test_database_context_manager(self):
        """Test that database can be used as a context manager."""
        from rhydb import Database
        with Database() as db:
            assert db is not None


def preprocess_example_dataset(database):
    """Preprocess the example dataset into the default table `data` of `database`."""
    database.preprocess(
        database_config=DATABASE_CONFIG_FILE,
        reference_genomes=REFERENCE_GENOMES_FILE,
        input_file=INPUT_FILE,
        lineage_definitions=[LINEAGE_DEFINITION_FILE],
        phylogenetic_tree=PHYLOGENETIC_TREE_FILE,
    )


class TestPreprocess:
    """Test preprocessing initialization files and NDJSON data into the default table."""

    def test_preprocess_fills_default_table(self, empty_database):
        preprocess_example_dataset(empty_database)
        assert len(empty_database.get_filtered_bitmap("data")) == 100

    def test_preprocess_fills_reference_genomes(self, empty_database, main_reference_sequence):
        preprocess_example_dataset(empty_database)
        assert empty_database.get_nucleotide_reference_sequence("data", "main") == main_reference_sequence
        result = empty_database.query(
            "reference_genomes.filter(name = 'main' && type = 'nucleotide').project({sequence})"
        )
        assert result.to_pydict() == {"sequence": [main_reference_sequence]}

    def test_preprocess_supports_lineage_queries(self, empty_database):
        preprocess_example_dataset(empty_database)
        assert empty_database.get_filtered_bitmap("data", "lineage(pango_lineage, 'B.1.1.7')")

    def test_preprocess_accepts_path_objects(self, empty_database):
        from pathlib import Path
        empty_database.preprocess(
            database_config=Path(DATABASE_CONFIG_FILE),
            reference_genomes=Path(REFERENCE_GENOMES_FILE),
            input_file=Path(INPUT_FILE),
            lineage_definitions=[Path(LINEAGE_DEFINITION_FILE)],
        )
        assert len(empty_database.get_filtered_bitmap("data")) == 100

    def test_preprocessed_database_survives_checkpoint(self, empty_database, temp_dir):
        from rhydb import Database
        preprocess_example_dataset(empty_database)
        save_path = os.path.join(temp_dir, "checkpoint")
        empty_database.save_checkpoint(save_path)

        loaded = Database(save_path)
        assert len(loaded.get_filtered_bitmap("data")) == 100

    def test_preprocess_twice_raises(self, empty_database):
        preprocess_example_dataset(empty_database)
        with pytest.raises(RuntimeError, match="already exists"):
            preprocess_example_dataset(empty_database)

    def test_preprocess_missing_lineage_definition_raises(self, empty_database):
        with pytest.raises(RuntimeError, match="lineage"):
            empty_database.preprocess(
                database_config=DATABASE_CONFIG_FILE,
                reference_genomes=REFERENCE_GENOMES_FILE,
                input_file=INPUT_FILE,
            )

    def test_preprocess_duplicate_primary_key_raises(self, empty_database, temp_dir):
        input_file = os.path.join(temp_dir, "input.ndjson")
        with open(INPUT_FILE) as source, open(input_file, "w") as target:
            first_line = source.readline()
            target.write(first_line)
            target.write(first_line)
        with pytest.raises(RuntimeError, match="primary key"):
            empty_database.preprocess(
                database_config=DATABASE_CONFIG_FILE,
                reference_genomes=REFERENCE_GENOMES_FILE,
                input_file=input_file,
                lineage_definitions=[LINEAGE_DEFINITION_FILE],
            )

    def test_preprocess_nonexistent_file_raises(self, empty_database):
        with pytest.raises(FileNotFoundError):
            empty_database.preprocess(
                database_config=DATABASE_CONFIG_FILE,
                reference_genomes=REFERENCE_GENOMES_FILE,
                input_file="/nonexistent/input.ndjson",
            )

    def test_preprocess_empty_path_raises(self, empty_database):
        with pytest.raises(ValueError):
            empty_database.preprocess(
                database_config="",
                reference_genomes=REFERENCE_GENOMES_FILE,
                input_file=INPUT_FILE,
            )


class TestCreateTableQuery:
    """Test creating tables with the SaneQL createTable statement through query()."""

    def test_create_table_returns_summary(self, empty_database):
        """createTable returns a single-row summary naming the created table."""
        result = empty_database.query("createTable(metadata, {id := string}, primaryKey := id)")
        assert isinstance(result, pa.Table)
        assert result.to_pydict() == {"createdTable": ["metadata"]}

    def test_created_table_is_queryable(self, empty_database):
        """The created table is empty and has the declared columns."""
        empty_database.query(
            "createTable(metadata, {id := string, age := int, qc := float, date := date, "
            "flag := boolean, country := string(generateIndex := true)}, primaryKey := id)"
        )
        result = empty_database.query("metadata")
        assert result.num_rows == 0
        assert set(result.column_names) == {"id", "age", "qc", "date", "flag", "country"}

    def test_create_table_with_scalar_columns_accepts_data(self, empty_database):
        """Scalar value columns created through createTable store appended data."""
        empty_database.query(
            "createTable(metadata, {id := string, age := int, country := string(generateIndex := true)}, "
            "primaryKey := id)"
        )
        empty_database.append_data_from_string(
            "metadata", '{"id": "s1", "age": 42, "country": "CH"}'
        )
        empty_database.append_data_from_string(
            "metadata", '{"id": "s2", "age": 7, "country": "DE"}'
        )
        result = empty_database.query("metadata.filter(age > 10)")
        assert result.to_pydict()["id"] == ["s1"]

    def test_create_table_with_sequence_column(self, empty_database):
        """Sequence columns take their reference from the reference_genomes table."""
        add_reference(empty_database, "main", "ACGT")
        empty_database.query(
            "createTable(sequences, {id := string, main := nucleotideSequence(reference := main)}, "
            "primaryKey := id)"
        )
        assert empty_database.get_nucleotide_reference_sequence("sequences", "main") == "ACGT"

    def test_create_table_without_reference_raises(self, empty_database):
        """Creating a sequence column without a matching reference fails."""
        with pytest.raises(ValueError, match="requires a reference"):
            empty_database.query(
                "createTable(sequences, {id := string, main := nucleotideSequence(reference := main)}, "
                "primaryKey := id)"
            )

    def test_create_existing_table_raises(self, empty_database):
        """Creating a table whose name is taken fails."""
        empty_database.query("createTable(metadata, {id := string}, primaryKey := id)")
        with pytest.raises(ValueError, match="already exists"):
            empty_database.query("createTable(metadata, {id := string}, primaryKey := id)")

    def test_insert_into_created_table(self, empty_database):
        """insertInto, the other write statement, is also applied through query()."""
        empty_database.query("createTable(source, {id := string, age := int}, primaryKey := id)")
        empty_database.query("createTable(archive, {id := string, age := int}, primaryKey := id)")
        empty_database.append_data_from_string("source", '{"id": "s1", "age": 42}')
        empty_database.append_data_from_string("source", '{"id": "s2", "age": 7}')

        result = empty_database.query("source.filter(age > 10).insertInto(archive)")

        assert result.to_pydict() == {"insertedRows": [1]}
        assert empty_database.query("archive").to_pydict() == {"id": ["s1"], "age": [42]}


class TestCreateNucleotideSequenceTable:
    """Test creating nucleotide sequence tables."""

    def test_create_table_with_simple_reference(self, empty_database):
        """Test creating a table with a simple reference sequence."""
        # Table names must be lowercase
        create_sequence_table(
            empty_database,
            table_name="testtable",
            primary_key_name="id",
            sequence_name="main",
            reference_sequence="ACGTACGTACGT"
        )
        # If no exception, table was created

    def test_create_table_with_real_reference(self, empty_database, main_reference_sequence):
        """Test creating a table with the real SARS-CoV-2 reference sequence."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="gisaidepisl",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        # If no exception, table was created

    def test_get_reference_sequence_after_create(self, empty_database, main_reference_sequence):
        """Test that we can retrieve the reference sequence after creating a table."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )

        retrieved = empty_database.get_nucleotide_reference_sequence("sequences", "main")
        assert retrieved == main_reference_sequence

    def test_get_gene_reference_sequence_after_create(self, empty_database):
        """Test that we can retrieve the reference sequence after creating a table."""
        create_sequence_table(
            empty_database,
            sequence_type="amino_acid",
            table_name="sequences",
            primary_key_name="key",
            sequence_name="main",
            reference_sequence="ABCD"
        )

        retrieved = empty_database.get_amino_acid_reference_sequence("sequences", "main")
        assert retrieved == "ABCD"


class TestCreateGeneTable:
    """Test creating gene tables."""

    def test_create_gene_table(self, empty_database):
        """Test creating a gene table."""
        # Table names must be lowercase
        create_sequence_table(
            empty_database,
            sequence_type="amino_acid",
            table_name="genes",
            primary_key_name="id",
            sequence_name="S",
            reference_sequence="MFVFLVLLPLVSSQCVNLTTRTQLPPAYTNSFTRGVYYPDKVFRSSVLHSTQDLFLPFFSNVTWFHAI*"
        )
        # If no exception, table was created


class TestAppendData:
    """Test appending data to tables."""

    def test_append_data_from_real_file(self, empty_database, main_reference_sequence):
        """Test appending data from the real test data file."""
        # First create the table
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )

        # Then append data
        empty_database.append_data_from_file("sequences", INPUT_FILE)
        # If no exception, data was appended


class TestGetFilteredBitmap:
    """Test getting filtered bitmaps."""

    def test_get_filtered_bitmap_true_filter(self, empty_database, main_reference_sequence):
        """Test getting a bitmap with True filter (returns all rows)."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        # True filter should return all rows
        bitmap = empty_database.get_filtered_bitmap("sequences", 'true')
        assert isinstance(bitmap, pyroaring.BitMap)
        assert len(bitmap) > 0  # Should have at least one row from test data

    def test_get_filtered_bitmap_returns_bitmap(self, empty_database, main_reference_sequence):
        """Test that get_filtered_bitmap returns a pyroaring.BitMap."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        bitmap = empty_database.get_filtered_bitmap("sequences", 'true')
        assert isinstance(bitmap, pyroaring.BitMap)
        # Can iterate over bitmap to get indices
        indices = list(bitmap)
        for idx in indices:
            assert isinstance(idx, int)

    def test_get_filtered_bitmap_with_none_filter(self, empty_database, main_reference_sequence):
        """Test that None filter defaults to True filter."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        bitmap = empty_database.get_filtered_bitmap("sequences", None)
        assert isinstance(bitmap, pyroaring.BitMap)

    def test_get_filtered_bitmap_with_empty_filter(self, empty_database, main_reference_sequence):
        """Test that empty string filter defaults to True filter."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        bitmap = empty_database.get_filtered_bitmap("sequences", "")
        assert isinstance(bitmap, pyroaring.BitMap)
        assert len(bitmap) > 0

    def test_get_filtered_bitmap_supports_set_operations(self, empty_database, main_reference_sequence):
        """Test that returned bitmap supports set operations."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        bitmap = empty_database.get_filtered_bitmap("sequences", 'true')

        # Test union, intersection operations
        other_bitmap = pyroaring.BitMap([0, 1, 2])
        union = bitmap | other_bitmap
        intersection = bitmap & other_bitmap

        assert isinstance(union, pyroaring.BitMap)
        assert isinstance(intersection, pyroaring.BitMap)

class TestSaveAndLoadCheckpoint:
    """Test saving and loading database checkpoints."""

    def test_save_checkpoint(self, empty_database, main_reference_sequence, temp_dir):
        """Test saving a database checkpoint."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        save_path = os.path.join(temp_dir, "checkpoint")
        empty_database.save_checkpoint(save_path)

        # Check that something was saved
        assert os.path.exists(save_path)

    def test_load_from_checkpoint(self, empty_database, main_reference_sequence, temp_dir):
        """Test loading a database from a checkpoint."""
        from rhydb import Database

        # Create and save
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        save_path = os.path.join(temp_dir, "checkpoint")
        empty_database.save_checkpoint(save_path)

        # Load from checkpoint
        loaded_db = Database(save_path)
        assert loaded_db is not None

        # Verify data is preserved
        ref_seq = loaded_db.get_nucleotide_reference_sequence("sequences", "main")
        assert ref_seq == main_reference_sequence

    def test_checkpoint_preserves_data(self, empty_database, main_reference_sequence, temp_dir):
        """Test that checkpoint preserves all data correctly."""
        from rhydb import Database

        # Create, add data, and save
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        # Get data before save
        bitmap_before = empty_database.get_filtered_bitmap("sequences", 'true')

        # Save and reload
        save_path = os.path.join(temp_dir, "checkpoint")
        empty_database.save_checkpoint(save_path)

        loaded_db = Database(save_path)

        # Compare with loaded data
        bitmap_after = loaded_db.get_filtered_bitmap("sequences", 'true')

        assert bitmap_before == bitmap_after


class TestDatabaseValidation:
    """Test input validation for database methods."""

    def test_append_data_empty_table_name(self, empty_database):
        """Test that empty table name raises ValueError."""
        with pytest.raises(ValueError, match="table_name cannot be empty"):
            empty_database.append_data_from_file("", "/some/file.ndjson")

    def test_append_data_empty_file_name(self, empty_database):
        """Test that empty file name raises ValueError."""
        with pytest.raises(ValueError, match="file_name cannot be empty"):
            empty_database.append_data_from_file("test_table", "")

    def test_append_data_nonexistent_file(self, empty_database):
        """Test that nonexistent file raises FileNotFoundError."""
        with pytest.raises(FileNotFoundError):
            empty_database.append_data_from_file("test_table", "/nonexistent/file.ndjson")

    def test_print_all_data_empty_table_name(self, empty_database):
        """Test that empty table name raises ValueError."""
        with pytest.raises(ValueError, match="table_name cannot be empty"):
            empty_database.print_all_data("")

    def test_get_nucleotide_reference_empty_table_name(self, empty_database):
        """Test that empty table name raises ValueError."""
        with pytest.raises(ValueError, match="table_name cannot be empty"):
            empty_database.get_nucleotide_reference_sequence("", "main")

    def test_get_nucleotide_reference_empty_sequence_name(self, empty_database):
        """Test that empty sequence name raises ValueError."""
        with pytest.raises(ValueError, match="sequence_name cannot be empty"):
            empty_database.get_nucleotide_reference_sequence("test", "")

    def test_get_filtered_bitmap_empty_table_name(self, empty_database):
        """Test that empty table name raises ValueError."""
        with pytest.raises(ValueError, match="table_name cannot be empty"):
            empty_database.get_filtered_bitmap("", "filter")

    def test_save_checkpoint_empty_directory(self, empty_database):
        """Test that empty directory raises ValueError."""
        with pytest.raises(ValueError, match="save_directory cannot be empty"):
            empty_database.save_checkpoint("")


class TestDatabaseLoad:
    """Test database loading from saved state."""

    def test_load_nonexistent_path(self):
        """Test that loading from nonexistent path raises FileNotFoundError."""
        from rhydb import Database
        with pytest.raises(FileNotFoundError):
            Database("/nonexistent/path/to/database")


class TestPrintAllData:
    """Test the print_all_data method."""

    def test_print_all_data(self, empty_database, main_reference_sequence, capsys):
        """Test that print_all_data outputs something."""
        # Note: printAllData expects sequence_name="sequence" (hardcoded in C++)
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        # This should print to stdout
        empty_database.print_all_data("sequences")

        # Check that something was printed
        captured = capsys.readouterr()
        # The output goes to C++ stdout, so we may not capture it in Python
        # Just verify no exception was raised


class TestExtraColumns:
    """Test creating tables with extra string columns."""

    def test_create_table_with_extra_columns(self):
        """Test creating a table with extra string columns."""
        from rhydb import Database

        db = Database()
        create_sequence_table(
            db,
            table_name="test",
            primary_key_name="id",
            sequence_name="seq",
            reference_sequence="ACGT",
            extra_columns=["country", "date", "lineage"]
        )
        # If no exception, table was created with extra columns

    def test_extra_columns_accept_data(self):
        """Test that extra columns can store and retrieve data."""
        from rhydb import Database

        db = Database()
        create_sequence_table(
            db,
            table_name="test",
            primary_key_name="id",
            sequence_name="seq",
            reference_sequence="ACGT",
            extra_columns=["country", "lineage"]
        )
        db.append_data_from_string("test", '{"id": "s1", "seq": {"sequence": "AAAA", "insertions": []}, "country": "USA", "lineage": "BA.1"}')
        db.append_data_from_string("test", '{"id": "s2", "seq": {"sequence": "CCCC", "insertions": []}, "country": "UK", "lineage": "BA.2"}')

        query = 'test'
        result = db.query(query)

        assert "country" in result.column_names
        assert "lineage" in result.column_names
        data = result.to_pydict()
        assert set(data["country"]) == {"USA", "UK"}
        assert set(data["lineage"]) == {"BA.1", "BA.2"}

    def test_extra_columns_default_empty(self):
        """Test that extra_columns defaults to empty (backward compatible)."""
        from rhydb import Database

        db = Database()
        # Call without extra_columns - should work as before
        create_sequence_table(
            db,
            table_name="test",
            primary_key_name="id",
            sequence_name="seq",
            reference_sequence="ACGT"
        )
        db.append_data_from_string("test", '{"id": "s1", "seq": {"sequence": "AAAA", "insertions": []}}')

        query = 'test'
        result = db.query(query)
        assert result.num_rows == 1

    def test_gene_table_with_extra_columns(self):
        """Test creating a gene table with extra columns."""
        from rhydb import Database

        db = Database()
        create_sequence_table(
            db,
            sequence_type="amino_acid",
            table_name="genes",
            primary_key_name="id",
            sequence_name="spike",
            reference_sequence="MFVFLVLLPLVSSQCVNLTTRTQLPPAYTNSFTRGVYYPDKVFRSSVLHSTQDLFLPFFSNVTWFHAI*",
            extra_columns=["variant", "source"]
        )
        # If no exception, table was created


class TestCreatedTablesQueryable:
    """Test listing the created tables through the SaneQL `tables()` function."""

    def test_tables_query_lists_all_tables(self, empty_database, main_reference_sequence):
        """Test that tables() lists created tables alongside the built-in ones."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )

        result = empty_database.query("tables()")
        assert isinstance(result, pa.Table)
        assert set(result.to_pydict()["tableName"]) == {"lineage_definitions", "reference_genomes", "sequences"}


class TestQuery:
    """Test the query method that returns PyArrow Tables."""

    def test_query_orders_rows(self, empty_database):
        """order() needs Arrow's compute functions, which the bindings register on import."""
        empty_database.query("createTable(t, {id := string, age := int}, primaryKey := id)")
        empty_database.append_data_from_string(
            "t", '{"id": "b", "age": 1}\n{"id": "a", "age": 3}\n{"id": "c", "age": 2}'
        )

        assert empty_database.query("t.order(by := {id})").to_pydict() == {
            "age": [3, 1, 2],
            "id": ["a", "b", "c"],
        }
        assert empty_database.query("t.order(by := {age}).limit(2).project({id})").to_pydict() == {
            "id": ["b", "c"],
        }

    def test_query_with_regex_after_importing_pyarrow_first(self):
        """like() runs RE2, which uses Abseil. pyarrow ships its own Abseil, which must not replace
        the one of the bindings when pyarrow is imported first. Runs in a subprocess to control the
        import order and to turn a crash into a failure."""
        import subprocess
        import sys
        script = """
import pyarrow
from rhydb import Database
db = Database()
db.query("createTable(t, {id := string, name := string}, primaryKey := id)")
db.append_data_from_string("t", '{"id": "1", "name": "first:main"}\\n{"id": "2", "name": "x"}')
print(db.query("t.filter(name.like('.*:main')).project({name})").to_pydict())
"""
        result = subprocess.run([sys.executable, "-c", script], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr[-2000:]
        assert "{'name': ['first:main']}" in result.stdout

    def test_query_returns_pyarrow_table(self, empty_database, main_reference_sequence):
        """Test that query returns a PyArrow Table."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        query = 'sequences'

        result = empty_database.query(query)

        assert isinstance(result, pa.Table)

    def test_query_has_correct_schema(self, empty_database, main_reference_sequence):
        """Test that the returned table has expected columns."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        query = 'sequences'
        result = empty_database.query(query)

        # Should have at least the primary key column
        assert "primary_key" in result.column_names

    def test_query_returns_data(self, empty_database, main_reference_sequence):
        """Test that query returns rows."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        query = 'sequences'
        result = empty_database.query(query)

        assert result.num_rows > 0

    def test_query_with_filter(self, empty_database, main_reference_sequence):
        """Test query with a filter expression."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        # Get all rows first
        all_query = 'sequences'
        all_result = empty_database.query(all_query)

        # Get filtered rows (False filter should return 0 rows)
        filtered_query = 'sequences.filter(false)'
        filtered_result = empty_database.query(filtered_query)

        assert filtered_result.num_rows == 0
        assert all_result.num_rows > filtered_result.num_rows

    def test_query_to_batches(self, empty_database, main_reference_sequence):
        """Test that the result can be converted to RecordBatches."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        query = 'sequences'
        result = empty_database.query(query)

        batches = result.to_batches()
        assert isinstance(batches, list)
        for batch in batches:
            assert isinstance(batch, pa.RecordBatch)

    def test_query_to_pydict(self, empty_database, main_reference_sequence):
        """Test that the result can be converted to Python dict."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        query = 'sequences'
        result = empty_database.query(query)

        data = result.to_pydict()
        assert isinstance(data, dict)
        assert "primary_key" in data
        assert isinstance(data["primary_key"], list)

    def test_query_empty_query_raises(self, empty_database, main_reference_sequence):
        """Test that empty query raises ValueError."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )

        with pytest.raises(ValueError, match="query_string cannot be empty"):
            empty_database.query("")

    def test_query_invalid_query_raises(self, empty_database, main_reference_sequence):
        """Test that invalid SaneQL raises an error."""
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )

        with pytest.raises(RuntimeError):
            empty_database.query("not valid saneql !")

    def test_query_simple_database(self):
        """Test query with a simple in-memory database."""
        from rhydb import Database

        db = Database()
        create_sequence_table(
            db,
            table_name="test",
            primary_key_name="id",
            sequence_name="seq",
            reference_sequence="ACGT"
        )
        db.append_data_from_string("test", '{"id": "sample1", "seq": {"sequence": "AAAA", "insertions": []}}')
        db.append_data_from_string("test", '{"id": "sample2", "seq": {"sequence": "CCCC", "insertions": []}}')

        query = 'test'
        result = db.query(query)

        assert isinstance(result, pa.Table)
        assert result.num_rows == 2
        assert "id" in result.column_names

        # Verify the data
        data = result.to_pydict()
        assert set(data["id"]) == {"sample1", "sample2"}

    def test_query_preserves_data_after_checkpoint(self, empty_database, main_reference_sequence, temp_dir):
        """Test that query works correctly after loading from checkpoint."""
        from rhydb import Database

        # Create and populate database
        create_sequence_table(
            empty_database,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence
        )
        empty_database.append_data_from_file("sequences", INPUT_FILE)

        # Query before checkpoint
        query = 'sequences'
        result_before = empty_database.query(query)

        # Save and reload
        save_path = os.path.join(temp_dir, "checkpoint")
        empty_database.save_checkpoint(save_path)
        loaded_db = Database(save_path)

        # Query after checkpoint
        result_after = loaded_db.query(query)

        # Results should match
        assert result_before.num_rows == result_after.num_rows
        assert result_before.column_names == result_after.column_names


class TestUpdateStatement:
    """Test the update statement through query().

    These tests cover the translation of C++ errors into Python exceptions and the end-to-end
    update of a plain string column. The update of scalar value columns (int/float/date/bool) is
    covered by TestUpdateStatementOnLoadedDatabase and the C++ unit tests.
    """

    def _database_with_string_column(self, main_reference_sequence):
        from rhydb import Database

        db = Database()
        create_sequence_table(
            db,
            table_name="sequences",
            primary_key_name="primary_key",
            sequence_name="main",
            reference_sequence=main_reference_sequence,
            extra_columns=["country"]
        )
        db.append_data_from_file("sequences", INPUT_FILE)
        return db

    @staticmethod
    def _count(database, filter_expression):
        return len(database.get_filtered_bitmap("sequences", filter_expression))

    def test_update_returns_number_of_updated_rows(self, main_reference_sequence):
        """The statement returns a single-row summary instead of rows."""
        db = self._database_with_string_column(main_reference_sequence)

        result = db.query(
            "update(sequences, set := {country := 'France'}, where := primary_key = 'key_29')"
        )

        assert result.to_pydict() == {"updatedRows": [1]}

    def test_update_unknown_column_raises(self, main_reference_sequence):
        """Updating a non-existent column surfaces the C++ query error as ValueError."""
        db = self._database_with_string_column(main_reference_sequence)
        with pytest.raises(ValueError, match="does not contain a column"):
            db.query("update(sequences, set := {does_not_exist := 1})")

    def test_update_string_column_all_rows(self, main_reference_sequence):
        """A string literal is assigned to every row of a string column."""
        db = self._database_with_string_column(main_reference_sequence)
        total = self._count(db, "true")
        assert self._count(db, "country = 'Switzerland'") == total

        db.query("update(sequences, set := {country := 'Germany'})")

        assert self._count(db, "country = 'Switzerland'") == 0
        assert self._count(db, "country = 'Germany'") == total

    def test_update_string_column_with_filter(self, main_reference_sequence):
        """Only rows matching the predicate are reassigned; a brand-new value is interned."""
        db = self._database_with_string_column(main_reference_sequence)
        total = self._count(db, "true")
        assert total > 1

        db.query("update(sequences, set := {country := 'France'}, where := primary_key = 'key_29')")

        assert self._count(db, "country = 'France'") == 1
        assert self._count(db, "country = 'Switzerland'") == total - 1

    def test_update_string_column_clears_rows_to_null(self, main_reference_sequence):
        """Assigning null clears the matched rows of a string column."""
        db = self._database_with_string_column(main_reference_sequence)
        assert self._count(db, "country.isNull()") == 0

        db.query("update(sequences, set := {country := null}, where := primary_key = 'key_29')")

        assert self._count(db, "country.isNull()") == 1

    def test_update_string_column_type_mismatch_raises(self, main_reference_sequence):
        """A non-string value is rejected for a string column."""
        db = self._database_with_string_column(main_reference_sequence)
        with pytest.raises(ValueError, match="cannot be assigned"):
            db.query("update(sequences, set := {country := 5})")


class TestUpdateStatementOnLoadedDatabase:
    """End-to-end update statement tests against the serialized database checked into the repo.

    That database has real scalar value columns (age:int, qc_value:float, date:date,
    test_boolean_column:bool) filled with real data. The update mutates only the in-memory database
    (the on-disk state is never saved), so every test loads its own fresh copy and the repo files
    are left untouched.
    """

    @pytest.fixture
    def loaded_database(self):
        from rhydb import Database
        return Database(SERIALIZED_STATE_DIR)

    @staticmethod
    def _column(database, name):
        return database.query(f'data.project({{{name}}})').to_pydict()[name]

    @staticmethod
    def _count(database, filter_expression):
        return len(database.get_filtered_bitmap("data", filter_expression))

    def test_update_int_column_all_rows(self, loaded_database):
        """Updating without a predicate assigns the value to every row."""
        total = self._count(loaded_database, "true")
        assert total > 0

        loaded_database.query("update(data, set := {age := 42})")

        assert self._count(loaded_database, "age = 42") == total
        assert all(age == 42 for age in self._column(loaded_database, "age"))

    def test_update_int_column_with_filter(self, loaded_database):
        """Only rows matching the predicate are updated; the rest keep their values."""
        matching = self._count(loaded_database, "age = 4")
        total = self._count(loaded_database, "true")
        assert 0 < matching < total

        loaded_database.query("update(data, set := {age := 100}, where := age = 4)")

        assert self._count(loaded_database, "age = 4") == 0
        assert self._count(loaded_database, "age = 100") == matching
        # Rows that did not match still exist and were not turned into 100.
        assert self._count(loaded_database, "age = 100") < total

    def test_update_float_column(self, loaded_database):
        """Float literals are assigned to a float column."""
        loaded_database.query("update(data, set := {qc_value := 0.5})")
        assert all(abs(value - 0.5) < 1e-9 for value in self._column(loaded_database, "qc_value"))

    def test_update_bool_column(self, loaded_database):
        """Boolean literals are assigned to a bool column (including rows that were null)."""
        loaded_database.query("update(data, set := {test_boolean_column := false})")
        assert all(value is False for value in self._column(loaded_database, "test_boolean_column"))

    def test_update_date_column(self, loaded_database):
        """SaneQL date literals are assigned to a date column."""
        loaded_database.query("update(data, set := {date := '2000-01-01'::date})")
        assert all(
            value == datetime.date(2000, 1, 1) for value in self._column(loaded_database, "date")
        )

    def test_update_clears_rows_to_null(self, loaded_database):
        """Assigning null clears the matched rows."""
        matching = self._count(loaded_database, "age = 4")
        nulls_before = self._count(loaded_database, "age.isNull()")
        assert matching > 0

        loaded_database.query("update(data, set := {age := null}, where := age = 4)")

        assert self._count(loaded_database, "age = 4") == 0
        assert self._count(loaded_database, "age.isNull()") == nulls_before + matching

    def test_update_does_not_persist_to_disk(self, loaded_database):
        """Updating the in-memory database must not modify the on-disk repo state."""
        loaded_database.query("update(data, set := {age := 999})")
        assert self._count(loaded_database, "age = 999") > 0

        from rhydb import Database
        fresh = Database(SERIALIZED_STATE_DIR)
        assert self._count(fresh, "age = 999") == 0


class TestDeleteStatementOnLoadedDatabase:
    """End-to-end delete statement tests against the serialized database checked into the repo.

    The deletion mutates only the in-memory database, so every test loads its own fresh copy and
    the repo files are left untouched.
    """

    @pytest.fixture
    def loaded_database(self):
        from rhydb import Database
        return Database(SERIALIZED_STATE_DIR)

    @staticmethod
    def _count(database, filter_expression):
        return len(database.get_filtered_bitmap("data", filter_expression))

    def test_delete_returns_number_of_deleted_rows(self, loaded_database):
        """Only the rows matching the predicate are deleted; the rest keep their values."""
        matching = self._count(loaded_database, "age = 4")
        total = self._count(loaded_database, "true")
        assert 0 < matching < total
        remaining_keys = loaded_database.query(
            "data.filter(!(age = 4)).project({primaryKey, age})"
        ).to_pydict()

        result = loaded_database.query("delete(data, where := age = 4)")

        assert result.to_pydict() == {"deletedRows": [matching]}
        assert self._count(loaded_database, "true") == total - matching
        assert self._count(loaded_database, "age = 4") == 0
        assert loaded_database.query("data.project({primaryKey, age})").to_pydict() == remaining_keys

    def test_delete_without_predicate_deletes_every_row(self, loaded_database):
        total = self._count(loaded_database, "true")

        result = loaded_database.query("delete(data)")

        assert result.to_pydict() == {"deletedRows": [total]}
        assert self._count(loaded_database, "true") == 0

    def test_delete_with_unknown_column_raises(self, loaded_database):
        with pytest.raises(ValueError, match="unknown column"):
            loaded_database.query("delete(data, where := does_not_exist = 1)")
