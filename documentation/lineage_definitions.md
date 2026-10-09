# Documentation: Monophyletic Search Using Lineage DAGs

This specification defines a simple YAML-based input format to enable general monophyletic searches on a tree of lineages. 

By treating lineage names as string labels in a directed acyclic graph (DAG), this approach avoids grammar complexity while remaining versatile and easy to implement.

## Overview

Purpose: Provide a straightforward format for defining lineage relationships for monophyletic search.

Input Format: A YAML configuration file represents lineage relationships in a DAG structure using parent-child relationships and aliases.

## Input Format

The input format is written in YAML and defines lineage labels in terms of:

- parents: Specifies the direct ancestors of the lineage.
- aliases: A list of alternative names for the lineage.

### YAML Structure

```
<lineage_label>:
  parents: [ <lineage_label of parent> ...]
  aliases: [ <alias_label> ...]
```

- `<lineage_label>`: The name of the lineage (unique identifier).
- `parents`: Optional list of parent lineage labels.
- `aliases`: Optional list of alternative names for the lineage.


## Examples

### Example 1: Basic Lineage Tree

A simple tree with a root (`A`) and two child lineages (`B` and `C`):

```
A:
  aliases:
    - Root

B:
  parents: 
    - A
  aliases:
    - BranchB

C:
  parents: 
    - A
  aliases:
    - BranchC
```

### Example 2: Lineage tree with recombinant

A more complex example where lineage `E` has multiple parents (`B` and `D`):

```
A:
  aliases:
    - Root

B:
  parents: 
    - A
  aliases:
    - BranchB

C:
  parent:
    - A
  aliases:
    - BranchC

D:
  parent: 
    - B
  aliases:
    - SubBranchD

E:
  parent: [B, D]
  aliases:
    - LeafE
```

## Key Considerations

- Alias Flexibility: Use aliases to standardize alternative names for lineages.
- Root Lineages: Specify root lineages with `parents: null`, `parents: []`, or by omitting the key `parents`
- Minimal assumptions: RhyDB verifies that the lineage labels are unique and the edges contain no cycles. No further assumptions about the lineage system are made

## Lineage Relation Tables

A lineage definition is attached to a metadata column via `generateLineageIndex` in the database
config. The `lineageIndexType` option of that column controls how it is made available:

- `columnMetadata` (default): the lineage tree lives in the column's metadata and is used by the
  `lineage(...)` filter, documented in [query_documentation.md](query_documentation.md).
- `table`: preprocessing materializes the tree as separate relation and alias tables (see below),
  and the column carries no lineage tree — `lineage(...)` is not available on it, but
  `lineageFromTables(...)` filters on it using these tables.
- `both`: both of the above.

For `table` and `both`, the materialized table is named after the column, so a column
`pango_lineage` yields a table `pango_lineage` that is queried like any other table:

```
pango_lineage
  .filter(parent = 'B.1')
```

### Schema

| Column | Type | Description |
|--------|------|-------------|
| `id` | string | Primary key. An opaque row identifier, not a lineage name |
| `lineage` | string | The canonical lineage name |
| `parent` | string | One direct parent of `lineage`, or `null` if `lineage` is a root |
| `is_recombinant_edge` | boolean | `true` if `lineage` has more than one direct parent |
| `recombinant_clade_ancestor` | string | For a recombinant `lineage`, the most recent common ancestor of its parents. `null` for non-recombinants, and also for a recombinant whose parents share no common ancestor |

### Semantics

- **Direct edges only.** The table holds one row per lineage and each of its immediate parents. The
  transitive ancestry is walked from these edges at query time rather than being materialized, so
  the table has no rows for grandparent relationships.
- **Recombinants yield several rows.** A lineage with *n* parents contributes *n* rows, one per
  parent, each with `is_recombinant_edge` set to `true` and the same
  `recombinant_clade_ancestor`.
- **Roots get a row too.** A root has no parent edge, so it gets a single row with a `null` `parent`
  instead — this both terminates an upward walk and keeps the root present in the table.
- **Derived from the definition, not the data.** The rows come from the lineage definition file, so
  every canonical lineage appears whether or not any sequence carries it.
- **Aliases are not rows.** An alias resolves to its canonical lineage; only canonical lineage names
  appear in `lineage` and `parent`. Aliases are listed in the alias table below.

### Alias Table

Next to the relation table, a table named after the column with an `_aliases` suffix (e.g.
`pango_lineage_aliases`) holds one row per alias in the lineage definition:

| Column | Type | Description |
|--------|------|-------------|
| `alias` | string | Primary key. The alias |
| `lineage` | string | The canonical lineage that `alias` stands for |
