# RhyDB

High-performance analytical database for sequence alignment data

For information on how to build, test, and contribute to RhyDB, see [Contributing](documentation/developer/contributing.md).

## Python Bindings

RhyDB provides Python bindings via Cython. The bindings wrap the core C++ `Database` and are installable by `pip install rhydb`.

See [Contributing](documentation/developer/contributing.md#building-python-bindings) for build instructions.

### Usage

```python
from rhydb import Database

# Create a new database
db = Database()

# Or load from a saved state
db = Database("/path/to/saved/database")

# Create a nucleotide sequence table
db.create_nucleotide_sequence_table(
    table_name="sequences",
    primary_key_name="id",
    sequence_name="main",
    reference_sequence="ACGT..."
)

# Append data from file
db.append_data_from_file("sequences", "/path/to/data.ndjson")

# Get reference sequence
ref = db.get_nucleotide_reference_sequence("sequences", "main")

# Get filtered bitmap (list of matching row indices)
indices = db.get_filtered_bitmap("sequences", "some_filter")

# Query data through the SaneQL front-end
table = db.query("sequences.filter(true)")

# Save database state
db.save_checkpoint("/path/to/save/directory")

# Print all data (to stdout)
db.print_all_data("sequences")
```

## Configuration Files

For RhyDB, there are three different configuration files:

- `DatabaseConfig` described in
  file [database_config.h](src/rhydb/config/database_config.h)
- `PreprocessingConfig` used when started with `preprocessing` and described in
  file [preprocessing_config.h](src/rhydb/config/preprocessing_config.h).
  For details see `rhydb preprocessing --help`.
- `RuntimeConfig` used when started with `api` and described in
  file [runtime_config.h](src/rhydb/config/runtime_config.h)
  For details see `rhydb api --help`.

The database config contains the schema of the database and is always required when preprocessing data. The database
config will be saved together with the output of the preprocessing and is therefore not required when starting RhyDB as
an API.

An example configuration file can be seen
in [testBaseData/exampleDataset/database_config.yaml](testBaseData/exampleDataset/database_config.yaml).

By default, the config files are expected to be YAML files in the current working directory in
snake_case (`database_config.yaml`, `preprocessing_config.yaml`, `runtime_config.yaml`), but their location can be
overridden using the options `--database-config=X`, `--preprocessing-config=X`, and `--runtime-config=X`.

Preprocessing and Runtime configurations contain default values for all fields and are thus only optional. Their
parameters can also be provided as command-line arguments in snake_case and as environment variables prefixed with RHYDB_
in capital SNAKE_CASE. (e.g. RHYDB_INPUT_DIRECTORY).

The precendence is `CLI argument > Environment Variable > Configuration File > Default Value`

### Run The Preprocessing

The preprocessing acts as a program that takes an input directory that contains the to-be-processed data
and an output directory where the processed data will be stored.
Both need to be mounted to the container.

RhyDB expects a preprocessing config that can be mounted to the default location `/app/preprocessing_config.yaml`.

Additionally, a database config and a ndjson file containing the data are required. They should typically be mounted in `/preprocessing/input`.

```shell
docker run \
  -v your/input/directory:/preprocessing/input \
  -v your/preprocessing/output:/preprocessing/output \
  -v your/preprocessing_config.yaml:/app/preprocessing_config.yaml \
  ghcr.io/rhyorg/rhydb preprocessing
```

Both config files can also be provided in custom locations:

```shell
rhydb preprocessing --preprocessing-config=./custom/preprocessing_config.yaml --database-config=./custom/database_config.yaml
```

The Docker image contains a default preprocessing config that sets defaults specific for running RhyDB in Docker.
Apart from that, there are default values if neither user-provided nor default config specify fields.
The user-provided preprocessing config can be used to overwrite the default values. For a full reference,
see the help text.

### Run docker container (api)

After preprocessing the data, the api can be started with the following command:

```shell
docker run \
  -p 8081:8081 \
  -v your/preprocessing/output:/data \
  ghcr.io/rhyorg/rhydb api
```

The directory where RhyDB expects the preprocessing output can be overwritten via
`rhydb api --data-directory=/custom/data/directory` or in a corresponding
[configuration file](#configuration-files).

# Acknowledgments

Original genome indexing logic with roaring bitmaps by Prof. Neumann: https://db.in.tum.de/~neumann/gi/
