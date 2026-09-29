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
.agents/skills/query-silo/queryLocalSilo.sh "default.groupBy({count:=count()})"
```

The script sends a `POST /query` with `Content-Type: text/plain` and prints the NDJSON response followed by the HTTP status code.

### Status code only

```bash
.agents/skills/query-silo/queryLocalSilo.sh -s "default.filter(country='CH').project({primaryKey})"
```

### Query language

Queries use **SaneQL**. Common patterns:

```
default                                          -- full table scan
default.filter(column='value')                   -- filter rows
default.project({col1, col2})                    -- select columns
default.groupBy({count:=count()}, {col})         -- aggregate
default.map({new_col := expression})             -- add computed column
default.orderBy({asc(col)})                      -- sort
default.mutations(minProportion:=0.5)            -- nucleotide mutations
unionAll(pipeline1, pipeline2)                   -- concatenate two pipelines
```

Chaining: `default.filter(...).project({...}).groupBy({...}).orderBy({...})`

### Error responses

On error, the response body contains `{"error": "...", "message": "..."}` with a non-200 status code.

### Notes

- The API must be running on port 8081 before querying.
- Response format is NDJSON (one JSON object per line).
- Always quote the query string to prevent shell expansion of `{}` and `()`.
- RhyDB instance contains dummy dataset with 100 sequences.
  Database config (schema definition) is in `testBaseData/exampleDataset/database_config.yaml`. 
