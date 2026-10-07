# Data Directories and Serialization Versioning

## How RhyDB discovers data directories

When RhyDB starts in API mode, it scans a configured output directory for preprocessed database states. Each database state is stored in a timestamped subdirectory containing a `data_version.silo` file.

### Directory structure

```
output/
  1700000000/
    data_version.silo
    database_schema.silo
    data.silo
  1700100000/
    data_version.silo
    database_schema.silo
    data.silo
```

### data_version.silo format

Each `data_version.silo` is a YAML file with two fields:

```yaml
timestamp: 1700100000
serializationVersion: 1774442520
```

- **timestamp**: Unix epoch seconds when the database was preprocessed. Must consist entirely of digits and must match the directory name.
- **serializationVersion**: A Unix timestamp (seconds since epoch) identifying the serialization format version. RhyDB will only load databases whose serialization version matches its own built-in version exactly.

### Selection algorithm

RhyDB selects a data directory as follows:

1. Scan all entries in the output directory.
2. Skip entries that are not directories, whose name is not a valid numeric timestamp, or that do not contain a `data_version.silo` file.
3. Parse `data_version.silo` and verify that the timestamp inside it matches the directory name.
4. Sort remaining candidates by timestamp in descending order (most recent first).
5. Select the first candidate whose `serializationVersion` matches the current RhyDB binary's built-in serialization version.
6. If no compatible candidate is found, RhyDB reports that no valid data is available.

Incompatible data directories (from older or newer RhyDB versions) are skipped with a warning.

