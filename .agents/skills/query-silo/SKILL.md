---
name: query-silo
description: >
  Query a locally running RhyDB instance using queryLocalSilo.sh. Use when user says
  "query silo", "query rhydb", "run query", "test query", "try this query", or wants to
  execute a SaneQL query against the local API.
---

## How to query

Use `queryLocalSilo.sh` (in this skill's directory) to send SaneQL queries to the local RhyDB API (port 8081):

```bash
.agents/skills/query-silo/queryLocalSilo.sh "data.group(by:={}, aggs:={count:=count()})"
```

The script sends a `POST /query` with `Content-Type: text/plain` and prints the NDJSON response followed by the HTTP status code.

### Status code only

```bash
.agents/skills/query-silo/queryLocalSilo.sh -s "data.filter(country='CH').project({primaryKey})"
```

### Query language

Queries use **SaneQL**. Common patterns:

```
data                                             -- full table scan
data.filter(column='value')                      -- filter rows
data.project({col1, col2})                       -- select columns
data.group(by:={col}, aggs:={count:=count()})            -- aggregate
data.map({new_col := expression})                -- add computed column
data.order(by:={asc(col)})                         -- sort
data.mutations(minProportion:=0.5)               -- nucleotide mutations
unionall(pipeline1, pipeline2)                   -- concatenate two pipelines
```

Chaining: `data.filter(...).project({...}).group(by:={}, aggs:={...}).order(by:={...})`

### Error responses

On error, the response body contains `{"error": "...", "message": "..."}` with a non-200 status code.

### Notes

- The API must be running on port 8081 before querying.
- Response format is NDJSON (one JSON object per line).
- Always quote the query string to prevent shell expansion of `{}` and `()`.
- RhyDB instance contains dummy dataset with 100 sequences.
  Database config (schema definition) is in `testBaseData/exampleDataset/database_config.yaml`. 
