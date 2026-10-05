# RhyDB HTTP API Reference

RhyDB includes an HTTP API for querying preprocessed outputs.

## Running the API Server

```bash
rhydb api \
  --database-config database_config.yaml \
  --runtime-config runtime_config.yaml
```

### Runtime Configuration

Configuration is resolved in order of precedence: CLI arguments > environment variables (prefixed `RHYDB_`) > config file > defaults.

| Key | Default | Description |
|-----|---------|-------------|
| `dataDirectory` | `output` | Path to preprocessed database |
| `api.address` | `0.0.0.0` | Address to bind the HTTP server to (`0.0.0.0` = all interfaces, `127.0.0.1` = localhost only) |
| `api.port` | `8081` | HTTP listen port |
| `api.maxQueuedHttpConnections` | `256` | Maximum queued connections |
| `api.threadsForHttpConnections` | `0` | Worker threads (0 = number of CPUs) |
| `api.estimatedStartupTimeInMinutes` | — | Used in `Retry-After` header during startup |
| `api.allowAdminEndpoint` | `false` | Whether to serve the write-enabled [`POST /admin/query`](#post-adminquery) endpoint. Opt-in: while it is disabled, the instance is read-only and the path returns 404 |
| `query.materializationCutoff` | `32767` | Batch size threshold for streaming. (Note: batch size of results is not guaranteed to stay below this number) |

## Common Response Headers

Every response includes:

| Header | Description |
|--------|-------------|
| `X-Request-Id` | Echoes the request's `X-Request-Id`, or a generated UUID v4 if none was provided. Useful for correlating logs. |
| `data-version` | A 10-digit Unix timestamp identifying the database snapshot used to serve the request. Allows clients to detect when the underlying data has changed between requests. |

The `POST /query` endpoint additionally returns an [`result-ordering`](#response-headers) header describing the sort order of the result rows.

## Endpoints

### `GET /health`

Returns the server health status.

**Response** (200, `application/json`):
```json
{"status": "UP"}
```

During startup, before the database is loaded, the server returns 503 with a `Retry-After` header.

---

### `GET /info`

Returns metadata about the loaded database.

**Response** (200, `application/json`):
```json
{
  "version": "0.1.0",
  "sequenceCount": 100,
  "horizontalBitmapsSize": 5594,
  "verticalBitmapsSize": 28102
}
```

| Field | Description |
|-------|-------------|
| `version` | RhyDB software version |
| `sequenceCount` | Total number of sequences in the database |
| `horizontalBitmapsSize` | Size of horizontal bitmap indexes (bytes) |
| `verticalBitmapsSize` | Size of vertical bitmap indexes (bytes) |

---

### `GET /lineageDefinition/{columnName}`

Returns the lineage tree definition for the given column.

**Parameters:**
- `columnName` (path) — Name of a column with `generateLineageIndex: true`

**Response** (200, `application/yaml`): the lineage definition YAML file.

**Errors** (400):
- Column does not exist
- Column is not of type `indexed-string`
- Column does not have a lineage tree defined

---

### `POST /query`

Executes a query against the database. See [query_documentation.md](query_documentation.md) for the full filter and action reference.

**Request** (`application/json`):
```json
{
  "filterExpression": { "type": "True" },
  "action": { "type": "Aggregated" }
}
```

Both `filterExpression` and `action` are required.

**Errors** (400, `application/json`):
```json
{
  "error": "Bad request",
  "message": "description of what went wrong"
}
```

Queries time out after 120 seconds.

#### Response Headers

In addition to the [common response headers](#common-response-headers), a successful (200) query response carries an `result-ordering` header describing the order in which the result rows are returned.

| Header | Description |
|--------|-------------|
| `result-ordering` | JSON array describing the sort order of the returned rows, one element per sort key. |

**Format.** The value is a JSON array. Each element describes one sort key, from most to least significant, with these fields:

| Field | Values | Description |
|-------|--------|-------------|
| `field` | column name | The column the rows are sorted by. |
| `order` | `"ascending"` \| `"descending"` | Sort direction. |
| `nullPlacement` | `"atStart"` \| `"atEnd"` | Whether null values sort before or after non-null values. |

Examples:

| Query | `result-ordering` header |
|-------|-------------------|
| `order(by:={date})` | `[{"field":"date","order":"ascending","nullPlacement":"atStart"}]` |
| `order(by:={country, date.desc()})` | `[{"field":"country","order":"ascending","nullPlacement":"atStart"},{"field":"date","order":"descending","nullPlacement":"atEnd"}]` |
| no `order` (e.g. an aggregation) | `[]` |

#### Output Format Negotiation

The output format is selected via the HTTP `Accept` header:

| Accept header value | Content-Type | Format |
|---------------------|--------------|--------|
| *(default, or any other value)* | `application/x-ndjson` | Newline-delimited JSON |
| `application/vnd.apache.arrow.stream` | `application/vnd.apache.arrow.stream` | Apache Arrow IPC stream |

##### NDJSON (default)

Each result row is a self-contained JSON object on its own line:

```
{"count":100}
```

NDJSON is human-readable and easy to consume from any language. However, because each line is independent, there is no built-in way for a client to distinguish a complete response from one that was truncated mid-stream (e.g. due to a network interruption or server timeout). A client that reads partial NDJSON will silently receive fewer rows than expected.

##### Arrow IPC Streaming

Request this format by setting the `Accept` header:

```
Accept: application/vnd.apache.arrow.stream
```

The response is a binary [Apache Arrow IPC stream](https://arrow.apache.org/docs/format/Columnar.html#ipc-streaming-format). This format:

- **Guards against incomplete downloads.** The stream begins with a schema message and ends with an explicit end-of-stream marker written by `Close()`. Arrow client libraries will raise an error when reading a stream that is missing this marker, so a truncated response (from a network drop, timeout, or server crash) is detected automatically rather than silently returning partial results.
- **Preserves type information.** Column types (integers, floats, booleans, strings) are carried in the schema rather than inferred from text, avoiding issues like integer-vs-float ambiguity in JSON.
- **Is efficient for large result sets.** Data is columnar and binary-encoded, avoiding the overhead of JSON serialization and text parsing.

Arrow IPC libraries are available for Python (`pyarrow`), R (`arrow`), JavaScript (`apache-arrow`), Rust, Go, Java, and many other languages.

Example with `curl` and Python:

```bash
curl -X POST \
  -H "Accept: application/vnd.apache.arrow.stream" \
  -d '{"action":{"type":"Aggregated"},"filterExpression":{"type":"True"}}' \
  http://localhost:8081/query --output result.arrow
```

```python
import pyarrow.ipc as ipc

with open("result.arrow", "rb") as f:
    reader = ipc.open_stream(f)
    table = reader.read_all()

print(table.to_pandas())
```

---

### `POST /admin/query`

Executes a SaneQL **write statement** against the database:
- `insertInto(query: expression, table: symbol)`. Runs `query` and inserts its result rows into
`table` — see [`insertInto`](query_documentation.md) for semantics and
limitations.
- `createTable(table: symbol, columns: record, primaryKey?: symbol)`. Creates a new, empty table with
the given schema, taking the reference sequences of sequence columns from `reference_genomes` and
the dictionaries of compressed string columns from queries —
see [`query docs`](query_documentation.md) for the column types.

The write goes through the [data directory](#runtime-configuration): the most recent state there is
loaded into a database of its own, the statement is applied to that one, and the result is saved
back as a new data version.

The endpoint is **opt-in**: it is only served when
[`api.allowAdminEndpoint`](#runtime-configuration) is set to `true`. While it is disabled the path
does not exist and requests to it get a 404, so an instance that does not enable it stays read-only.
When enabled, it is generally available and no authentication is possible. If used in publicly
available instances the endpoint should be guarded by other means (e.g. using `nginx` rules).

**Request** (a SaneQL write statement as the raw request body):
```
source.filter(country='CH').project({primaryKey, country}).insertInto(archive)
```

**Response** (200): a single row summarizing the effect, as NDJSON (`application/x-ndjson`) or, when
requested via the `Accept: application/vnd.apache.arrow.stream` header, as an Arrow IPC stream, the
same as for [`POST /query`](#post-query). For `insertInto`, the number of rows inserted:
```json
{"insertedRows": 42}
```

For `createTable`, the name of the created table:
```json
{"createdTable": "archive"}
```

A successful response carries the [`data-version`](#common-response-headers) header naming the
version the write produced, which is served once the directory watcher has picked it up. A failed
write leaves the data version unchanged.

**Errors** (400, `application/json`): returned when the body is not a valid write statement.
A 500 is returned when the write itself could not be carried out, e.g. because the
data directory holds no state to write to or could not be written.

```bash
curl -X POST \
  --data "source.filter(country='CH').project({primaryKey, country}).insertInto(archive)" \
  http://localhost:8081/admin/query
```

The append times out after 120 seconds.

---

## Error Responses

All error responses are JSON, regardless of the `Accept` header.

| Status | Meaning |
|--------|---------|
| 400 | Bad request — malformed JSON, invalid query, unknown filter/action type |
| 404 | Unknown endpoint |
| 405 | HTTP method not allowed on this endpoint |
| 500 | Internal server error |
| 503 | Database not yet loaded; `Retry-After` header indicates estimated wait |

Error body:
```json
{
  "error": "Bad request",
  "message": "detailed description"
}
```
