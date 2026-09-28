from libcpp.string cimport string
from libcpp.vector cimport vector
from libcpp.pair cimport pair
from libcpp.optional cimport optional
from libc.stdint cimport uint64_t, uint32_t

cdef extern from "exception_handler.h":
    void handleRhyDBException()

cdef extern from "roaring/roaring.hh" namespace "roaring":
    cdef cppclass Roaring:
        Roaring() except +
        size_t getSizeInBytes() except +
        size_t write(char* buf) except +

cdef extern from "rhydb/database.h" namespace "rhydb":
    cdef cppclass Database:
        Database() except +
        Database(const Database&) except +  # Copy constructor
        void createNucleotideSequenceTable(string table_name, string primary_key_name, string sequence_name, string reference_sequence, vector[string] extra_string_columns) except +
        void createGeneTable(string table_name, string primary_key_name, string sequence_name, string reference_sequence, vector[string] extra_string_columns) except +
        void appendDataFromFile(string table_name, string file_name) except +
        void appendDataFromString(string table_name, string json_string) except +
        void printAllData(string table_name) except +
        string getNucleotideReferenceSequence(string table_name, string sequence_name) except +
        string getAminoAcidReferenceSequence(string table_name, string sequence_name) except +
        Roaring getFilteredBitmap(string table_name, string filter) except +handleRhyDBException
        void updateColumn(string table_name, string column_name, string value, string filter_expression) except +handleRhyDBException
        void saveDatabaseState(string save_directory) except +
        string executeQueryAsArrowIpc(string query_string) except +handleRhyDBException

        @staticmethod
        optional[Database] loadDatabaseStateFromPath(string save_directory) except +