# python/rhydb/database.pyx
from libcpp.string cimport string
from libcpp.vector cimport vector
from libcpp.pair cimport pair
from libcpp.optional cimport optional
from libc.stdint cimport uint64_t, uint32_t
from libc.stdlib cimport malloc, free
from cython.operator cimport dereference as deref
from database cimport Database as CppDatabase, Roaring as CppRoaring, preprocessDatabase
import os
import pyroaring
import pyarrow as pa

def _existing_path(str name, path):
    """The UTF-8 encoded path of an existing file, for an argument named `name`"""
    path = os.fspath(path)
    if not path or not path.strip():
        raise ValueError(f"{name} cannot be empty")
    if not os.path.exists(path):
        raise FileNotFoundError(f"File not found: {path}")
    return path.encode('utf-8')

cdef class PyDatabase:
    """Python wrapper for C++ Database"""
    cdef CppDatabase* c_database
    
    def __cinit__(self, str file_name=None):
        cdef string cpp_file_name
        cdef optional[CppDatabase] loaded

        if file_name is not None:
            if not os.path.exists(file_name):
                raise FileNotFoundError(f"Database file not found: {file_name}")

            cpp_file_name = file_name.encode('utf-8')
            loaded = CppDatabase.loadDatabaseStateFromPath(cpp_file_name)

            if not loaded.has_value():
                raise RuntimeError(f"Failed to load database from '{file_name}'")

            # Allocate and move the loaded database to heap
            self.c_database = new CppDatabase(deref(loaded))
        else:
            self.c_database = new CppDatabase()
    
    def __dealloc__(self):
        if self.c_database != NULL:
            del self.c_database

    def save_checkpoint(self, str save_directory):
        """
        Save a checkpoint of the database to a directory

        Parameters
        ----------
        save_directory : str
            Path to the directory where the database state will be saved
        """
        if not save_directory or not save_directory.strip():
            raise ValueError("save_directory cannot be empty")

        cdef string cpp_save_directory = save_directory.encode('utf-8')

        try:
            self.c_database.saveDatabaseState(cpp_save_directory)
        except Exception as e:
            raise RuntimeError(f"Failed to save checkpoint to '{save_directory}': {e}")

    def preprocess(
        self,
        database_config,
        reference_genomes,
        input_file,
        lineage_definitions=(),
        phylogenetic_tree=None,
        bint without_unaligned_sequences=False,
    ):
        """
        Create the default table from initialization files and fill it with NDJSON data

        This does what the ``preprocessing`` command does, but into this database instead of a
        saved database state: it creates the table from the database config and the reference
        genomes, fills the built-in ``reference_genomes`` and ``lineage_definitions`` tables, builds
        the lineage relation tables, appends the input data and validates that the primary key is
        unique. The built-in tables of this database must still be empty.

        Parameters
        ----------
        database_config : str or os.PathLike
            Path to the database config (YAML or JSON)
        reference_genomes : str or os.PathLike
            Path to the reference genomes JSON file
        input_file : str or os.PathLike
            Path to the NDJSON input data
        lineage_definitions : iterable of str or os.PathLike, optional
            Paths to the lineage definition files. A column's ``generateLineageIndex`` names its
            file, with or without the ``.yaml`` extension
        phylogenetic_tree : str or os.PathLike, optional
            Path to the phylogenetic tree file
        without_unaligned_sequences : bool, optional
            Do not create the ``unaligned_<sequence>`` columns (default: False)
        """
        cdef vector[string] cpp_lineage_definitions
        cdef optional[string] cpp_phylogenetic_tree
        cdef string cpp_database_config
        cdef string cpp_reference_genomes
        cdef string cpp_input_file

        cpp_database_config = _existing_path("database_config", database_config)
        cpp_reference_genomes = _existing_path("reference_genomes", reference_genomes)
        cpp_input_file = _existing_path("input_file", input_file)
        for path in lineage_definitions:
            cpp_lineage_definitions.push_back(_existing_path("lineage_definitions", path))
        if phylogenetic_tree is not None:
            cpp_phylogenetic_tree = <string> _existing_path("phylogenetic_tree", phylogenetic_tree)

        try:
            preprocessDatabase(
                deref(self.c_database),
                cpp_database_config,
                cpp_reference_genomes,
                cpp_input_file,
                cpp_lineage_definitions,
                cpp_phylogenetic_tree,
                without_unaligned_sequences,
            )
        except Exception as e:
            raise RuntimeError(
                f"""Failed to preprocess: {e}
                The database object might be invalid now. Any further operations can lead to undefined behavior."""
            )

    def append_data_from_file(self, str table_name, str file_name):
        """
        Append data from file to table
        
        Parameters
        ----------
        table_name : str
            Name of the table
        file_name : str
            Path to data file
        """
        if not table_name or not table_name.strip():
            raise ValueError("table_name cannot be empty")
        
        if not file_name or not file_name.strip():
            raise ValueError("file_name cannot be empty")
        
        if not os.path.exists(file_name):
            raise FileNotFoundError(f"File not found: {file_name}")
        
        cdef string cpp_table_name = table_name.encode('utf-8')
        cdef string cpp_file_name = file_name.encode('utf-8')
        
        try:
            self.c_database.appendDataFromFile(cpp_table_name, cpp_file_name)
        except Exception as e:
            raise RuntimeError(
                f"""Failed to append data: {e}
                The database object might be invalid now. Any further operations can lead to undefined behavior."""
            )

    def append_data_from_string(self, str table_name, str json_string):
        """
        Append data from a JSON string to table
        
        Parameters
        ----------
        table_name : str
            Name of the table
        json_string : str
            a serialized json containing the data to append
        """
        if not table_name or not table_name.strip():
            raise ValueError("table_name cannot be empty")
        
        if not json_string or not json_string.strip():
            raise ValueError("json_string cannot be empty")
        
        cdef string cpp_table_name = table_name.encode('utf-8')
        cdef string cpp_json_string = json_string.encode('utf-8')
        
        try:
            self.c_database.appendDataFromString(cpp_table_name, cpp_json_string)
        except Exception as e:
            raise RuntimeError(
                f"""Failed to append data: {e}
                The database object might be invalid now. Any further operations can lead to undefined behavior."""
            )
    def print_all_data(self, str table_name):
        """
        Print all data in a table to stdout

        Parameters
        ----------
        table_name : str
            Name of the table
        """
        if not table_name or not table_name.strip():
            raise ValueError("table_name cannot be empty")

        cdef string cpp_table_name = table_name.encode('utf-8')

        try:
            self.c_database.printAllData(cpp_table_name)
        except Exception as e:
            raise RuntimeError(f"Failed to print data from table '{table_name}': {e}")

    def get_nucleotide_reference_sequence(self, str table_name, str sequence_name):
        """
        Get the nucleotide reference sequence for a given table and sequence name

        Parameters
        ----------
        table_name : str
            Name of the table
        sequence_name : str
            Name of the sequence

        Returns
        -------
        str
            The nucleotide reference sequence
        """
        cdef string cpp_table_name
        cdef string cpp_sequence_name
        cdef string result

        if not table_name or not table_name.strip():
            raise ValueError("table_name cannot be empty")
        if not sequence_name or not sequence_name.strip():
            raise ValueError("sequence_name cannot be empty")

        cpp_table_name = table_name.encode('utf-8')
        cpp_sequence_name = sequence_name.encode('utf-8')

        try:
            result = self.c_database.getNucleotideReferenceSequence(cpp_table_name, cpp_sequence_name)
            return result.decode('utf-8')
        except Exception as e:
            raise RuntimeError(f"Failed to get nucleotide reference sequence: {e}")

    def get_amino_acid_reference_sequence(self, str table_name, str sequence_name):
        """
        Get the amino acid reference sequence for a given table and sequence name

        Parameters
        ----------
        table_name : str
            Name of the table
        sequence_name : str
            Name of the sequence

        Returns
        -------
        str
            The amino acid reference sequence
        """
        cdef string cpp_table_name
        cdef string cpp_sequence_name
        cdef string result

        if not table_name or not table_name.strip():
            raise ValueError("table_name cannot be empty")
        if not sequence_name or not sequence_name.strip():
            raise ValueError("sequence_name cannot be empty")

        cpp_table_name = table_name.encode('utf-8')
        cpp_sequence_name = sequence_name.encode('utf-8')

        try:
            result = self.c_database.getAminoAcidReferenceSequence(cpp_table_name, cpp_sequence_name)
            return result.decode('utf-8')
        except Exception as e:
            raise RuntimeError(f"Failed to get amino acid reference sequence: {e}")

    def get_filtered_bitmap(self, str table_name, str filter_expression=""):
        """
        Get a roaring bitmap of row indices matching the filter expression

        Parameters
        ----------
        table_name : str
            Name of the table
        filter_expression : str, optional
            SaneQL filter expression (default: 'true' which matches all rows)

        Returns
        -------
        pyroaring.BitMap
            A roaring bitmap containing the indices of matching rows
        """
        cdef string cpp_table_name
        cdef string cpp_filter
        cdef CppRoaring bitmap
        cdef size_t size_in_bytes
        cdef char* buffer

        if not table_name or not table_name.strip():
            raise ValueError("table_name cannot be empty")

        # Default to True filter (returns all rows) if no filter specified
        if filter_expression is None or filter_expression == "":
            filter_expression = 'true'

        cpp_table_name = table_name.encode('utf-8')
        cpp_filter = filter_expression.encode('utf-8')

        try:
            bitmap = self.c_database.getFilteredBitmap(cpp_table_name, cpp_filter)

            # Serialize the C++ bitmap to bytes
            size_in_bytes = bitmap.getSizeInBytes()
            buffer = <char*>malloc(size_in_bytes)
            if buffer == NULL:
                raise MemoryError("Failed to allocate buffer for bitmap serialization")

            try:
                bitmap.write(buffer)
                # Create Python bytes object from buffer
                serialized = bytes(buffer[:size_in_bytes])
            finally:
                free(buffer)

            # Deserialize into pyroaring BitMap
            return pyroaring.BitMap.deserialize(serialized)
        except ValueError:
            raise
        except Exception as e:
            raise RuntimeError(f"Failed to get filtered bitmap: {e}")

    def query(self, str query_string):
        """
        Execute a query and return results as a PyArrow Table

        Write statements such as ``createTable(...)``, ``<query>.insertInto(<table>)`` and
        ``update(<table>, set := {...}, where := ...)`` are applied to this database in place and
        return a single-row summary of their effect (e.g. ``{createdTable: "sequences"}`` or
        ``{updatedRows: 2}``). Sequence columns take their reference from the
        built-in ``reference_genomes`` table, so add the reference there before creating a table
        with a sequence column. Not thread-safe: write statements mutate the in-memory database
        with no internal locking.

        Parameters
        ----------
        query_string : str
            SaneQL query string. The leading identifier is the table name.
            Example: 'sequences.filter(true)' or 'sequences.mutations(minProportion:=0.05)'

        Returns
        -------
        pyarrow.Table
            Query results as a PyArrow Table. Use .to_batches() to get individual RecordBatches.

        Example
        -------
        >>> db = PyDatabase()
        >>> db.append_data_from_string(
        ...     "reference_genomes", '{"name": "main", "type": "nucleotide", "sequence": "ACGT"}'
        ... )
        >>> db.query(
        ...     "createTable(sequences, {id := string, country := string, "
        ...     "main := nucleotideSequence(reference := main)}, primaryKey := id)"
        ... )
        >>> table = db.query("sequences.filter(true)")
        >>> print(table.schema)
        >>> df = table.to_pandas()  # Convert to pandas DataFrame
        """
        cdef string cpp_query_string
        cdef string ipc_buffer

        if not query_string or not query_string.strip():
            raise ValueError("query_string cannot be empty")

        cpp_query_string = query_string.encode('utf-8')

        try:
            ipc_buffer = self.c_database.executeQueryAsArrowIpc(cpp_query_string)

            # Convert IPC buffer to PyArrow Table
            ipc_bytes = (<char*> ipc_buffer.data())[:ipc_buffer.size()]
            buffer_reader = pa.BufferReader(ipc_bytes)
            reader = pa.ipc.open_stream(buffer_reader)
            return reader.read_all()
        except ValueError:
            raise
        except Exception as e:
            raise RuntimeError(f"Failed to execute query: {e}")


    def __repr__(self):
        return "Database()"
    
    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        return False