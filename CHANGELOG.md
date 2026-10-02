# Changelog

## [0.15.0](https://github.com/RhyOrg/RhyDB/compare/v0.14.5...v0.15.0) (2026-10-02)


### ⚠ BREAKING CHANGES

* **query_engine:** the `fields` parameter of `projectout` is renamed to `remove`. Positional calls are unaffected; migrate `t.projectout(fields:={...})` to `t.projectout(remove:={...})`.
* **query_engine:** the `fields` parameter of `project` is renamed to `expressions`. Positional calls are unaffected; migrate `t.project(fields:={...})` to `t.project(expressions:={...})`.
* **query_engine:** the `predicate` parameter of `filter` is renamed to `condition`. Positional calls are unaffected; migrate `t.filter(predicate:=...)` to `t.filter(condition:=...)`.
* **query_engine:** `unionAll` is renamed to `unionall`. Migrate `unionAll(a, b)` / `a.unionAll(b)` to `unionall(a, b)` / `a.unionall(b)`.
* **query_engine:** `orderBy` is renamed to `order` and its `fields` parameter is renamed to `by`. Migrate `t.orderBy({col})` to `t.order(by:={col})`.

### Features

* **query_engine:** only accept `to` and `from` in `transitiveClosure` as identifiers ([#1620](https://github.com/RhyOrg/RhyDB/issues/1620)) ([7e441bf](https://github.com/RhyOrg/RhyDB/commit/7e441bf303b317dcb802472615384cee662ce9d6))
* **query_engine:** only accept an identifier as the `insertInto` target ([#1618](https://github.com/RhyOrg/RhyDB/issues/1618)) ([626814f](https://github.com/RhyOrg/RhyDB/commit/626814fc4c5ecd618a1235ddd9dea0050d34727e))


### Bug Fixes

* **query_engine:** rename filter parameter predicate to condition ([0eb5fd6](https://github.com/RhyOrg/RhyDB/commit/0eb5fd6aa1b400462f86a2ef1aae3ccfe2081a26))
* **query_engine:** rename orderBy to order with by parameter ([f0c2951](https://github.com/RhyOrg/RhyDB/commit/f0c2951c56a6237b2ea256d50e7876a21485dca1))
* **query_engine:** rename project parameter fields to expressions ([69ab9fe](https://github.com/RhyOrg/RhyDB/commit/69ab9fe632a388a331a0afbd947cfb6feee31f0b))
* **query_engine:** rename projectout parameter fields to remove ([e37cc14](https://github.com/RhyOrg/RhyDB/commit/e37cc14c54da1896ebaf9b9f67844db0477ff083))
* **query_engine:** rename unionAll to unionall ([79b7d67](https://github.com/RhyOrg/RhyDB/commit/79b7d676d2be81bac3f1ae86e559635eee394305))

## [0.14.5](https://github.com/RhyOrg/RhyDB/compare/v0.14.4...v0.14.5) (2026-09-28)


### Features

* **query_engine:** accept set literals of any scalar type in in() ([#1579](https://github.com/RhyOrg/RhyDB/issues/1579)) ([970ce71](https://github.com/RhyOrg/RhyDB/commit/970ce717ddd96e15b40d60a6a516984506f8ce87))
* **query_engine:** add `sum` aggregation function ([#1584](https://github.com/RhyOrg/RhyDB/issues/1584)) ([3c4473c](https://github.com/RhyOrg/RhyDB/commit/3c4473ca9b720e188bed6c1f13f9b8ca120ae6d2))
* **rhydb:** introduce `built-in` tables, which are always present ([#1591](https://github.com/RhyOrg/RhyDB/issues/1591)) ([4a8cc26](https://github.com/RhyOrg/RhyDB/commit/4a8cc2649368bdda4d2dad53ed01395c929fd223))


### Bug Fixes

* **query_engine:** do not report incorrect results when bitmap container is empty ([#1608](https://github.com/RhyOrg/RhyDB/issues/1608)) ([b3c37c4](https://github.com/RhyOrg/RhyDB/commit/b3c37c41919515549424383e3c2aaf2471ca0a81))
* **query_engine:** don't hardcode the "count(*)" result column name to `count` ([#1590](https://github.com/RhyOrg/RhyDB/issues/1590)) ([e3455d8](https://github.com/RhyOrg/RhyDB/commit/e3455d87d317a4bb3eb8d3aa0084bcc8a826bf9c))
* **query_engine:** let a plan finish before tearing it down after end of stream ([cf5c61f](https://github.com/RhyOrg/RhyDB/commit/cf5c61f53996b40781d6366ee9945a869f0ca0a1))


### Performance Improvements

* **query_engine:** narrow columns through joins ([#1595](https://github.com/RhyOrg/RhyDB/issues/1595)) ([ca1aae8](https://github.com/RhyOrg/RhyDB/commit/ca1aae88a3951d2626f28d480cb9c1104727c809))

## [0.14.4](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.14.3...v0.14.4) (2026-09-24)


### Features

* **preprocessing:** create a reference_genomes table on creation ([#1573](https://github.com/GenSpectrum/LAPIS-SILO/issues/1573)) ([ae84149](https://github.com/GenSpectrum/LAPIS-SILO/commit/ae8414971294d34d8ca69489f14e84dba65cec56))
* **query_engine:** add `projectout()` function to query language ([#1550](https://github.com/GenSpectrum/LAPIS-SILO/issues/1550)) ([964fe38](https://github.com/GenSpectrum/LAPIS-SILO/commit/964fe384cba095cd76f8d6c695af2f31e7a8cb52))
* **query_engine:** add `tables()` function to list all tables ([#1555](https://github.com/GenSpectrum/LAPIS-SILO/issues/1555)) ([9e38dda](https://github.com/GenSpectrum/LAPIS-SILO/commit/9e38dda869db662c56b9f643a21784b81f08d837))
* **query_engine:** add startingFrom to transitiveClosure() ([#1563](https://github.com/GenSpectrum/LAPIS-SILO/issues/1563)) ([7451f52](https://github.com/GenSpectrum/LAPIS-SILO/commit/7451f52153925c5c3a58829a0bce6728e7fc226f))
* **query_engine:** allow filters on subexpressions ([#1506](https://github.com/GenSpectrum/LAPIS-SILO/issues/1506)) ([851231b](https://github.com/GenSpectrum/LAPIS-SILO/commit/851231bd45349dcf73f320c0a55902b5678380cc))
* **query_engine:** implement more efficient bitmap-aggregation ([#1556](https://github.com/GenSpectrum/LAPIS-SILO/issues/1556)) ([4df8211](https://github.com/GenSpectrum/LAPIS-SILO/commit/4df8211532b104c9fe0dde81db7fcec381f1e807))
* **rhydb:** make primaryKey of tables optional ([#1545](https://github.com/GenSpectrum/LAPIS-SILO/issues/1545)) ([22936c9](https://github.com/GenSpectrum/LAPIS-SILO/commit/22936c9a899b6943be9dde45c4888cf229be4569))

## [0.14.3](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.14.2...v0.14.3) (2026-09-09)


### Features

* add `a[i]` square-bracket notation ([#1524](https://github.com/GenSpectrum/LAPIS-SILO/issues/1524)) ([c99129c](https://github.com/GenSpectrum/LAPIS-SILO/commit/c99129c07c355d7b995c5ff62e68b50953d7b2ba))


### Bug Fixes

* **rhydb:** implement custom arrow::acero node for selectK ([#1544](https://github.com/GenSpectrum/LAPIS-SILO/issues/1544)) ([b545e1f](https://github.com/GenSpectrum/LAPIS-SILO/commit/b545e1fd7338e56bb3de0d5a3c9283578e620787))

## [0.14.2](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.14.1...v0.14.2) (2026-09-04)


### Features

* **app:** add config option for binding address ([#1539](https://github.com/GenSpectrum/LAPIS-SILO/issues/1539)) ([62d13a6](https://github.com/GenSpectrum/LAPIS-SILO/commit/62d13a65f9b81879d520ba044617acc8ee1b820b))
* **rhydb:** add `transitiveClosure` table function ([#1493](https://github.com/GenSpectrum/LAPIS-SILO/issues/1493)) ([2e43349](https://github.com/GenSpectrum/LAPIS-SILO/commit/2e43349f9425392c313b2b2175a250f1a4ae6716))
* **rhydb:** add function `insertInto` and `/admin/query` endpoint ([#1502](https://github.com/GenSpectrum/LAPIS-SILO/issues/1502)) ([660b830](https://github.com/GenSpectrum/LAPIS-SILO/commit/660b830945b631fea7a988e8601a04540d4173c6))

## [0.14.1](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.14.0...v0.14.1) (2026-09-03)


### Features

* **query_engine:** allow a boolean field ref as filter ([#1517](https://github.com/GenSpectrum/LAPIS-SILO/issues/1517)) ([bfd6361](https://github.com/GenSpectrum/LAPIS-SILO/commit/bfd636160833f87a30d8dac6c8b0bcd7f550018e))

## [0.14.0](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.13.3...v0.14.0) (2026-09-03)


### ⚠ BREAKING CHANGES

* environment variables now use the `RHYDB_` prefix instead of `SILO_`, and the `rhydb append` options `--silo-directory`/`--silo-data-source` are renamed to `--data-directory`/`--data-source` (YAML keys `dataDirectory`/`dataSource`). Update env vars, CLI flags, YAML config keys, and Docker/compose setups accordingly.
* **query_engine:** `column = null` and `column <> null` filters are no longer supported and now return a query error. Use `column.isNull()` / `column.isNotNull()` instead.

### Features

* **query_engine:** add `<>` operator ([#1501](https://github.com/GenSpectrum/LAPIS-SILO/issues/1501)) ([61b0dc5](https://github.com/GenSpectrum/LAPIS-SILO/commit/61b0dc52492cac562e0c21843631d60104b6cac2))
* **query_engine:** add comparison operators (`<`,`>`,`<=`,`>=`) to SaneQL ([#1495](https://github.com/GenSpectrum/LAPIS-SILO/issues/1495)) ([5351116](https://github.com/GenSpectrum/LAPIS-SILO/commit/53511162b341a78c807637ecb18aa027706234ab))
* **query_engine:** disallow `column = null` filters ([#1504](https://github.com/GenSpectrum/LAPIS-SILO/issues/1504)) ([77b23a5](https://github.com/GenSpectrum/LAPIS-SILO/commit/77b23a58e62f43032fdd6a6254e28b8585e6268e))
* rename env variables and CLI arguments (SILO -&gt; RhyDb) ([#1511](https://github.com/GenSpectrum/LAPIS-SILO/issues/1511)) ([f71a3a6](https://github.com/GenSpectrum/LAPIS-SILO/commit/f71a3a624c3398c8dce8a117797c2e4042efb67a))
* **rhydb:** return ISO week date string from isoWeek() ([81e17a3](https://github.com/GenSpectrum/LAPIS-SILO/commit/81e17a32b62f8744ebc71f843bb9cce9c4415342))
* **roaring_util:** add container-level set-algebra operators ([#1491](https://github.com/GenSpectrum/LAPIS-SILO/issues/1491)) ([70a1bdd](https://github.com/GenSpectrum/LAPIS-SILO/commit/70a1bdd9170a07e7f1caee69c6cfa3a022787863))
* **storage:** implement int64 column ([#1448](https://github.com/GenSpectrum/LAPIS-SILO/issues/1448)) ([509820b](https://github.com/GenSpectrum/LAPIS-SILO/commit/509820b59f63ea63144b19412a990106abbea0fb))

## [0.13.3](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.13.2...v0.13.3) (2026-08-12)


### Features

* **python:** change pypi package name to `rhydb` ([c4e5e63](https://github.com/GenSpectrum/LAPIS-SILO/commit/c4e5e637f271d75d469c9314a91eb6118012492c))

## [0.13.2](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.13.1...v0.13.2) (2026-08-06)


### Features

* **silo:** add opt-in N-way clustered ingestion buffering ([eae375f](https://github.com/GenSpectrum/LAPIS-SILO/commit/eae375f9f20c65c79fbe15f9a107eeae806518d6))
* **silo:** materialize per-lineage relation tables during preprocessing ([3cafccb](https://github.com/GenSpectrum/LAPIS-SILO/commit/3cafccb72e091670ed3a896ed06af142c9dba986))


### Bug Fixes

* **query_engine:** do not invalidly reorder filter nodes ([#1431](https://github.com/GenSpectrum/LAPIS-SILO/issues/1431)) ([be8e0d7](https://github.com/GenSpectrum/LAPIS-SILO/commit/be8e0d7a6bf86e31bb79a88d71f313c7bddb5883))


### Performance Improvements

* **silo:** add clustered-ingestion benchmark modelling amplicon coverage classes ([d68cd98](https://github.com/GenSpectrum/LAPIS-SILO/commit/d68cd9862eca5f714bc0ea3b1cbca6176bd2dcfd))
* **silo:** process input sequences word-at-a-time ([b92fbb0](https://github.com/GenSpectrum/LAPIS-SILO/commit/b92fbb05fbf5d4f138887ee3e236617e2c631d51))

## [0.13.1](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.13.0...v0.13.1) (2026-07-31)


### Features

* **app:** add the plan Ordering as an http header to the response ([4b501d5](https://github.com/GenSpectrum/LAPIS-SILO/commit/4b501d562cb2e4f763ef7dc9767c299afaea58cf))
* **query_engine:** add a specialized OrderByWithLimit node ([34d3814](https://github.com/GenSpectrum/LAPIS-SILO/commit/34d38148611ba68e5ecac1ae461912b7412b3d62))
* **query_engine:** implement free IUs for boolean expressions ([#1377](https://github.com/GenSpectrum/LAPIS-SILO/issues/1377)) ([f366c81](https://github.com/GenSpectrum/LAPIS-SILO/commit/f366c811e9d10daf407554928b34c24ab1a7a87e))


### Bug Fixes

* **silo:** allow `limit` on `groupBy` ([035e244](https://github.com/GenSpectrum/LAPIS-SILO/commit/035e2442d6944936a9f3a73c8aee39c4c24f32e6))

## [0.13.0](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.12.1...v0.13.0) (2026-07-29)


### ⚠ BREAKING CHANGES

* **silo:** Insertions response: removed `insertion` field. Mutations response: removed `mutation` field. The string was constructed from information that is still present in the response.
* **preprocessing:** the database config options `defaultNucleotideSequence` and `defaultAminoAcidSequence` have been removed. `sequenceName` is now required on all nucleotide and amino acid filters (`nucleotideEquals`/`aminoAcidEquals`, `hasMutation`/`hasAAMutation`, `insertionContains`/`aminoAcidInsertionContains`, `symbolInSet` and the mutation profile filters).

### Features

* **app:** add a --version command to the silo binary ([8b3146e](https://github.com/GenSpectrum/LAPIS-SILO/commit/8b3146ec977f5fd643acb41b494ac381f085e1e3))
* **preprocessing:** remove default sequence names from database config ([#1386](https://github.com/GenSpectrum/LAPIS-SILO/issues/1386)) ([b8712ec](https://github.com/GenSpectrum/LAPIS-SILO/commit/b8712ec2fbee7b7df374c9091019858adb8bed46))
* **query_engine:** add join operator ([f7cbaf4](https://github.com/GenSpectrum/LAPIS-SILO/commit/f7cbaf480336399f6d07e25b4383d797210bef0d))
* **silo:** allow updating of string columns ([818a737](https://github.com/GenSpectrum/LAPIS-SILO/commit/818a737a0b0d8d8df86cd27447d695d0722e93b3))
* **silo:** remove `insertion` and `mutation` field from responses ([#1408](https://github.com/GenSpectrum/LAPIS-SILO/issues/1408)) ([72fca9f](https://github.com/GenSpectrum/LAPIS-SILO/commit/72fca9fd660733914b84cf2c3c322f3003e5da7d))

## [0.12.1](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.12.0...v0.12.1) (2026-07-27)


### Features

* **performance:** add co-occurrence query benchmark ([2c52241](https://github.com/GenSpectrum/LAPIS-SILO/commit/2c522413965259591d76925c778a22453147da9f))
* **query_engine:** bitmap-based aggregation for count group-bys ([a81fd24](https://github.com/GenSpectrum/LAPIS-SILO/commit/a81fd2422d07f555949b1fe4163ad01c7389a926))
* **SaneQL:** pipeline optimization step: map pull up through filter ([#1354](https://github.com/GenSpectrum/LAPIS-SILO/issues/1354)) ([b1966c4](https://github.com/GenSpectrum/LAPIS-SILO/commit/b1966c47bc68ab5ba66a830e32da772991dc67cd))
* **silo:** add update function to python bindings ([6e27cd5](https://github.com/GenSpectrum/LAPIS-SILO/commit/6e27cd52192014ab0856a05615cad25d820972c0))
* **silo:** add WebAssembly build ([#1320](https://github.com/GenSpectrum/LAPIS-SILO/issues/1320)) ([f41993a](https://github.com/GenSpectrum/LAPIS-SILO/commit/f41993a68f3d1f347d05759b4147b00cbcde1038))


### Bug Fixes

* **silo:** sort nulls consistently as smallest value ([00420f3](https://github.com/GenSpectrum/LAPIS-SILO/commit/00420f399cc4251aa0a40ba6198cc65dcc31ff5f))


### Performance Improvements

* **silo:** always prime Selection with most-selective predicate ([a8b458e](https://github.com/GenSpectrum/LAPIS-SILO/commit/a8b458e621cefa4f9556b4f4a5e480064b0263eb))
* **silo:** when adapting references, compute the coverage bitmaps only when necessary ([8f178dc](https://github.com/GenSpectrum/LAPIS-SILO/commit/8f178dc221081d88c4161a24e480251160ab101d))

## [0.12.0](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.11.3...v0.12.0) (2026-07-15)


### ⚠ BREAKING CHANGES

* **preprocessing:** remove deprecated preprocessing config values `intermediateResultsDirectory`, `preprocessingDatabaseLocation`, `duckdbMemoryLimitInG` and database config values `dateToSortBy`, `partitionBy`
* **silo:** use date32 type for dates instead of converting to string
* **silo:** change query interface to saneql

### Features

* add `unionAll` operator to SaneQL queries ([#1305](https://github.com/GenSpectrum/LAPIS-SILO/issues/1305)) ([53da65f](https://github.com/GenSpectrum/LAPIS-SILO/commit/53da65fa816e3c58029c98dff4b2ad8ccf0f1c35))
* **preprocessing:** remove deprecated preprocessing config and database config values ([#1287](https://github.com/GenSpectrum/LAPIS-SILO/issues/1287)) ([fbe4014](https://github.com/GenSpectrum/LAPIS-SILO/commit/fbe4014000651f67f86cb42dd462fb59c40f4b4d))
* **SaneQL:** add `schema` operator to get query result schema ([#1333](https://github.com/GenSpectrum/LAPIS-SILO/issues/1333)) ([f61955e](https://github.com/GenSpectrum/LAPIS-SILO/commit/f61955efc0e583ed263886be01b5c231538807e2))
* **silo:** add a dedicated filter-pushdown-pass and move node resolution to own file ([2da2f48](https://github.com/GenSpectrum/LAPIS-SILO/commit/2da2f483f2fdb18f881f968c7ee61aca2019f855))
* **silo:** add a references to input fields in `.map(...)` expressions ([2ce79f2](https://github.com/GenSpectrum/LAPIS-SILO/commit/2ce79f20ae45065308f6d1a0297875325784ea01))
* **silo:** add an at expression ([82f98b4](https://github.com/GenSpectrum/LAPIS-SILO/commit/82f98b4353f3bec217848f8eb72d0631693ee9ca))
* **silo:** add column narrowing to query planner ([90071f3](https://github.com/GenSpectrum/LAPIS-SILO/commit/90071f384750da77ba16576b36c81400ffa5713a))
* **silo:** change query interface to saneql ([daba3ea](https://github.com/GenSpectrum/LAPIS-SILO/commit/daba3eaf903f7b4bab56fd805c4619a984a73d8b))
* **silo:** log the query plan and optimization steps ([50e3e17](https://github.com/GenSpectrum/LAPIS-SILO/commit/50e3e17d703be2ff7e1867699437970840972269))
* **silo:** organize string columns in immutable chunks ([b8922e4](https://github.com/GenSpectrum/LAPIS-SILO/commit/b8922e49f121146aa1925fd8a4a5c5550eedb17b))
* **silo:** use date32 type for dates instead of converting to string ([f907787](https://github.com/GenSpectrum/LAPIS-SILO/commit/f907787e86a39520d21ade243abdb68613a0f694))


### Bug Fixes

* **preprocessing:** ignore bracketed comments in Newick tree files ([#1268](https://github.com/GenSpectrum/LAPIS-SILO/issues/1268)) ([9d0e323](https://github.com/GenSpectrum/LAPIS-SILO/commit/9d0e323a14ed8b3bb708fe23a9cabc39c3327358))
* **silo:** disambiguate unary/binary minus in SaneQL ([#1266](https://github.com/GenSpectrum/LAPIS-SILO/issues/1266)) ([e1739ff](https://github.com/GenSpectrum/LAPIS-SILO/commit/e1739ff765c5a3922af0357c92602887d32b81c4))
* **silo:** null sequences should not match N symbol queries ([004bba4](https://github.com/GenSpectrum/LAPIS-SILO/commit/004bba4dcf4c5197f6035e841dba13d23cfa53f0))


### Performance Improvements

* **silo:** replace charToSymbol switch with a lookup table ([4395af5](https://github.com/GenSpectrum/LAPIS-SILO/commit/4395af5af7edbf5fa29f9f156d8727e79ee54a66))

## [0.11.3](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.11.2...v0.11.3) (2026-05-18)


### Features

* **silo:** add a MutationProfile filter ([2d49d9b](https://github.com/GenSpectrum/LAPIS-SILO/commit/2d49d9b708f22e16139c4f221f13aafc26e8f522))

## [0.11.2](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.11.1...v0.11.2) (2026-04-17)


### Features

* **silo:** add option to ignore unknown lineage values ([16e1389](https://github.com/GenSpectrum/LAPIS-SILO/commit/16e1389a39e81e0206a93e12ba9d3eaf7cfafabd))

## [0.11.1](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.11.0...v0.11.1) (2026-03-23)


### Features

* **silo:** allow sequence-column inputs to be zstd-compressed in base64 format ([1f2bbc5](https://github.com/GenSpectrum/LAPIS-SILO/commit/1f2bbc5aeefb586d25fc11396915987164103d31))


### Bug Fixes

* **silo:** let `N` (`X` for amino acids) also code for `-` ([a081630](https://github.com/GenSpectrum/LAPIS-SILO/commit/a081630ab382920c0805f72bfdc906bd5e433457))

## [0.11.0](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.10.1...v0.11.0) (2026-03-03)


### ⚠ BREAKING CHANGES

* **silo:** "" is no longer considered `null` for date columns

### Features

* **silo:** add a DateEquals expression ([9cf5fd4](https://github.com/GenSpectrum/LAPIS-SILO/commit/9cf5fd4db6b10d41b9b292b86d4222e022c5cce4))


### Bug Fixes

* **silo:** actually validate dates on insertion ([9e24c7b](https://github.com/GenSpectrum/LAPIS-SILO/commit/9e24c7b5367bf2e334bcc67abaf7d7600d33d367))

## [0.10.1](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.10.0...v0.10.1) (2026-02-27)


### Features

* **performance:** add performance benchmark for the many short-read filters ([f8ef7ab](https://github.com/GenSpectrum/LAPIS-SILO/commit/f8ef7ab0a0c2777f5dc27a5a32ae664bbe4903b5))
* **silo:** add verbosity argument to CLI interface ([0bc682d](https://github.com/GenSpectrum/LAPIS-SILO/commit/0bc682d10fb6a4f8e8602498c923b2fac383e1b7))

## [0.10.0](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.9.12...v0.10.0) (2026-02-22)


### ⚠ BREAKING CHANGES

* **silo:** SILO will return `null` instead of `NN...NN`/`XX...XX` for sequences that were `null` in the input. "" in the input is no longer considered `null` for columns with `type: string` and `generateIndex: true`

### Features

* **benchmarking:** remove keep-silo feature ([e1486ae](https://github.com/GenSpectrum/LAPIS-SILO/commit/e1486ae70a01450267f1b787c99ae2ac4f7dc5cb))
* **silo:** add null_bitmap for all columns to explicitly model NULL ([baef7d3](https://github.com/GenSpectrum/LAPIS-SILO/commit/baef7d3ade93b5889d13aa83a813287519d521d0))
* **silo:** improve IsNull expression to support `IndexedStringColumn`, `ZstdCompressedStringColumn` and `SequenceColumn` ([6e48abf](https://github.com/GenSpectrum/LAPIS-SILO/commit/6e48abfabae742a0df154b6484356fd9fccd10a0))

## [0.9.12](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.9.11...v0.9.12) (2026-02-19)


### Features

* **silo:** return query result as arrow IPC if the http header is set ([6044c73](https://github.com/GenSpectrum/LAPIS-SILO/commit/6044c7368d116a19283efa34c72b5593b7f82419))

## [0.9.11](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.9.10...v0.9.11) (2026-02-18)


### Bug Fixes

* **silo:** error if a user tries to insert invalid insertion positions ([ad7ef81](https://github.com/GenSpectrum/LAPIS-SILO/commit/ad7ef815948584c3dde980f58b81506a08bf2627))

## [0.9.10](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.9.9...v0.9.10) (2026-02-12)


### Features

* **silodb:** release python bindings as release artifacts ([1117dde](https://github.com/GenSpectrum/LAPIS-SILO/commit/1117ddeaa6b6a1afc11c3cf61b67bc5a21da4c13))

## [0.9.9](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.9.8...v0.9.9) (2026-02-11)


### Features

* **silo:** add a arrow query interface to the python bindings ([4b7df7e](https://github.com/GenSpectrum/LAPIS-SILO/commit/4b7df7e29dbad66c6b53b85429202f4dc5ad4ad9))
* **silo:** add python bindings ([bb7ca5a](https://github.com/GenSpectrum/LAPIS-SILO/commit/bb7ca5a9610dd80676f3cc78ebe646a8765d4d2f))

## [0.9.8](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.9.7...v0.9.8) (2026-02-04)


### Bug Fixes

* **silo:** avoid race-condition in plan clean-up leading to crashes ([0685ad8](https://github.com/GenSpectrum/LAPIS-SILO/commit/0685ad8fa8f35b2108a25314d29777938995a022))

## [0.9.7](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.9.6...v0.9.7) (2026-01-30)


### Features

* add compiled binaries for linux to GitHub releases ([#1129](https://github.com/GenSpectrum/LAPIS-SILO/issues/1129)) ([c7dd359](https://github.com/GenSpectrum/LAPIS-SILO/commit/c7dd35914c6024a7d5f5d2cbd48af5d8beb9aaf9)), closes [#575](https://github.com/GenSpectrum/LAPIS-SILO/issues/575)
* **ci:** add a ci formatting job for workflow/*.yml ([a67ed2c](https://github.com/GenSpectrum/LAPIS-SILO/commit/a67ed2cb0584ebc72f0f20f5fb1df4a81cacdde3))

## [0.9.6](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.9.5...v0.9.6) (2026-01-28)


### Features

* **performance-tests:** add benchmark for nested ors ([34d6986](https://github.com/GenSpectrum/LAPIS-SILO/commit/34d698670a7c227a1aec6a6e41039b85f64ef9e7))
* **silo:** add a StringInSet expression to filter for many strings ([b67fd5a](https://github.com/GenSpectrum/LAPIS-SILO/commit/b67fd5a40bfc54b1d490e9e132d8b96bb943b2ad))
* **silo:** add an IsNull filter ([3437b3d](https://github.com/GenSpectrum/LAPIS-SILO/commit/3437b3d12f478cb21e54a8bb47a1cad14611a1e8))
* **silo:** improve rewriting of nested Or expressions ([24b27a4](https://github.com/GenSpectrum/LAPIS-SILO/commit/24b27a4631c133dfdac28ba2fcf1b22e214e7955))
* **silo:** rewrite StringEquals and StringInSet expressions during optimization ([23553ae](https://github.com/GenSpectrum/LAPIS-SILO/commit/23553ae09e86c8bc55052878dcc6fff8ac3562d2))
* **test:** add performance benchmark for many StringEquals filters ([579e6c6](https://github.com/GenSpectrum/LAPIS-SILO/commit/579e6c68d1ba7564dccb07e3b003a904952332cb))

## [0.9.5](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.9.4...v0.9.5) (2026-01-22)


### Features

* **build:** change builder base image to debian ([9e07f48](https://github.com/GenSpectrum/LAPIS-SILO/commit/9e07f483b7187878bf6da960e585a0c244790e66))


### Bug Fixes

* **silo:** correctly adapt local reference in incremental appends ([fa2ef25](https://github.com/GenSpectrum/LAPIS-SILO/commit/fa2ef25102e5a7063795f7f8760fe69264b7c31d))

## [0.9.4](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.9.3...v0.9.4) (2026-01-15)


### Bug Fixes

* **legacyNdjsonTransformer:** correctly parse floats with serde_json by setting the feature flag `float_roundtrip` ([8d38b27](https://github.com/GenSpectrum/LAPIS-SILO/commit/8d38b2739524cca52857d9d09ff05e9373cea4df))
* **silo:** do not use mimalloc on Mac ([6d78de8](https://github.com/GenSpectrum/LAPIS-SILO/commit/6d78de8e935c6341c95b8fd0f011a9c899005282))
* **silo:** only link mimalloc into the final executable, not the library, which might be included differently ([51fa4dc](https://github.com/GenSpectrum/LAPIS-SILO/commit/51fa4dc9ecb092684a91f1fa9f9d904d52c03476))

## [0.9.3](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.9.2...v0.9.3) (2025-12-16)


### Features

* **benchmarking:** add optional `QUERIES` custom variable ([772cd36](https://github.com/GenSpectrum/LAPIS-SILO/commit/772cd36cc6e860d85939f790a42f074ff8586719))
* **benchmarking:** allow avoidance of preprocessing by setting `OUTPUT_DIR` ([74c8b84](https://github.com/GenSpectrum/LAPIS-SILO/commit/74c8b844204d10a0483d141c7a05ac0d7972a839))
* **benchmarking:** optionally store the produced binaries ([7bb482c](https://github.com/GenSpectrum/LAPIS-SILO/commit/7bb482c6fde55847701d9d9743cb520c047d60bb)), closes [#1087](https://github.com/GenSpectrum/LAPIS-SILO/issues/1087)
* **benchmarking:** update api-query to ignore empty ignore regex files ([f572701](https://github.com/GenSpectrum/LAPIS-SILO/commit/f5727015da4a4097bfb71ae711bf3e509cb04d84))
* **silo:** add function to dynamically create tables in a silo instance ([b70f1df](https://github.com/GenSpectrum/LAPIS-SILO/commit/b70f1df863210d0467289f48e481179b54674ba9))
* **silo:** use mimalloc as the default allocator ([2203db9](https://github.com/GenSpectrum/LAPIS-SILO/commit/2203db9c09ac507210e66c4386ee2bd076de6440))


### Bug Fixes

* **benchmarking:** accept differences in HTTP error responses ([df3e18b](https://github.com/GenSpectrum/LAPIS-SILO/commit/df3e18b5203bc9b9916e82beaef9303edd92c8fd))
* **benchmarking:** set a timeout waiting for SILO to start ([f50e38d](https://github.com/GenSpectrum/LAPIS-SILO/commit/f50e38d4e0b3b24dee9aa09ff9af18af3a81eeb0))
* **benchmarking:** set default for `QUERIES` env var, not just `QUERIES_DIR` ([20ad729](https://github.com/GenSpectrum/LAPIS-SILO/commit/20ad729da0a48fa98125c48f26945116c5919eff))

## [0.9.2](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.9.1...v0.9.2) (2025-12-03)


### Features

* **benchmarking:** add DEBUG custom variable for running debug builds ([541afc8](https://github.com/GenSpectrum/LAPIS-SILO/commit/541afc89c95bc37e78c6e64d38c3e410d2897c9d))
* **benchmarking:** update api-query, enable CSV log file w/ checksum comparisons ([3d135d0](https://github.com/GenSpectrum/LAPIS-SILO/commit/3d135d0222d594013c9da27017c015d4ef7bf99b))
* **build:** add Makefile rule to generate conanprofile ([1a8a334](https://github.com/GenSpectrum/LAPIS-SILO/commit/1a8a334f2ca865545b4d24f7b1d0958d2f63b63f))
* **build:** tag images with branch name again ([670817c](https://github.com/GenSpectrum/LAPIS-SILO/commit/670817c3f92a902033e8dcae8dcff18798449a4d))
* **ci:** add CI workflow that also runs the test suite with debug build ([9bdc52b](https://github.com/GenSpectrum/LAPIS-SILO/commit/9bdc52b53aa2a9ae7200a0ec8d2779078542120f))
* **documentation:** add documentation for sequence storage format ([47e4081](https://github.com/GenSpectrum/LAPIS-SILO/commit/47e40813a2739cda181c02918b084117a5e6e75f))
* **silo:** add efficient way of computing vertical coverage bitmaps and use them to adapt reference positions ([8e7e9f4](https://github.com/GenSpectrum/LAPIS-SILO/commit/8e7e9f4045717c7bd29ea3860c486fa591ac9b73))
* **silo:** add reordering of predicates to apply most selective predicates first ([1db8cf0](https://github.com/GenSpectrum/LAPIS-SILO/commit/1db8cf0fbee73080c09c29b45826b6f507b4559b))
* **silo:** better compression for sequences ([9eb69c7](https://github.com/GenSpectrum/LAPIS-SILO/commit/9eb69c7d5dc62e24677fc0d23338dea88a5e347e))
* **silo:** change IsInCoveredRegion to predicate instead of being its own filter-operator ([6fef558](https://github.com/GenSpectrum/LAPIS-SILO/commit/6fef558eda987ebeb3b439af3dce682a701a0a08))
* **silo:** enable rewrite of SymbolInSet expressions under Or ([5489117](https://github.com/GenSpectrum/LAPIS-SILO/commit/54891178bbb647c27015a719459cb3bb15e3b597))
* **silo:** split compile function into separate rewrite step ([27b7e7c](https://github.com/GenSpectrum/LAPIS-SILO/commit/27b7e7cf8080a9fca8737dc8a39108867e019d29))


### Bug Fixes

* **build:** correctly fix the cppstd for installing the arrow dependency ([0c4aed3](https://github.com/GenSpectrum/LAPIS-SILO/commit/0c4aed3e5182fbaaff379c553327a139ad86ef2d))
* **build:** fix Makefile to not repeatedly invoke `conan install` ([346e6a9](https://github.com/GenSpectrum/LAPIS-SILO/commit/346e6a9f02d9f2ce1a28319a184318cd2ac01534))
* **silo:** do not add empty coverage bitmaps ([5bd32c7](https://github.com/GenSpectrum/LAPIS-SILO/commit/5bd32c7e3ca32cfcbc95b196784b82066999bfeb))
* **silo:** remove memory leak in sequence storage ([1da9ecd](https://github.com/GenSpectrum/LAPIS-SILO/commit/1da9ecd244f8df1682e984c9f330368240e6b777))

## [0.9.1](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.9.0...v0.9.1) (2025-10-28)


### Features

* **benchmarking:** support versioned datasets ([#1031](https://github.com/GenSpectrum/LAPIS-SILO/issues/1031)) ([4a7399b](https://github.com/GenSpectrum/LAPIS-SILO/commit/4a7399b4147fbd45f51b95c14ab721dcd6ee337e)), closes [#1018](https://github.com/GenSpectrum/LAPIS-SILO/issues/1018)
* **performance:** add performance benchmark subdirectory with a benchmark on mutation queries ([23af5c1](https://github.com/GenSpectrum/LAPIS-SILO/commit/23af5c1e8d4bbe60633c474cc7bf5aca7860e1f9))
* **silo:** add amino acid codes J, O, U and clean some code ([ccf15ed](https://github.com/GenSpectrum/LAPIS-SILO/commit/ccf15ed862a8df1d4f747984857f92845252995c))
* **silo:** add template to run ppx instances ([#1019](https://github.com/GenSpectrum/LAPIS-SILO/issues/1019)) ([129b428](https://github.com/GenSpectrum/LAPIS-SILO/commit/129b42833e5ac832918cc1005ce538f8aaaf232d))
* **silo:** address memory leak concerns ([#1008](https://github.com/GenSpectrum/LAPIS-SILO/issues/1008)) ([7359eea](https://github.com/GenSpectrum/LAPIS-SILO/commit/7359eeaf89ebf1a5e1e5316047d5c1d475576ce3))
* **silo:** fix clang-tidy configs (no comment allowed in checks), error on warnings in CMake clang-tidy option ([#1009](https://github.com/GenSpectrum/LAPIS-SILO/issues/1009)) ([d5bbaba](https://github.com/GenSpectrum/LAPIS-SILO/commit/d5bbaba6fbe93a6af7751e562ec62dcf8a35f319))
* **silo:** improve error message if primary key is of type lineage ([#1030](https://github.com/GenSpectrum/LAPIS-SILO/issues/1030)) ([a861141](https://github.com/GenSpectrum/LAPIS-SILO/commit/a86114177c3e5215fc4c36927ced8e83eebe9483))


### Bug Fixes

* **e2e:** remove obsolete version tag in docker compose files leading to warning ([5beabad](https://github.com/GenSpectrum/LAPIS-SILO/commit/5beabade64f6568293f60d197497067e3e974c63))
* **silo:** correctly error when requested insertion positions are larger than reference sequence size ([befbc76](https://github.com/GenSpectrum/LAPIS-SILO/commit/befbc76594358017f1c5b040fe75d3c9792968d8))
* **silo:** optimize check for unique keys ([4738a27](https://github.com/GenSpectrum/LAPIS-SILO/commit/4738a2729075893c1abdc639c870d905aaa8d8e0))

## [0.9.0](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.8.6...v0.9.0) (2025-10-03)


### ⚠ BREAKING CHANGES

* **silo:** Multiple breaking changes to lineage configuration:
    - PreprocessingConfig: `lineageDefinitionsFilename` renamed to
      `lineageDefinitionFilenames` and changed from string to list
    - DatabaseConfig: `generateLineageIndex` changed from boolean to string
      (expects lineage definition filename)

### Features

* **silo:** support multiple lineage systems ([f4be635](https://github.com/GenSpectrum/LAPIS-SILO/commit/f4be63507cfb656e773c4cc2d44b4537319312f0))

## [0.8.6](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.8.5...v0.8.6) (2025-09-26)


### Features

* improve logging in the query_plan ([bd2b494](https://github.com/GenSpectrum/LAPIS-SILO/commit/bd2b494687cdd17bdc043c15ebe83be094e871d6))
* **silo:** add a health endpoint that checks whether the api is able to answer requests ([5422ff1](https://github.com/GenSpectrum/LAPIS-SILO/commit/5422ff13dc6132c4bd6fc3388304b6e00831241b))
* **silo:** add throttled reslicing of ExecBatches before zstd decompression ([8ac2859](https://github.com/GenSpectrum/LAPIS-SILO/commit/8ac285982890eb4ed84cc582e3ddd476d0be0b97))
* **silo:** remove TBB dependency ([#985](https://github.com/GenSpectrum/LAPIS-SILO/issues/985)) ([91ec897](https://github.com/GenSpectrum/LAPIS-SILO/commit/91ec89736f92ef5f2a9982e4c2643ef004619924)), closes [#873](https://github.com/GenSpectrum/LAPIS-SILO/issues/873)
* update arrow to version 21.0.0 ([478366e](https://github.com/GenSpectrum/LAPIS-SILO/commit/478366e957c5fa69857ae2ac2647055396bfe316))


### Bug Fixes

* improvements of the arrow plan teardown after a query finishes ([223e5c6](https://github.com/GenSpectrum/LAPIS-SILO/commit/223e5c68f32167b2746c99038835adf2a817551c))
* **test:** properly free request handlers in tests ([0f9e0d9](https://github.com/GenSpectrum/LAPIS-SILO/commit/0f9e0d9d16f5b778f8e11f5c8248c6afcc0322c4))

## [0.8.5](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.8.4...v0.8.5) (2025-09-09)


### Features

* **silo:** add possibility to disable generation of unaligned sequence columns in schema ([b3bf856](https://github.com/GenSpectrum/LAPIS-SILO/commit/b3bf856399d3bbb0a6793ec3115c3ffc3635f502))
* **silo:** add possibility to specify offset in the input ndjson ([bf3a1d9](https://github.com/GenSpectrum/LAPIS-SILO/commit/bf3a1d93d9869fb034240358b32deaba3bfc34d8))
* **silo:** also allow lower-case characters as input in sequences ([97bba59](https://github.com/GenSpectrum/LAPIS-SILO/commit/97bba59247add0cffa9dae879a9e9100af10259b))

## [0.8.4](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.8.3...v0.8.4) (2025-09-04)


### Features

* bump version manually to release major refactor separately ([0a06315](https://github.com/GenSpectrum/LAPIS-SILO/commit/0a0631502af9c1474ce8b0611f9b345f70f77b8d))

## [0.8.3](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.8.2...v0.8.3) (2025-09-02)


### Features

* default to using number of threads equal to processors ([9795d28](https://github.com/GenSpectrum/LAPIS-SILO/commit/9795d28337ae61375965e31e2da2ad3c029c7458))

## [0.8.2](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.8.1...v0.8.2) (2025-08-28)


### Features

* increase default concurrent connections to 256 and number of worker threads to 8 ([987717c](https://github.com/GenSpectrum/LAPIS-SILO/commit/987717c9e16f63062c7326cbf3f7dac72d85d45b))

## [0.8.1](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.8.0...v0.8.1) (2025-08-13)


### Features

* add a default timeout of 2 minutes between sending response batches of requests ([f5effef](https://github.com/GenSpectrum/LAPIS-SILO/commit/f5effefbfcb219f309a0979c1a81d2c9bc676bd3))
* add mrcaParent and mrcaDepth to response of MostRecentCommonAncestor queries ([f49177b](https://github.com/GenSpectrum/LAPIS-SILO/commit/f49177bd10a8d0af6f64d70ed18b671c187cbd94))

## [0.8.0](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.7.11...v0.8.0) (2025-08-11)


### ⚠ BREAKING CHANGES

* the schema of the expected ndjson file changed. Pass all data flat, and aligned sequence data as a json object together with corresponding insertions. SILO no longer appends "unaligned_" internally when looking for fields in the ndjson

### Features

* use simdjson as json parser during preprocessing ([542d231](https://github.com/GenSpectrum/LAPIS-SILO/commit/542d231973fe7459d9cf9aa15e54c980332babdc))

## [0.7.11](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.7.10...v0.7.11) (2025-08-11)


### Features

* **silo:** include null values in phyloTree column in missingNodeCount ([#936](https://github.com/GenSpectrum/LAPIS-SILO/issues/936)) ([8f121cf](https://github.com/GenSpectrum/LAPIS-SILO/commit/8f121cfec8589b4dcbaf4104e9d41d026c58045d))

## [0.7.10](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.7.9...v0.7.10) (2025-08-07)


### Bug Fixes

* bump the silo serialization version ([d7adad5](https://github.com/GenSpectrum/LAPIS-SILO/commit/d7adad51afe567a96b22f3dd9c6f149d9f9eadbc))

## [0.7.9](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.7.8...v0.7.9) (2025-08-07)


### Features

* **benchmarking:** add a separate target for the preprocessing ([5c18daf](https://github.com/GenSpectrum/LAPIS-SILO/commit/5c18dafdfd99eb98453da10f895b38deca251064)), closes [#919](https://github.com/GenSpectrum/LAPIS-SILO/issues/919)


### Bug Fixes

* **benchmarking:** check for the presence of the BENCH_OUTPUT_LOG var ([d04789b](https://github.com/GenSpectrum/LAPIS-SILO/commit/d04789bbe4f14b2e525e78ea1fa916ebd263a423))
* **ci:** do not cancel in-progress runs, which could lead to non-generation of released images ([32a9b83](https://github.com/GenSpectrum/LAPIS-SILO/commit/32a9b83ba1b22d867dd20af147889e38215bed25))
* throw an error when a user requests a limit without specifying ordering ([3ad8dfe](https://github.com/GenSpectrum/LAPIS-SILO/commit/3ad8dfe8670514b1029c060797a41e7c6f215d96))
* validate a arrow query plan before executing it to catch errors that might lead to dying requests ([c49a698](https://github.com/GenSpectrum/LAPIS-SILO/commit/c49a6981dce34a934e9818f0392a8bd23e6a6472))

## [0.7.8](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.7.7...v0.7.8) (2025-08-01)


### Features

* **silo:** store branch lengths and export them in subtree newick ([#918](https://github.com/GenSpectrum/LAPIS-SILO/issues/918)) ([624f248](https://github.com/GenSpectrum/LAPIS-SILO/commit/624f248adc3da807ac75bcee84fdba6227c7dad2))

## [0.7.7](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.7.6...v0.7.7) (2025-07-31)


### Features

* **silo:** further optimizations of queries using the phylo tree - use hash sets instead of vectors ([#917](https://github.com/GenSpectrum/LAPIS-SILO/issues/917)) ([8164013](https://github.com/GenSpectrum/LAPIS-SILO/commit/816401342d2e4d7b26c11a71173a0ab8441247ce))

## [0.7.6](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.7.5...v0.7.6) (2025-07-29)


### Features

* **silo:** add an endpoint to export a newick of the nodes in the filter ([#911](https://github.com/GenSpectrum/LAPIS-SILO/issues/911)) ([0bb94b7](https://github.com/GenSpectrum/LAPIS-SILO/commit/0bb94b7434ceb48232146d7116cc2306138a75a4))
* **silo:** optimize phyloSubtree action ([#916](https://github.com/GenSpectrum/LAPIS-SILO/issues/916)) ([7c218d4](https://github.com/GenSpectrum/LAPIS-SILO/commit/7c218d487a9ca89983a08f92878c6a11e598112f))
* **silo:** refactor config - rename `phyloTreeNodeIdentifier` to `isPhyloTreeField` ([#921](https://github.com/GenSpectrum/LAPIS-SILO/issues/921)) ([3085059](https://github.com/GenSpectrum/LAPIS-SILO/commit/30850594d157d6f21ef6a39e28e82e8a470c8a78))

## [0.7.5](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.7.4...v0.7.5) (2025-07-23)


### Features

* **benchmarking:** add benchmarking probes to preprocessing ([8fbebe1](https://github.com/GenSpectrum/LAPIS-SILO/commit/8fbebe11cb0fc115cdfe96a32363cc4a01aeb597))
* **benchmarking:** add probes to "action" part of query evaluation ([f63eb41](https://github.com/GenSpectrum/LAPIS-SILO/commit/f63eb41b61dde69fd11f868d5a375cdaf166f821))
* **benchmarking:** add query type to benchmark context before query execution ([e62677c](https://github.com/GenSpectrum/LAPIS-SILO/commit/e62677c63bef5dc2dcc8e28764e7f8faca43acdd))
* **benchmarking:** hack: save evobench log file from preprocessing phase ([5193ae8](https://github.com/GenSpectrum/LAPIS-SILO/commit/5193ae8a6bc4bdf703b1606dcc866bb99dd2316b))
* **benchmarking:** optimize & add possibility to pass constexpr string variables ([3e7a41b](https://github.com/GenSpectrum/LAPIS-SILO/commit/3e7a41b243e45b95e80a06e2fda7511a67a5ca5b))
* **benchmarking:** update api-query, add REPEAT, CONCURRENCY vars ([4a063d8](https://github.com/GenSpectrum/LAPIS-SILO/commit/4a063d83d548aa99928d560c3bb57a48b5405eb8))
* **silo:** optimize ndjson sink for higher download speeds ([4c97982](https://github.com/GenSpectrum/LAPIS-SILO/commit/4c97982b31de07123f8cdd2a210cf299ad83eb86))


### Bug Fixes

* **benchmarking:** also prefix output dir name with $COMMIT_ID ([e18c881](https://github.com/GenSpectrum/LAPIS-SILO/commit/e18c8813f1aca534bf16fdd2046d7aa1eec4b41f))
* **benchmarking:** prefix output dir name with $DATASET ([df6405e](https://github.com/GenSpectrum/LAPIS-SILO/commit/df6405e6c2eb73fb14244615c8053862db2d7124))
* **build:** change format target to also include .hpp files ([7c90965](https://github.com/GenSpectrum/LAPIS-SILO/commit/7c9096503cd409a87c7c96e72c734b32a4cce207))
* project out the superfluous column instead of producing mismatching schema-ExecBatches ([53e3187](https://github.com/GenSpectrum/LAPIS-SILO/commit/53e3187c2acd7f730419712b795f10fe177c3249))
* properly designate the hash_count_all output as int64 in the output schema ([e04987c](https://github.com/GenSpectrum/LAPIS-SILO/commit/e04987c9e0055f6befda7d2612d30122e11a0356))

## [0.7.4](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.7.3...v0.7.4) (2025-07-21)


### Bug Fixes

* **silo:** accept more characters in newick string and improve error messages ([#891](https://github.com/GenSpectrum/LAPIS-SILO/issues/891)) ([5f8bc9e](https://github.com/GenSpectrum/LAPIS-SILO/commit/5f8bc9e5c2672089f16ea545766fae48fc31d391))
* **silo:** correctly deduplicate requested fields ([78f4146](https://github.com/GenSpectrum/LAPIS-SILO/commit/78f41460cfff10a182c1ec8712ea6c69c2f7eb1e))

## [0.7.3](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.7.2...v0.7.3) (2025-07-17)


### Features

* enable filtering for recombinant lineages with specifiable mode for including all recombinant sublineages or only the ones that are fully contained in the clade of the filtered node ([6e335dd](https://github.com/GenSpectrum/LAPIS-SILO/commit/6e335dda520c2e9c12d34165f2188a0daa5aa74f))


### Bug Fixes

* **benchmarking:** only depend on WisePulse for the tool to run, not for the data ([#875](https://github.com/GenSpectrum/LAPIS-SILO/issues/875)) ([1aa4dc9](https://github.com/GenSpectrum/LAPIS-SILO/commit/1aa4dc9661b7840a9ad35f35474886b752f7df5d))
* **silo:** handle newick files with new line at end ([#889](https://github.com/GenSpectrum/LAPIS-SILO/issues/889)) ([7f31fa7](https://github.com/GenSpectrum/LAPIS-SILO/commit/7f31fa75b8a3bb1501b1e30c65dd525b178bfc3c))

## [0.7.2](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.7.1...v0.7.2) (2025-07-08)


### Features

* add the field `additionalFields` to Fasta action ([#861](https://github.com/GenSpectrum/LAPIS-SILO/issues/861)) ([80777f1](https://github.com/GenSpectrum/LAPIS-SILO/commit/80777f188d58137d067a908a2f37420f497c466a))
* **benchmarking:** add benchmarking/ directory with benchmark runner code ([be93833](https://github.com/GenSpectrum/LAPIS-SILO/commit/be938338e30d2fc0547e6c15244dd6efef28f080))
* **benchmarking:** add copy of the evobench-probes library ([b73e414](https://github.com/GenSpectrum/LAPIS-SILO/commit/b73e4144876c3b3a91cf642208c4dfb375993568))
* **benchmarking:** add probe on query plan execution ([f2d96df](https://github.com/GenSpectrum/LAPIS-SILO/commit/f2d96df44244b7e6a69649166fec825a9b0868e1))
* **benchmarking:** add probes on all operator evaluate methods ([96b14c6](https://github.com/GenSpectrum/LAPIS-SILO/commit/96b14c6ae87ef57ca74ef97d1cec527173bdd85c))
* **benchmarking:** add probes on Date sort optimization ([44b61de](https://github.com/GenSpectrum/LAPIS-SILO/commit/44b61de097a6a5fd30ae3fb911fd17ef2315af55))
* **benchmarking:** re-use the TCP address/port immediately ([8d8940f](https://github.com/GenSpectrum/LAPIS-SILO/commit/8d8940f3077a606e447d938e7c211df73ae4a5b8))
* enable Backpressure when streaming batches ([3af7d04](https://github.com/GenSpectrum/LAPIS-SILO/commit/3af7d04d46b7febfd1534177122c3d75fc6d9d7d))
* show the error message when failing with a Poco::Net::NetException ([07c42b2](https://github.com/GenSpectrum/LAPIS-SILO/commit/07c42b24be8995585a8ec0a4c830937aa384dc26))
* **silo:** add MostRecentCommonAncestor action ([#834](https://github.com/GenSpectrum/LAPIS-SILO/issues/834)) ([157f186](https://github.com/GenSpectrum/LAPIS-SILO/commit/157f1868e608560009fd2c1f0f3f015333e4a081))


### Bug Fixes

* **benchmarking:** update location of sorted_input_file.* ([02daf40](https://github.com/GenSpectrum/LAPIS-SILO/commit/02daf405593f6da0fb715441bb8f94e4779b6adb))
* better error message if the DateBetween column is not of type date ([9b7fa99](https://github.com/GenSpectrum/LAPIS-SILO/commit/9b7fa9961e07cce58694bf5aba4441e405cdb67c))
* change default streaming batch size from 50000 to 32767 to avoid reslicing batches ([718fd22](https://github.com/GenSpectrum/LAPIS-SILO/commit/718fd22f9ea8abe01c2f29182718b37c121e0e43))
* improve error messages when ndjson file is invalid ([#850](https://github.com/GenSpectrum/LAPIS-SILO/issues/850)) ([dcff2e9](https://github.com/GenSpectrum/LAPIS-SILO/commit/dcff2e9278c1a12854680a8434e13665b82ff8b6))
* stop streaming response when network stream is interrupted ([d58d668](https://github.com/GenSpectrum/LAPIS-SILO/commit/d58d668e09e6d08ea6cb8eb4e01713a1053cc7fa))
* use arrow::acero::SourceNode when constructing a TableScan ([7035f1f](https://github.com/GenSpectrum/LAPIS-SILO/commit/7035f1f60258749e1568b2659d83aadf45d4e08e))

## [0.7.1](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.7.0...v0.7.1) (2025-07-02)


### Bug Fixes

* add proper error messages for invalid groupByFields ([2c8babb](https://github.com/GenSpectrum/LAPIS-SILO/commit/2c8babb68e7f628228581d4c30bdad83cff53b06))
* evaluate the bitmaps in the preparation of the QueryPlan to not send wrong http headers ([9c831c9](https://github.com/GenSpectrum/LAPIS-SILO/commit/9c831c95f45bb45ed013186db18db6051f2c7d38))
* make TableScan react to StopProducing calls ([#855](https://github.com/GenSpectrum/LAPIS-SILO/issues/855)) ([ea3ee7d](https://github.com/GenSpectrum/LAPIS-SILO/commit/ea3ee7df8438564ca559b12d0569b19741b86222))
* throw an error when silo tries to start on a port that is already in use ([e0ee23b](https://github.com/GenSpectrum/LAPIS-SILO/commit/e0ee23b9d1467a9e7f586ca183b5697ad60a9e92))

## [0.7.0](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.6.0...v0.7.0) (2025-06-30)


### ⚠ BREAKING CHANGES

* the field sequenceName in the Fasta and FastaAligned is now named sequenceNames and required to be an array ([#804](https://github.com/GenSpectrum/LAPIS-SILO/issues/804))
* change field sequenceName to sequenceNames in Insertions action ([#821](https://github.com/GenSpectrum/LAPIS-SILO/issues/821))
* change field sequenceName to sequenceNames in Mutations action ([#819](https://github.com/GenSpectrum/LAPIS-SILO/issues/819))
* rename DatabaseInfo field `totalSize` to `verticalBitmapsSize` and `nBitmapsSize` to `horizontalBitmapsSize` ([#805](https://github.com/GenSpectrum/LAPIS-SILO/issues/805))
* Null values are no longer ordered as the smallest element, but at the start or end instead. This is decided based on the sort-order of the first orderByField to mimic old behavior. This leads to a breaking change when ordering by multiple fields with mismatching order direction ([#799](https://github.com/GenSpectrum/LAPIS-SILO/issues/799)) 
* all unaligned nucleotide sequences are prefixed with `unaligned_` in the api requests and response ([#795](https://github.com/GenSpectrum/LAPIS-SILO/issues/795))

### Features

* streaming implementation of the SILO query engine based on the arrow framework ([#765](https://github.com/GenSpectrum/LAPIS-SILO/issues/765), [#775](https://github.com/GenSpectrum/LAPIS-SILO/issues/775), [#792](https://github.com/GenSpectrum/LAPIS-SILO/issues/792), [#795](https://github.com/GenSpectrum/LAPIS-SILO/issues/795), [#799](https://github.com/GenSpectrum/LAPIS-SILO/issues/799), [#809](https://github.com/GenSpectrum/LAPIS-SILO/issues/809), [#821](https://github.com/GenSpectrum/LAPIS-SILO/issues/821), [#829](https://github.com/GenSpectrum/LAPIS-SILO/issues/829), [#837](https://github.com/GenSpectrum/LAPIS-SILO/issues/837))
* cache DatabaseInfo, add amino acid sequence columns to size info ([bc0d0a2](https://github.com/GenSpectrum/LAPIS-SILO/commit/bc0d0a2210922bf712bf744d9f1d4bf889054541))
* **silo:** add function to get all clades that are descendants of an internal node ([#815](https://github.com/GenSpectrum/LAPIS-SILO/issues/815)) ([d1f9989](https://github.com/GenSpectrum/LAPIS-SILO/commit/d1f99894b603d4dd398fd890d48276b698c83661))
* **silo:** add recommended vscode extensions to .vscode folder for developers using vscode ([d2c74db](https://github.com/GenSpectrum/LAPIS-SILO/commit/d2c74db6e2fb00d98f687a0dbf958d378127718b))
* **silo:** improve the docs and update build_with_conan.py ([#779](https://github.com/GenSpectrum/LAPIS-SILO/issues/779)) ([f176fd6](https://github.com/GenSpectrum/LAPIS-SILO/commit/f176fd6c4e0a26e638a63f577bb90db49c89d9f4))
* **silo:** read in phylogenetic tree files and create tree structure ([#806](https://github.com/GenSpectrum/LAPIS-SILO/issues/806)) ([f3d3f36](https://github.com/GenSpectrum/LAPIS-SILO/commit/f3d3f36fea44f0d6b8727fdc66a8fed6501110e3))
* **silo:** refactor phylotree to use nodeId for children and parent and serialize ([#822](https://github.com/GenSpectrum/LAPIS-SILO/issues/822)) ([29e8361](https://github.com/GenSpectrum/LAPIS-SILO/commit/29e83611fa191e439894407222917d660f1aefb6))


### Bug Fixes

* avoid dropping the input bitmap of Selections in a nested And query ([e87093c](https://github.com/GenSpectrum/LAPIS-SILO/commit/e87093c4f0b254d526663bfe1658c1be3209a1ee))
* correctly error when the input data contains illegal insertion characters ([4867c32](https://github.com/GenSpectrum/LAPIS-SILO/commit/4867c320c495226add39a981c25287866bc49302))
* remove erroneous warning printed on every preprocessing run ([82b65c8](https://github.com/GenSpectrum/LAPIS-SILO/commit/82b65c8a91aebe80bc15453a2196be98501e672a))

## [0.6.0](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.5.9...v0.6.0) (2025-04-23)


### ⚠ BREAKING CHANGES

* the DatabaseConfig fields `dateToSortBy` and `partitionBy` have been deprecated and do no longer have any effect. The internal endpoint for more detailed database storage information has been removed.

### Features

* add `append` as a third silo execution mode for incremental preprocessing ([84c4ae3](https://github.com/GenSpectrum/LAPIS-SILO/commit/84c4ae3df0995a07ebec26c286bc8e17001ef60b)), closes [#368](https://github.com/GenSpectrum/LAPIS-SILO/issues/368)
* introduce a database schema that actually describes the schema of the database ([be71e08](https://github.com/GenSpectrum/LAPIS-SILO/commit/be71e08a5413b9bb3b2c9566cdaa54b246745a56))
* still enable `silo preprocessing` as a shorthand for initialize-then-append ([9a91d0e](https://github.com/GenSpectrum/LAPIS-SILO/commit/9a91d0e0512ecce904629cb940be78410eeba4b4))

## [0.5.9](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.5.8...v0.5.9) (2025-04-16)


### Bug Fixes

* return 503 response when no database has been loaded yet ([#742](https://github.com/GenSpectrum/LAPIS-SILO/issues/742)) ([79ebbaa](https://github.com/GenSpectrum/LAPIS-SILO/commit/79ebbaacd47c51e8466fd2253aaf4647d759ff3b)), closes [#723](https://github.com/GenSpectrum/LAPIS-SILO/issues/723)

## [0.5.8](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.5.7...v0.5.8) (2025-03-31)


### Bug Fixes

* the InsertionContains filter regex should match for the full string ([#735](https://github.com/GenSpectrum/LAPIS-SILO/issues/735)) ([4bb8320](https://github.com/GenSpectrum/LAPIS-SILO/commit/4bb832001ffa2a6c543607787c1dc59baa2614be))

## [0.5.7](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.5.6...v0.5.7) (2025-03-17)


### Bug Fixes

* correctly expect an escaped stop codon in requests ([9b3c27a](https://github.com/GenSpectrum/LAPIS-SILO/commit/9b3c27aa3f66b503671360fbf8ef2b74f877c3b9))

## [0.5.6](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.5.5...v0.5.6) (2025-02-27)


### Features

* add coverage field to Mutations output and make it possible to only return subset of the fields ([2b11472](https://github.com/GenSpectrum/LAPIS-SILO/commit/2b114722c09eae26ebbfee2203d8c210a9bfe639))

## [0.5.5](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.5.4...v0.5.5) (2025-02-25)


### Bug Fixes

* return proper error message when index is out of bounds for a HasMutations filter ([50e9f49](https://github.com/GenSpectrum/LAPIS-SILO/commit/50e9f49f562465eb32691388bc4c3de5d2644c55))

## [0.5.4](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.5.3...v0.5.4) (2025-02-20)


### Features

* add monitoring to log the swapover to datasets ([39f1ed2](https://github.com/GenSpectrum/LAPIS-SILO/commit/39f1ed2ddba4e4711ce850380674b8d41d645d52))
* call malloc_trim after all connections to the old database are finished ([5742a52](https://github.com/GenSpectrum/LAPIS-SILO/commit/5742a521b467d153a2dd633cdacfd7ec3b8080bb))
* tell malloc to give empty heap space back to the OS before every fasta call ([16ce850](https://github.com/GenSpectrum/LAPIS-SILO/commit/16ce850145db75d81f702368f3944692dd0da080))


### Bug Fixes

* prohibit allocation of new Poco threads for incoming requests ([7c2994f](https://github.com/GenSpectrum/LAPIS-SILO/commit/7c2994f515e8750eb5c43a882d76788b59dfc11c))

## [0.5.3](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.5.2...v0.5.3) (2025-02-06)


### Features

* add endpoint that returns the lineageDefinitions for the requested column ([#689](https://github.com/GenSpectrum/LAPIS-SILO/issues/689)) ([69cd9b0](https://github.com/GenSpectrum/LAPIS-SILO/commit/69cd9b0f5947d8b249a1c5431dd425e7f090e50f))

## [0.5.2](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.5.1...v0.5.2) (2025-01-29)


### Features

* add the version to the output of the info endpoint ([#680](https://github.com/GenSpectrum/LAPIS-SILO/issues/680)) ([87e3f60](https://github.com/GenSpectrum/LAPIS-SILO/commit/87e3f60c5c517a6ad1e33376886cb72e6fb9cd2c)), closes [#604](https://github.com/GenSpectrum/LAPIS-SILO/issues/604)

## [0.5.1](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.5.0...v0.5.1) (2025-01-28)


### Features

* update base image to ubuntu:24.04 ([46d0567](https://github.com/GenSpectrum/LAPIS-SILO/commit/46d056739f7b673ad6a2f3d9daa72f4514abe4e3))


### Bug Fixes

* return an error if reserved value INT32_MIN is used in filter ([042646b](https://github.com/GenSpectrum/LAPIS-SILO/commit/042646be296d7a8d68736ad3b0d8da6715f51c92))

## [0.5.0](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.4.0...v0.5.0) (2024-12-19)


### ⚠ BREAKING CHANGES

* new command line / config system

### Features

* add materializationCutoff option ([8920d5f](https://github.com/GenSpectrum/LAPIS-SILO/commit/8920d5ff7af6906aa55f4a5b034e11c631df2955))
* new command line / config system ([2a29074](https://github.com/GenSpectrum/LAPIS-SILO/commit/2a2907493fd8ce28d93fb73ccfdc205396a17e35)), closes [#633](https://github.com/GenSpectrum/LAPIS-SILO/issues/633)


### Bug Fixes

* declare virtual constructor for abstract base class CustomSqlFunction ([603ddf6](https://github.com/GenSpectrum/LAPIS-SILO/commit/603ddf6f23595de390dac4899411177f95fec022))

## [0.4.0](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.3.2...v0.4.0) (2024-11-14)


### ⚠ BREAKING CHANGES

* `metadataInputFile` key in preprocessing config file has been removed. Instead, ndjson files should be used and specified with the `ndjsonInputFilename` option

### Features

* remove tsv/fasta/sam input format ([#562](https://github.com/GenSpectrum/LAPIS-SILO/issues/562)) ([fd7dc6f](https://github.com/GenSpectrum/LAPIS-SILO/commit/fd7dc6f4628f7dc512a99daeb184bab3e05d4150))


### Bug Fixes

* **preprocessing:** correct error message when field is in config but not in metadata file ([828182a](https://github.com/GenSpectrum/LAPIS-SILO/commit/828182af791ea4da1fd39685f3366b9f36bacb02))
* remove shared_ptr in lambda captures which might lead to memory leaks ([a26d8b8](https://github.com/GenSpectrum/LAPIS-SILO/commit/a26d8b88146e5ca7f2841525c1d8c3b6bc93e87b))

## [0.3.2](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.3.1...v0.3.2) (2024-10-30)


### Bug Fixes

* **preprocessing**: add column_name to column classes and add them to error messages ([e011e5c](https://github.com/GenSpectrum/LAPIS-SILO/commit/e011e5c804ed11a32bb42ca57be8d304c22d4fe9)) ([#629](https://github.com/GenSpectrum/LAPIS-SILO/pull/629))

## [0.3.1](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.3.0...v0.3.1) (2024-10-25)


### Bug Fixes

* **preprocessing:** error on duplicate primary keys ([687c3ef](https://github.com/GenSpectrum/LAPIS-SILO/commit/687c3ef8803c3dee19b222bcb790bbc4fb33cabe))
* validate the column types earlier to prevent duckdb auto-casting to cast types with differing round-trip conversions ([ac238b2](https://github.com/GenSpectrum/LAPIS-SILO/commit/ac238b208a02a7b00bdab92ff3039562e2a1d704))

## [0.3.0](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.24...v0.3.0) (2024-10-16)


### ⚠ BREAKING CHANGES

* generalized wildcard queries ([#458](https://github.com/GenSpectrum/LAPIS-SILO/issues/458))
  * The preprocessing config field `pangoLineageDefinitionFilename` has been renamed to `lineageDefinitionFilename`.
  * We now accept a YAML lineage definition file instead of a Pango alias key.
  * Input and query validation now checks whether the provided lineage exists in the defined lineages, and errors are thrown if validation fails.
  * The metadata type `pangoLineage` has been removed. `type: string` with `generateLineageIndex: true` should be used instead.


### Features

* generalized wildcard queries ([#458](https://github.com/GenSpectrum/LAPIS-SILO/issues/458)) ([1432320](https://github.com/GenSpectrum/LAPIS-SILO/commit/143232056146405c1fbb391d22adfd271f699c69))


### Bug Fixes

* resolve aliases when inserting to or querying lineage indexes again ([0e841e6](https://github.com/GenSpectrum/LAPIS-SILO/commit/0e841e659d2c59677820d3b83a21f864c27d069a))
* update script to also generate aliases ([43ac4aa](https://github.com/GenSpectrum/LAPIS-SILO/commit/43ac4aa0ffd6939fafa6b1f56a8ec1e7190e0c22))

## [0.2.24](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.23...v0.2.24) (2024-10-15)


### Bug Fixes

* prevent setDatabase starvation which leads to no updates of currently loaded database ([24e4db6](https://github.com/GenSpectrum/LAPIS-SILO/commit/24e4db60b6da7d5e7b909818d230a740aa41ef74)) ([#613](https://github.com/GenSpectrum/LAPIS-SILO/pull/613))

## [0.2.23](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.22...v0.2.23) (2024-10-10)


### Bug Fixes

* **preprocessing:** enforce exact match on file base stem when pairing sequence and metadata when processing from file ([7c9b23d](https://github.com/GenSpectrum/LAPIS-SILO/commit/7c9b23d618a75c4455ed9c7ac4cd76cf1218251a)), closes [#607](https://github.com/GenSpectrum/LAPIS-SILO/issues/607)

## [0.2.22](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.21...v0.2.22) (2024-10-01)


### Bug Fixes

* correctly escape quotes in field names ([#595](https://github.com/GenSpectrum/LAPIS-SILO/issues/595)) ([7e7b448](https://github.com/GenSpectrum/LAPIS-SILO/commit/7e7b448670c2080bee62c876adb68e6e439e9da6))

## [0.2.21](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.20...v0.2.21) (2024-09-20)


### Bug Fixes

* **preprocessing:** resolves spurious OOM crashes when handling large datasets ([6e4eae2](https://github.com/GenSpectrum/LAPIS-SILO/commit/6e4eae283317b932e34008912997bfd6f4037c10))

## [0.2.20](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.19...v0.2.20) (2024-09-17)


### Features

* make duckdb memory limit configurable via preprocessing config ([c112eb1](https://github.com/GenSpectrum/LAPIS-SILO/commit/c112eb18976b86240ae23069bf11b7286e634d1f))

## [0.2.19](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.18...v0.2.19) (2024-09-12)


### Bug Fixes

* preprocessing: validate that either a ndjson or tsv file was given as input ([#564](https://github.com/GenSpectrum/LAPIS-SILO/issues/564)) ([7bf2ef7](https://github.com/GenSpectrum/LAPIS-SILO/commit/7bf2ef7fafb63fe2938cf846385ddc087290ca3b))

## [0.2.18](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.17...v0.2.18) (2024-09-12)


### Features

* do not abort on assertment failures ([4e5f598](https://github.com/GenSpectrum/LAPIS-SILO/commit/4e5f59879ce95c8925e15ca6df2f773d989e5c51))
* panic: add `ASSERT`, `UNREACHABLE`, `UNIMPLEMENTED`, safer env var ([867d08f](https://github.com/GenSpectrum/LAPIS-SILO/commit/867d08f4f8b068617b2102edb1ddb36fa6c585fc))
* update duckdb version ([577c1b2](https://github.com/GenSpectrum/LAPIS-SILO/commit/577c1b29d0dde43caf23227b518b7a1927e1f9be))


### Bug Fixes

* duplicate key in sample.ndjson ([c9b5232](https://github.com/GenSpectrum/LAPIS-SILO/commit/c9b523221a8a1ee20ce8830ce8105f2a248afd13))
* increase duckdb memory limit to 80GB ([20131bb](https://github.com/GenSpectrum/LAPIS-SILO/commit/20131bbd294f1da3683a58e0fb7e58cc72649bd5))
* order of randomized tests changed ([d29b2f2](https://github.com/GenSpectrum/LAPIS-SILO/commit/d29b2f267274db5708e857b22240c782a3fa2e80))

## [0.2.17](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.16...v0.2.17) (2024-08-30)


### Bug Fixes

* manually overwrite already existing unaligned sequence files in the temp folder ([f37fd88](https://github.com/GenSpectrum/LAPIS-SILO/commit/f37fd88dc36f0907e09881f6ca921c405a99cf43))
  
  * In case the unaligned sequence directory already existed in the temp folder when running preprocessing,
  it is manually deleted. Before, this lead to a bug, as the currently used duckdb version does
  not correctly overwrite the temp files and may leave duplicate entries
  in the output. This is fixed in duckdb PR [#11787](https://github.com/duckdb/duckdb/pull/11787) 
  which is included in release v0.10.3

## [0.2.16](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.15...v0.2.16) (2024-08-29)


### Bug Fixes

* do not order keys and sequences differently, leading to wrong unaligned sequence results when dateToSortBy is set ([b0f8352](https://github.com/GenSpectrum/LAPIS-SILO/commit/b0f8352814594e6171217ebf4f869b41d51453a3))
* do not return incorrect results when encountering a null value in the unaligned sequence action ([a69d117](https://github.com/GenSpectrum/LAPIS-SILO/commit/a69d117fdce2f8263d450b9646ab4432f09da304))
* fix the RIGHT JOIN in preprocessing of unaligned sequences to contain the correct primary key ([a25d5d0](https://github.com/GenSpectrum/LAPIS-SILO/commit/a25d5d0527e5cb2571c1d94c73c7d5b8ebcee3b6))

## [0.2.15](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.14...v0.2.15) (2024-08-23)


### Bug Fixes

* don't abort when unaligned sequence is missing - we should return `null` ([#542](https://github.com/GenSpectrum/LAPIS-SILO/issues/542)) ([2c649cb](https://github.com/GenSpectrum/LAPIS-SILO/commit/2c649cb324b2855204869a9cead42b03e6378fdb)), closes [#541](https://github.com/GenSpectrum/LAPIS-SILO/issues/541)

## [0.2.14](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.13...v0.2.14) (2024-08-19)


### Features

* new operator for matching strings against regular expressions ([ade25e8](https://github.com/GenSpectrum/LAPIS-SILO/commit/ade25e8b147914ff7383a0404981437d77956dd3))


### Bug Fixes

* typo in the error message if a column is not found ([67029a4](https://github.com/GenSpectrum/LAPIS-SILO/commit/67029a4ace345b8c3687ea39aff066849bcc19d1))

## [0.2.13](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.12...v0.2.13) (2024-08-06)


### Features

* bump serialization version to 2 ([e897203](https://github.com/GenSpectrum/LAPIS-SILO/commit/e897203780e7cd7546dfc82a2d29a6725bf07da2))
* bump serialization version to 3 ([7584214](https://github.com/GenSpectrum/LAPIS-SILO/commit/7584214e7c6866d38ad415c82abf7a01aa95d8bf))
* support SAM files as sequence input and allow partial sequence input with an offset ([5be9a9f](https://github.com/GenSpectrum/LAPIS-SILO/commit/5be9a9f23d5c1e2c54ca5837a82798aa459f9859))


### Bug Fixes

* allow all sequence-names by escaping them properly in all SQL statements ([901fc7e](https://github.com/GenSpectrum/LAPIS-SILO/commit/901fc7e0645954c7fc5ca705fd9c3902ab3d0928))

## [0.2.12](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.11...v0.2.12) (2024-07-31)


### Features

* change base image to ubuntu ([64dee0d](https://github.com/GenSpectrum/LAPIS-SILO/commit/64dee0d90decd8aeafacbf88db611c53eb12c4b5))

## [0.2.11](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.10...v0.2.11) (2024-07-24)


### Bug Fixes

* streaming: report an error when sorting requested while streaming ([313d6bb](https://github.com/GenSpectrum/LAPIS-SILO/commit/313d6bba7b0201c330fe4b39a6d68063142f8b1d))

## [0.2.10](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.9...v0.2.10) (2024-07-22)


### Bug Fixes

* reduce function calls and parallelism when inserting rows into storage backend ([#509](https://github.com/GenSpectrum/LAPIS-SILO/issues/509)) ([93ba858](https://github.com/GenSpectrum/LAPIS-SILO/commit/93ba8584fed5807123ea3fb724ff55fc57b6265d))
* preprocessing: don't abort inserting insertions upon finding a null value ([03a500a](https://github.com/GenSpectrum/LAPIS-SILO/commit/03a500acf02f4d13975748f1bb6f222392604b7c))

## [0.2.9](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.8...v0.2.9) (2024-07-18)


### Features

* have no default sequence by default, implement default amino acid sequence from config ([2edf924](https://github.com/GenSpectrum/LAPIS-SILO/commit/2edf9240fb9f63400289121f495367c7aff4ea73)), closes [#454](https://github.com/GenSpectrum/LAPIS-SILO/issues/454)
* set the default sequence when there is only a single sequence in the reference genomes ([0ae4022](https://github.com/GenSpectrum/LAPIS-SILO/commit/0ae40221e66382ac330748c3eefe916de5e26975)), closes [#454](https://github.com/GenSpectrum/LAPIS-SILO/issues/454)
* throw error in preprocessing when default sequence is not in reference genomes ([287bb8b](https://github.com/GenSpectrum/LAPIS-SILO/commit/287bb8bb08f376a2b9a9c69613b73182c900fc7a)), closes [#454](https://github.com/GenSpectrum/LAPIS-SILO/issues/454)


### Bug Fixes

* streaming: report an error when the unimplemented offset or limit matters ([3be9c06](https://github.com/GenSpectrum/LAPIS-SILO/commit/3be9c06ded44c0b1666fd770d88876856a0a9899)), closes [#511](https://github.com/GenSpectrum/LAPIS-SILO/issues/511)

## [0.2.8](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.7...v0.2.8) (2024-07-08)


### Features

* the `Fasta` and `FastaAligned` actions now stream their result instead of allocating it in memory, closes [#112](https://github.com/GenSpectrum/LAPIS-SILO/issues/112)

## [0.2.7](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.6...v0.2.7) (2024-06-25)


### Bug Fixes

* don't abort when reading table in chunks ([26b1558](https://github.com/GenSpectrum/LAPIS-SILO/commit/26b155881a6f0a46ea1882c4f3f3641d691b13a7))

## [0.2.6](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.5...v0.2.6) (2024-06-19)


### Bug Fixes

* trigger release due to refactor commit-5e3041e5 ([#494](https://github.com/GenSpectrum/LAPIS-SILO/issues/494)) ([#496](https://github.com/GenSpectrum/LAPIS-SILO/issues/496)) ([ecd4ba0](https://github.com/GenSpectrum/LAPIS-SILO/commit/ecd4ba0f4b2f5338c4f968bda7b2b18dfbea0c2c))

## [0.2.5](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.4...v0.2.5) (2024-06-17)


### Bug Fixes

* empty input without partitioning ([5fa3c92](https://github.com/GenSpectrum/LAPIS-SILO/commit/5fa3c9267eff1b0e59dda718a2ded0f874b038ae))

## [0.2.4](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.3...v0.2.4) (2024-06-14)


### Features

* allow null for sequenceName in insertion contains queries ([6dbe251](https://github.com/GenSpectrum/LAPIS-SILO/commit/6dbe251a03e8a317188de0292f0e637b8ec4c24d))


### Bug Fixes

* more efficient ndjson emptiness check ([#481](https://github.com/GenSpectrum/LAPIS-SILO/issues/481)) ([344ec7b](https://github.com/GenSpectrum/LAPIS-SILO/commit/344ec7b20a3d6727e8972334542a610260cdc782))

## [0.2.3](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.2...v0.2.3) (2024-06-10)


### Bug Fixes

* be able to start without genes, without nucleotide sequences or with neither ([e878ed5](https://github.com/GenSpectrum/LAPIS-SILO/commit/e878ed5567679f966c615a26b2d4c713f2f045e6))
* empty ndjson input files more robust ([#473](https://github.com/GenSpectrum/LAPIS-SILO/issues/473)) ([9d4232b](https://github.com/GenSpectrum/LAPIS-SILO/commit/9d4232b01cb366def7e464155867bc6f7a466471))
* erroneous file created during unit tests should not leak ([1b764af](https://github.com/GenSpectrum/LAPIS-SILO/commit/1b764afbefe13969496bf8b2bf51d50824c495bf))
* insertions being added at wrong index for large files ([#472](https://github.com/GenSpectrum/LAPIS-SILO/issues/472)) ([e056ed9](https://github.com/GenSpectrum/LAPIS-SILO/commit/e056ed9354306690b51d6d0f829b962420e2be24))
* remove incorrect compile flags ([b92ee4e](https://github.com/GenSpectrum/LAPIS-SILO/commit/b92ee4e0816f0f5b9ef1bef711b968600620f80d))

## [0.2.2](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.1...v0.2.2) (2024-05-31)


### Bug Fixes

* start with empty files without throwing an error ([d407b92](https://github.com/GenSpectrum/LAPIS-SILO/commit/d407b9277ea89294ebbc581a80d23124040e266d))

## [0.2.1](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.2.0...v0.2.1) (2024-05-28)


### Bug Fixes

* allow insertions that start at 0 ([#449](https://github.com/GenSpectrum/LAPIS-SILO/issues/449)) ([f427137](https://github.com/GenSpectrum/LAPIS-SILO/commit/f4271375b743cab0b012ef4337316c8bb3d0c6d8))

## [0.2.0](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.1.1...v0.2.0) (2024-05-23)


### ⚠ BREAKING CHANGES

* old database_config files might be invalid if they contained insertion columns. Also, we are more prohibitive for ndjson input files, which now MUST contain nucleotide/amino acid insertions for all respective sequences. The insertions action and filter do no longer require a column field.

### Features

* insertions no longer in metadata or databaseConfig, instead expected for all aligned sequences [#372](https://github.com/GenSpectrum/LAPIS-SILO/issues/372) ([1f3680c](https://github.com/GenSpectrum/LAPIS-SILO/commit/1f3680c87e912b57a79cd1280213f828a40c238e))

## [0.1.1](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.1.0...v0.1.1) (2024-05-23)


### Features

* add number of partitions to /info ([1e780b5](https://github.com/GenSpectrum/LAPIS-SILO/commit/1e780b51f7b64334e9102fef3eeb279132e363e5))
* add serializationVersion to SILO output for smoother transitioning to new formats ([d3badb6](https://github.com/GenSpectrum/LAPIS-SILO/commit/d3badb65e6c2ce5d59ab8134cf2a237c836d417e))

## [0.1.0](https://github.com/GenSpectrum/LAPIS-SILO/compare/v0.0.1...v0.1.0) (2024-05-10)


### ⚠ BREAKING CHANGES

* return data as NDJSON instead of JSON

### Features

* AAMutations with multiple sequences ([0def8b2](https://github.com/GenSpectrum/LAPIS-SILO/commit/0def8b21e27cbc25973fb64ac9e56670c09cc023))
* Action for amino acid distribution ([a0a4cf1](https://github.com/GenSpectrum/LAPIS-SILO/commit/a0a4cf1e7422ef536c6e40863f27fdd85151cc65))
* add limit orderBy and offset to all query actions ([13b7e01](https://github.com/GenSpectrum/LAPIS-SILO/commit/13b7e01cbd4396e8e4cbb6357baec4514ad2eb6c))
* add log statements to loadDatabaseState ([46a0421](https://github.com/GenSpectrum/LAPIS-SILO/commit/46a04214244ba4ff799bcb2b9bf4c2cd1ad4e006))
* add more tests, make less flaky and viable with large dataset ([7772ae3](https://github.com/GenSpectrum/LAPIS-SILO/commit/7772ae345d44a2feb32126f87026f011e7e49a59))
* add unit test for findIllegalNucleotideChar, unique test case name for insertion contains invalid pattern tests ([99c9c4b](https://github.com/GenSpectrum/LAPIS-SILO/commit/99c9c4b3cd8935c2b2ba9998cf2e0847107edd4f))
* added amino acid insertion search, added many test cases and fixed various bugs ([d1e4b2b](https://github.com/GenSpectrum/LAPIS-SILO/commit/d1e4b2b757a23f81effe3e8c1be4a112f073d590))
* allow default preprocessing config along with user defined preprocessing config ([ee9f20e](https://github.com/GenSpectrum/LAPIS-SILO/commit/ee9f20edc58b55f6afd168888ed88f0faa486109))
* allow reading fasta files with missing segments and genes [#220](https://github.com/GenSpectrum/LAPIS-SILO/issues/220) ([8ea9893](https://github.com/GenSpectrum/LAPIS-SILO/commit/8ea9893b5f0fcc0135864eae69057118ff5375dd))
* allow reading segments and genes that are null from ndjson file [#220](https://github.com/GenSpectrum/LAPIS-SILO/issues/220) ([d0a3a7e](https://github.com/GenSpectrum/LAPIS-SILO/commit/d0a3a7e0f6eb2c8d45e400bee183889d4cd41e4e))
* also get Runtime Config options from environment variables ([33bdd65](https://github.com/GenSpectrum/LAPIS-SILO/commit/33bdd65b826f97d682aed9f4f98c18870220e60a))
* also log to stdout ([54b8a47](https://github.com/GenSpectrum/LAPIS-SILO/commit/54b8a472316ad5230bcec14e0e0abfab2b7caba2))
* also return mutation destructed that does not need to be reparsed ([a93abbf](https://github.com/GenSpectrum/LAPIS-SILO/commit/a93abbf3b294a61f5858b34a140a3e6b9b228657))
* Alternative templating of symbol classes ([6b61985](https://github.com/GenSpectrum/LAPIS-SILO/commit/6b619858b8651914098a850a9bbed877921a4cd7))
* automatically detect file endings for fasta files ([75bd14e](https://github.com/GenSpectrum/LAPIS-SILO/commit/75bd14e87531e3f3d6c08d7bc3dea8158a4311e1))
* be more lenient on input data, ignore superfluous sequences and fill missing sequences with Ns ([ee12186](https://github.com/GenSpectrum/LAPIS-SILO/commit/ee1218670d4cea5c4599c5c42b0c913d9eb8b864))
* Better test coverage for SymbolEquals filter ([42c685c](https://github.com/GenSpectrum/LAPIS-SILO/commit/42c685cf4c8ce068072fbf35ac8b6a0e263234f9))
* **boolean columns, resolves #384:** const declaration ([685db9f](https://github.com/GenSpectrum/LAPIS-SILO/commit/685db9fea3562076e2dbcb9dbf426154fbd60eb7))
* **boolean columns:** actions/tuple: update assignTupleField() ([b020109](https://github.com/GenSpectrum/LAPIS-SILO/commit/b020109a7c25908b2ef647c94eb240d9a92ba8c1))
* **boolean columns:** add and use JsonValueType ([2ad1268](https://github.com/GenSpectrum/LAPIS-SILO/commit/2ad1268c36fd6c4dbd926db0adbeef0a5cf28127))
* **boolean columns:** add bool to JsonValueType, update tuple ([3c990e2](https://github.com/GenSpectrum/LAPIS-SILO/commit/3c990e25f4e42c802d9552e19342edfc6fc2f378))
* **boolean columns:** add expression_type "BooleanEquals" ([b82ec69](https://github.com/GenSpectrum/LAPIS-SILO/commit/b82ec69e5d7aef6b75dc68de6290e1d962b0edf5))
* **boolean columns:** add filter_expressions/bool_equals ([ff2a138](https://github.com/GenSpectrum/LAPIS-SILO/commit/ff2a138a82670ba3035c7432c0c558d520d79268))
* **boolean columns:** add optional_bool ([a2e47f8](https://github.com/GenSpectrum/LAPIS-SILO/commit/a2e47f8d1895e852d487be0ed3a88fa5a928f4f4))
* **boolean columns:** add storage/column/bool_column ([5eb3430](https://github.com/GenSpectrum/LAPIS-SILO/commit/5eb3430e3eafe5ab28dbf90632f1bb85663d66e8))
* **boolean columns:** column_group: update ColumnPartitionGroup ([d7adecf](https://github.com/GenSpectrum/LAPIS-SILO/commit/d7adecf4e9c51bb9e3e6b1aad8e3f7b43e0692b0))
* **boolean columns:** column_group.h: add {ColumnPartitionGroup,ColumnGroup}::bool_columns fields ([af44f1a](https://github.com/GenSpectrum/LAPIS-SILO/commit/af44f1a8cf552d8ab6625d9fd440d83b2e10c36f))
* **boolean columns:** database ([c555a68](https://github.com/GenSpectrum/LAPIS-SILO/commit/c555a68801db08d089be8d337f40a8793ac54b48))
* **boolean columns:** database_config: add "bool" case to DatabaseConfigReader::readConfig() ([12fc7b8](https://github.com/GenSpectrum/LAPIS-SILO/commit/12fc7b8c95decfd58692332a9496e7046ca12242))
* **boolean columns:** database_config: add "boolean" case to de/serialisation ([e6d5363](https://github.com/GenSpectrum/LAPIS-SILO/commit/e6d536330fb4ac162db34f5c5233ae495211da3e))
* **boolean columns:** database_config: add BOOL to ValueType ([0b47b82](https://github.com/GenSpectrum/LAPIS-SILO/commit/0b47b828915b293a1ed6c1210b3574045a6fcf67))
* **boolean columns:** database_config: update DatabaseMetadata::getColumnType() ([30febdd](https://github.com/GenSpectrum/LAPIS-SILO/commit/30febddb8914d1e909b10d7f31a0803b4a485ba7))
* **boolean columns:** database_partition ([0aef8f0](https://github.com/GenSpectrum/LAPIS-SILO/commit/0aef8f0c4e28fa8bdeec9f6638388e62d9b7b9cc))
* **boolean columns:** optional_bool: add `==` ([f0aa3e8](https://github.com/GenSpectrum/LAPIS-SILO/commit/f0aa3e8c1ac5b43d9414a84587bfb13ed818323e))
* **boolean columns:** selection ([e52f2dc](https://github.com/GenSpectrum/LAPIS-SILO/commit/e52f2dc5623929395d40aff3264c48099f6a246a))
* build metadata in parallel to sequences. Do not create unaligned sequence tables in preprocessing, rather hive-partition them directly to disk. Better (debug-)logging ([c1cdfeb](https://github.com/GenSpectrum/LAPIS-SILO/commit/c1cdfeb3cb25e8bcbd5e095e5dbc0f78ae35492f))
* bulk Tuple allocations now possible ([902ec04](https://github.com/GenSpectrum/LAPIS-SILO/commit/902ec0461544fc0cbc0e8e0ba07b18600b29126d))
* clearer Operator::negate and Expression::toString, logical Equivalents for debug printing/logging for the Leaf Operators IndexScan and BitmapSelection ([026b639](https://github.com/GenSpectrum/LAPIS-SILO/commit/026b639af5d45690c42c70b13b926ce72fdcc837))
* consistent behavior of configs when starting SILO with both --preprocessing and --api ([847ec7e](https://github.com/GenSpectrum/LAPIS-SILO/commit/847ec7e8af6eda73c95076c9b4428ed3fd5c3f9f))
* declutter README.md from linting option, which is now disabled by default and enforced in the CI for the Linter ([9220435](https://github.com/GenSpectrum/LAPIS-SILO/commit/922043538a39992b2322c02434826dab070d1538))
* details no longer shows insertions ([#354](https://github.com/GenSpectrum/LAPIS-SILO/issues/354)) ([473cd98](https://github.com/GenSpectrum/LAPIS-SILO/commit/473cd980f6aee5267e8d4d0cd65671e5722dc26b))
* display database info after loading new database state ([0249416](https://github.com/GenSpectrum/LAPIS-SILO/commit/02494165930eefbafb0f74e069962a94f830d3e8))
* display preprocessing duration  in logs in human-readable format (not in microseconds) [#296](https://github.com/GenSpectrum/LAPIS-SILO/issues/296) ([a2499af](https://github.com/GenSpectrum/LAPIS-SILO/commit/a2499af5cf3c5f46c695f019fda632c28939c62c))
* do not enforce building with clang-tidy by default. Linter will still be enforced ([7134e45](https://github.com/GenSpectrum/LAPIS-SILO/commit/7134e45ae2c66acb0e53ac433a187cdc4e2ccefa))
* FastaAligned action ([50776c8](https://github.com/GenSpectrum/LAPIS-SILO/commit/50776c83d8cb4409e4f01c6b49a189647f5140de))
* faster builds by copying [@corneliusroemer](https://github.com/corneliusroemer) image caching for our dependency images, which rarely change ([#374](https://github.com/GenSpectrum/LAPIS-SILO/issues/374)) ([7867bc7](https://github.com/GenSpectrum/LAPIS-SILO/commit/7867bc744648f5410793eef8d52df24b23c3c918))
* filter for amino acids ([b52aabd](https://github.com/GenSpectrum/LAPIS-SILO/commit/b52aabde38ef4e1fb976a37c89fe1869f03ace57))
* fix sorting ([1ed18ae](https://github.com/GenSpectrum/LAPIS-SILO/commit/1ed18ae4ab38fb4822b075e5c3deeac90b2bfb37))
* flipped bitmap can now be set before insertion ([f61c803](https://github.com/GenSpectrum/LAPIS-SILO/commit/f61c803f74d135e75865aa09a9b373aa3603f19d))
* format DatabaseConfig ([4fb8f1b](https://github.com/GenSpectrum/LAPIS-SILO/commit/4fb8f1b90486abc84d7d9e941fb8d31fa3cc3e5d))
* format PreprocessingConfig ([ee35207](https://github.com/GenSpectrum/LAPIS-SILO/commit/ee35207e29888d52cbbfba0ae4f4fe0dda80be1d))
* generalize mutations action to have consistent behavior for different symbols ([9834aea](https://github.com/GenSpectrum/LAPIS-SILO/commit/9834aea8413fe34efb5414178de8a93f4fbf076f))
* generalizing symbol and mutation filters. Clear handling of ambiguous symbols ([aa9ad4d](https://github.com/GenSpectrum/LAPIS-SILO/commit/aa9ad4df935eed84f482cb9d81bafaca6f3374a3))
* Generalizing the config for multiple nucleotide sequences and multiple genes ([9a80204](https://github.com/GenSpectrum/LAPIS-SILO/commit/9a802049ea95fb30955f12d9696ac672bd14ce21))
* have structured and destructured insertion in insertions response ([0a7e46a](https://github.com/GenSpectrum/LAPIS-SILO/commit/0a7e46ad0e7740919059773c327dcda2cb1ae09e))
* hide intermediate results of the preprocessing - don't put it in the output ([44327b0](https://github.com/GenSpectrum/LAPIS-SILO/commit/44327b0b30203c526fe7a3e40e1701645808940e))
* implement basic request id to trace requests [#303](https://github.com/GenSpectrum/LAPIS-SILO/issues/303) ([4defb59](https://github.com/GenSpectrum/LAPIS-SILO/commit/4defb59cfe9d48635592ae0a4cd572d6caa94895))
* implement data updates at runtime. More resilient to superfluous or missing directory separators ([dc5dfaa](https://github.com/GenSpectrum/LAPIS-SILO/commit/dc5dfaa50d13512dc39c665aeab20630fa94415c))
* implement insertion columns and search ([9167236](https://github.com/GenSpectrum/LAPIS-SILO/commit/91672365f8cfdfbecd71804e3064134f93adc1cb))
* improve loadDB speeds ([2b7cd7d](https://github.com/GenSpectrum/LAPIS-SILO/commit/2b7cd7dacadb5e3887225aa84b57da8f7ca84394))
* improve validation error message of some actions on orderByFields ([a0da5b5](https://github.com/GenSpectrum/LAPIS-SILO/commit/a0da5b54a1ca2e9eff9c718028bea56fc8cd2661))
* insertion action targets all insertion columns by default ([6b70241](https://github.com/GenSpectrum/LAPIS-SILO/commit/6b70241db3577480078e51e7bc861579013dd4e6))
* insertion columns for amino acids and multiple sequence names ([3cc8fee](https://github.com/GenSpectrum/LAPIS-SILO/commit/3cc8fee9153ec1b23a819c4331b3f9dce3e16b37))
* insertions action ([e067062](https://github.com/GenSpectrum/LAPIS-SILO/commit/e0670629b1a40dc0e56a31e15eaa42e51a350d66))
* insertions contains action now targets all columns if the column name is missing ([32a6951](https://github.com/GenSpectrum/LAPIS-SILO/commit/32a69514b654a737478f0aafe9cdca9c41cc0691))
* introduce new storage type for Sequence Positions, where the most numerous symbol is deleted ([6e15204](https://github.com/GenSpectrum/LAPIS-SILO/commit/6e15204a9265e65e26562c227f5ccaf5cfba8c65))
* introduce storage of unaligned sequences from either ndjson file or fasta file and make them queryable via the Fasta action ([44df849](https://github.com/GenSpectrum/LAPIS-SILO/commit/44df849d6f84eba3321285bdaf6f22509c0c5f17))
* load table lazily. Unaligned Sequences do not need to load the table ([c2a8439](https://github.com/GenSpectrum/LAPIS-SILO/commit/c2a8439fc724452c4184f8c49b3db65c6a99b731))
* log databaseConfig and preprocessingConfig ([d2dc58c](https://github.com/GenSpectrum/LAPIS-SILO/commit/d2dc58cdbed9cea1e9e3028324eaeef32d82dbba))
* logging for partition ([e75a925](https://github.com/GenSpectrum/LAPIS-SILO/commit/e75a925d8b4ac7bd66229437d244089d26cb10de))
* logging improvements ([4c12a88](https://github.com/GenSpectrum/LAPIS-SILO/commit/4c12a882aa75025fac99e65ba3a491f72a2b78ec))
* make database serializable again ([2523e67](https://github.com/GenSpectrum/LAPIS-SILO/commit/2523e67d08cc59c297bc4ad0a9134f1b96d2cb0a))
* make pangoLineageDefinitionFilename in preprocessing config optional, linter errors ([0f3dc53](https://github.com/GenSpectrum/LAPIS-SILO/commit/0f3dc534891f4d6553385e85de8ad77565f2a9f9))
* make partition_by field in config optional ([3942418](https://github.com/GenSpectrum/LAPIS-SILO/commit/394241812df369a908c5d4495e503eca3abdf3db))
* make SILO Docker image by default read data from /data ([e83b910](https://github.com/GenSpectrum/LAPIS-SILO/commit/e83b910d8f9c7e38fa7c80ee4571b85601f99ab4))
* make threads and max queued http connections available through optional parameter ([3ecde68](https://github.com/GenSpectrum/LAPIS-SILO/commit/3ecde686b4c6979d07b8dce73b2ccb8fda6b48ee))
* migration to duckdb 0.10.1 ([c1426ef](https://github.com/GenSpectrum/LAPIS-SILO/commit/c1426ef906da8d8914718eaa5173a1e7750d407b))
* mine data version at beginning of preprocessing ([362fe0f](https://github.com/GenSpectrum/LAPIS-SILO/commit/362fe0f988e9901ba5b48a5b0c1a81d480093eb8))
* More robust InputStreamWrapper ([305dd36](https://github.com/GenSpectrum/LAPIS-SILO/commit/305dd366422131d6ecfbb4ef97f3cb3edb712211))
* multiple performance improvements for details endpoint ([28f41d0](https://github.com/GenSpectrum/LAPIS-SILO/commit/28f41d01933a53095bf69888beef1ee5ebbc6208))
* optimize bitmaps before finishing partition ([5b06d58](https://github.com/GenSpectrum/LAPIS-SILO/commit/5b06d582859ec9dd1bb0c63ff5c52b21fa08b9fe))
* order all actions by default ([a2f5c04](https://github.com/GenSpectrum/LAPIS-SILO/commit/a2f5c04b64e9b175ea023cd82d7fe8f1bacf2f95))
* preparation of insertion columns ([c14a370](https://github.com/GenSpectrum/LAPIS-SILO/commit/c14a370ebf19800e9ad0b6eeb9e4957dafd77cba))
* put output and logs to gitignore ([789e489](https://github.com/GenSpectrum/LAPIS-SILO/commit/789e489980be9f65c618822c6460588c9ed7aff7))
* reenable bitmap inversion ([75ac20f](https://github.com/GenSpectrum/LAPIS-SILO/commit/75ac20f679c433f7b439ec4e9935f7ea005d9ca8))
* reenable pushdown of And expressions through selections ([802bec0](https://github.com/GenSpectrum/LAPIS-SILO/commit/802bec0cf1ba876138641b9c0702dde77ea01683))
* refactor saving and loading database to not require preprocessing structs anymore ([45bf7ed](https://github.com/GenSpectrum/LAPIS-SILO/commit/45bf7ede2130ec9d2cfc3c8d1c48d32a921e6e83))
* reintroduce randomize for all query actions ([166045c](https://github.com/GenSpectrum/LAPIS-SILO/commit/166045cad4953dfd1eb8c3ff2b72b2d903d2193b))
* reserve space in columns when bulk inserting rows ([e3c9620](https://github.com/GenSpectrum/LAPIS-SILO/commit/e3c962095152cdb427bd5ee2f6a748bb4788ef73))
* return data as NDJSON instead of JSON ([c236ba4](https://github.com/GenSpectrum/LAPIS-SILO/commit/c236ba43af666758a0c59083c3bd97396a6e4fec)), closes [#126](https://github.com/GenSpectrum/LAPIS-SILO/issues/126)
* return data version on each query ([be5c886](https://github.com/GenSpectrum/LAPIS-SILO/commit/be5c886e4dcdf8d1094ee28a08738f62a4ededb4))
* return only aliased pango lineages ([abf0844](https://github.com/GenSpectrum/LAPIS-SILO/commit/abf08444677ebd796dc2fad6107051c571a221ff))
* reveal some more details when reading YAML fails ([1f8d9db](https://github.com/GenSpectrum/LAPIS-SILO/commit/1f8d9db929add1120169b73c80b11700c88d4cfc))
* run preprocessing in github ci ([f53eddb](https://github.com/GenSpectrum/LAPIS-SILO/commit/f53eddbbbc6e24d1ee371cac4c752bae19ebaeb0))
* save database state into folder with name &lt;data version&gt; ([41923eb](https://github.com/GenSpectrum/LAPIS-SILO/commit/41923eb1c99ab1373d448a9fbdaa8219ddd4df92))
* separate preprocessing and starting silo ([9808e2d](https://github.com/GenSpectrum/LAPIS-SILO/commit/9808e2d91cece622121e6d47734b1901b76cdeef))
* serialization of partition descriptor to json ([472c1da](https://github.com/GenSpectrum/LAPIS-SILO/commit/472c1da51d66b79ea8e20aaa59d1e2856f333756))
* some suggestions for the insertion search ([ae900da](https://github.com/GenSpectrum/LAPIS-SILO/commit/ae900da3672e30d6c04ad4edf56e9b8d3195d13b))
* Specifiable nucSequence query target ([7cc609f](https://github.com/GenSpectrum/LAPIS-SILO/commit/7cc609fe716116ab0a77443eaa2dce73482b6116))
* statically disable deleted symbol optimisations because of performance penalty ([4e522f4](https://github.com/GenSpectrum/LAPIS-SILO/commit/4e522f41fbc5bccb14a505f38af83c96e17634fc))
* stick to the default of having config value keys in camel case ([a1cae40](https://github.com/GenSpectrum/LAPIS-SILO/commit/a1cae40d7e5163289bbf68f6398a036a59eb0cae))
* storing amino acids ([f11a330](https://github.com/GenSpectrum/LAPIS-SILO/commit/f11a330302d61eff2dfa2354c0d565faae89b400))
* support for nullable columns ([6f78e3a](https://github.com/GenSpectrum/LAPIS-SILO/commit/6f78e3ad61af29e20b98bda72d4edc34951e7c1b))
* support recombinant lineages ([3e848a5](https://github.com/GenSpectrum/LAPIS-SILO/commit/3e848a539f2b4970b071028f347e654c9b745149))
* template class for sequence store ([cef4d48](https://github.com/GenSpectrum/LAPIS-SILO/commit/cef4d487db0ef57b2aab5ffa339a5abec05f65cd))
* templatized Symbol classes ([6b9d734](https://github.com/GenSpectrum/LAPIS-SILO/commit/6b9d734d49fe2ae0ca47065361f6d6465166177c))
* test set with amino acid insertions ([de2c4f8](https://github.com/GenSpectrum/LAPIS-SILO/commit/de2c4f83d8b5d45cd9d0944f496b7a2f5b7d763f))
* throw an error when there is not initialized database loaded yet [#295](https://github.com/GenSpectrum/LAPIS-SILO/issues/295) ([b17f72a](https://github.com/GenSpectrum/LAPIS-SILO/commit/b17f72a55c7835ef5c07409ce7f0d742661a859c))
* tidying up CLI and file configuration for runtime config. Added option for specifying the port ([c3a88a0](https://github.com/GenSpectrum/LAPIS-SILO/commit/c3a88a0f44e6d1eaaf715ade8389f13e7a87fb40))
* Unit tests for Tuple ([4fc06e8](https://github.com/GenSpectrum/LAPIS-SILO/commit/4fc06e81de653dac470c02fd258eaf4532f876ae))
* update conan version ([5540a67](https://github.com/GenSpectrum/LAPIS-SILO/commit/5540a67f4054f5f985791b283593f2bdd6885232))
* use 'pragma once' as include guards instead of 'ifndef...' ([bc49aa5](https://github.com/GenSpectrum/LAPIS-SILO/commit/bc49aa5530c97ca7cc10f7da4c27e29cb6ee16c9))
* use own scope for preprocessing ([2a93846](https://github.com/GenSpectrum/LAPIS-SILO/commit/2a9384651ba545265a5c8ccc6feccc1249b44ffd))
* use same default min proportions for mutations actions as the old LAPIS ([f42f830](https://github.com/GenSpectrum/LAPIS-SILO/commit/f42f83088e2302cf604d8846b69ea7e58659570a))


### Bug Fixes

* adapt randomize query results to target architecture. x86 and ARM have possibly different std::hash results ([#355](https://github.com/GenSpectrum/LAPIS-SILO/issues/355)) ([600000d](https://github.com/GenSpectrum/LAPIS-SILO/commit/600000d80616bb1688eb4fc8a7855b361a1eae1d))
* add bash dependency which is required by conan build of pkgconf and is not installed on alpine by default ([1b3f51c](https://github.com/GenSpectrum/LAPIS-SILO/commit/1b3f51c5e7e6a88c5b7500f3fd832eead62fff3b))
* add insertion to database_config test ([aec40d0](https://github.com/GenSpectrum/LAPIS-SILO/commit/aec40d0f8366c8f15906c6edbed35439b5a794a0))
* add missing file for test ([262bedc](https://github.com/GenSpectrum/LAPIS-SILO/commit/262bedc35fc3704854b2f321c8cab49e08d803a3))
* add missing sequenceName field to mutation action "orderBy" ([06c8c86](https://github.com/GenSpectrum/LAPIS-SILO/commit/06c8c86ebeb822d03f46342faafcf1f7d29a7d51))
* add sleep statement before row call ([8fa8efb](https://github.com/GenSpectrum/LAPIS-SILO/commit/8fa8efb5f7634e9eb91ad71d0d5d3af47c0fcf30))
* add workaround so insertions are read correctly ([8a2bfa8](https://github.com/GenSpectrum/LAPIS-SILO/commit/8a2bfa8ee211a8e8ec6c2e83671eab8abe69fc87))
* allow sql keywords for metadata field names [#259](https://github.com/GenSpectrum/LAPIS-SILO/issues/259) ([6fbeee5](https://github.com/GenSpectrum/LAPIS-SILO/commit/6fbeee56732d2d3934ff5769cc07ded2cbf2caa4))
* also consider 'missing' symbols in the mutation action. Bugfix where Position invariant was broken because of 'missing' symbols ([fab72a6](https://github.com/GenSpectrum/LAPIS-SILO/commit/fab72a60795568d0d1dddc25bf4a8e594f77477b))
* alternative non-exhaustive three mer index ([467086f](https://github.com/GenSpectrum/LAPIS-SILO/commit/467086f52d75ecee39a2fcb7937554869a7d03e1))
* always build dependency image for amd64 platform ([4f73cb0](https://github.com/GenSpectrum/LAPIS-SILO/commit/4f73cb04687ec57df32878e310139671a110ed5a))
* bug when filtering for indexedStringColumns which are not present in some partitions ([62d4e08](https://github.com/GenSpectrum/LAPIS-SILO/commit/62d4e08d3718a4b4b20e283602b558ad26c23a13))
* bug where sequence reconstruction is false when the flipped bitmap is different from the reference sequence symbol ([edac58c](https://github.com/GenSpectrum/LAPIS-SILO/commit/edac58cb340c409f3db96f5e6d6c325c8fb9ecb7))
* change random ordering for gcc hashing ([78055ff](https://github.com/GenSpectrum/LAPIS-SILO/commit/78055ff25fef2541b260285552e416433c4b6ca2))
* change test to reflect new optimisations ([cb63010](https://github.com/GenSpectrum/LAPIS-SILO/commit/cb630104c81e1846d8d87de0704a90216fbe5986))
* compiling And: append selection_child-&gt;predicates to predicates - not vice versa ([bdebca5](https://github.com/GenSpectrum/LAPIS-SILO/commit/bdebca5caab90ce4f37c7cf7c7844fc7808b2ba6))
* deterministic order for e2e test ([02427a2](https://github.com/GenSpectrum/LAPIS-SILO/commit/02427a25d291f3aba8fcaa95e577bb32c9c32179))
* divergence between mac and linux info test results, fix memory leaks in Threshold.cpp ([ffcefd0](https://github.com/GenSpectrum/LAPIS-SILO/commit/ffcefd0fb82345dddce170c0254fb20a167e18ba))
* do not exclude zstd filter from boost installation ([c974230](https://github.com/GenSpectrum/LAPIS-SILO/commit/c97423091d073a501499e30d0ee3a6782da4ba3c))
* do not use std::filesystem::path::relative_path() to also support absolute paths ([b9ff422](https://github.com/GenSpectrum/LAPIS-SILO/commit/b9ff422b76e63a673b8869725dc3a3fec677979a))
* endToEndResults ([c807a33](https://github.com/GenSpectrum/LAPIS-SILO/commit/c807a33b11ceb3e2db81f14613cc7601e81ea689))
* error when the Mutations action looked for sequences but the filter was empty ([91e5d52](https://github.com/GenSpectrum/LAPIS-SILO/commit/91e5d52e0dc6d11522c8ea2cc0be237d332d967c))
* fix memory leaks in indexed_string_column.cpp and insertion_contains.cpp ([c2eeac8](https://github.com/GenSpectrum/LAPIS-SILO/commit/c2eeac88ec688f25e65409490dcf68b954e53789))
* floatEquals and floatBetween with null values ([47b436e](https://github.com/GenSpectrum/LAPIS-SILO/commit/47b436e611e2266226c3070d11dcbc3366de2c7c))
* hide nucleotide sequence for default sequence ([584715c](https://github.com/GenSpectrum/LAPIS-SILO/commit/584715cd210f51c4e24a2b301145a1bdf4a91ac5))
* insertion column, remove reference ([d940c52](https://github.com/GenSpectrum/LAPIS-SILO/commit/d940c52a206ab9884f707baa491c4b12c59dfe50))
* insertion search e2e and insertion column tests, dont allow non-empty value for insertion search ([19af04d](https://github.com/GenSpectrum/LAPIS-SILO/commit/19af04dd7fbcafa6004ddc4c2894967d427a8308))
* linking error on linux ([ad9076c](https://github.com/GenSpectrum/LAPIS-SILO/commit/ad9076c8f385c5d56f20219422ee1d1d49602966))
* linter ([54d3c34](https://github.com/GenSpectrum/LAPIS-SILO/commit/54d3c3435a38dbfa0ddbfaf5af01b03f2dbbfaa1))
* linter ([e6b6ab7](https://github.com/GenSpectrum/LAPIS-SILO/commit/e6b6ab70b89ce7065185d2a19c0017d93d40801b))
* linter ([b31851c](https://github.com/GenSpectrum/LAPIS-SILO/commit/b31851c44d2473560ef486659bd3f3aad1f40709))
* linter ([34830ab](https://github.com/GenSpectrum/LAPIS-SILO/commit/34830ab0f0818c07f19ad28f8aa12b4229018fca))
* linter errors ([e9e1bbf](https://github.com/GenSpectrum/LAPIS-SILO/commit/e9e1bbfea06510b2dbb1c1097daadab90ade3ab1))
* Linter throws again and added clang-format option ([87cb4a5](https://github.com/GenSpectrum/LAPIS-SILO/commit/87cb4a57422773c5117edc8cba9afdea5754f1b6))
* Make C++ flags in CMake compatible for MacOS ([38cae69](https://github.com/GenSpectrum/LAPIS-SILO/commit/38cae694f86abad05b650756091e2238c02baf72))
* metadata info test accessing getMetadataFields output no longer directly but over the address of a const ([0dcdee1](https://github.com/GenSpectrum/LAPIS-SILO/commit/0dcdee1f8b0f788d850dd9576e6d9f770969d440))
* missing include ([0773999](https://github.com/GenSpectrum/LAPIS-SILO/commit/0773999ae2178dc2b5e221cf74b5ffd7e523f06a))
* new linter errors ([ea6934c](https://github.com/GenSpectrum/LAPIS-SILO/commit/ea6934cb3daf6b8cdbcb707a113ed3866f87e6cd))
* no longer have regression when no bitmap flipped is most efficient ([b653753](https://github.com/GenSpectrum/LAPIS-SILO/commit/b65375320612625fd1a7325878174a1e2d621eda))
* nodiscard (silence warnings) ([69421c1](https://github.com/GenSpectrum/LAPIS-SILO/commit/69421c158424dd75fba9c3804ebf6d0d89262aea)), closes [#390](https://github.com/GenSpectrum/LAPIS-SILO/issues/390)
* non default unaligned nucleotide sequence prefix ([93b4829](https://github.com/GenSpectrum/LAPIS-SILO/commit/93b4829d834269cfbbc86fb1eb53a29bb0659190))
* nucleotide symbol equals with dot ([6ad623e](https://github.com/GenSpectrum/LAPIS-SILO/commit/6ad623eeaf9089808bdfc29c9b30d1486f2f9782))
* only apply order-by if the field is set, validate orderBy fields for all operations ([405a7f1](https://github.com/GenSpectrum/LAPIS-SILO/commit/405a7f1657c94c327fc667eabcef14e07e858779))
* pango lineage filter with null values ([b7238a8](https://github.com/GenSpectrum/LAPIS-SILO/commit/b7238a8ddced5c2661d8778b838c3a5aa0bd860c))
* parse error messages for mutation filter expressions ([9e4612d](https://github.com/GenSpectrum/LAPIS-SILO/commit/9e4612d34b3e4b50909d3759b1dfc5050783fa6d))
* put compressors into sql function to avoid static variables ([c1a11c8](https://github.com/GenSpectrum/LAPIS-SILO/commit/c1a11c88676d597d4ed66f7611a42a8b726c3753))
* quoting {} in "x.{}" SQL struct accesses, as a string starting with a number leads to parser errors ([#409](https://github.com/GenSpectrum/LAPIS-SILO/issues/409)) ([f3ba6db](https://github.com/GenSpectrum/LAPIS-SILO/commit/f3ba6dbfbaf276ee7161d79751bdb68533e2c19a))
* random (but deterministic for a version) result can depend on internal state, which was changed with duckdb update ([9d9351b](https://github.com/GenSpectrum/LAPIS-SILO/commit/9d9351b27353a8bc8630f9498f8e390bb5b4ca56))
* recursive file reading for nodejs&lt;20 ([9ac0ffe](https://github.com/GenSpectrum/LAPIS-SILO/commit/9ac0ffebc5fd7d831fc4b37bb54a0493be31b822))
* remove caching for linter. Docker image to large on github actions. ([8eaeee6](https://github.com/GenSpectrum/LAPIS-SILO/commit/8eaeee6a1395ef10006f2a8ff3044ffef1b6007c))
* response format ([bca4961](https://github.com/GenSpectrum/LAPIS-SILO/commit/bca49612e55245ceff20e26ca37e0e1260912dd0))
* revert duckdb migration due to it being unable to build the new version on the GitHub runner ([2a62c54](https://github.com/GenSpectrum/LAPIS-SILO/commit/2a62c54d8de3aebabf20394c3b58c15c43773f08))
* revert test numbers to pre-optimization ([99b600a](https://github.com/GenSpectrum/LAPIS-SILO/commit/99b600abda10d4d204ebc2f69cae1d2aa5ebeb4e))
* Roaring from 1.3.0 -&gt; 1.0.0 because of broken CI ([ce82c77](https://github.com/GenSpectrum/LAPIS-SILO/commit/ce82c779c453882c2fa0491778c5df2163d1ce48))
* seralization for insertion_index and insertion_column ([ee99f8d](https://github.com/GenSpectrum/LAPIS-SILO/commit/ee99f8d2dd0631b20ff78026f716c6c8a40411ae))
* single partition build fix ([a8af1c9](https://github.com/GenSpectrum/LAPIS-SILO/commit/a8af1c9792b9d95fc9d146e4a12daec72cc7a4fb))
* specify namespace fmt in calls to format_to ([#353](https://github.com/GenSpectrum/LAPIS-SILO/issues/353)) ([62ffa3b](https://github.com/GenSpectrum/LAPIS-SILO/commit/62ffa3b9b3cf9b14a50cfafd87ce10adae2b7294))
* specifying apk versions ([2c2354e](https://github.com/GenSpectrum/LAPIS-SILO/commit/2c2354edea80063b941a5e84f264401bff4a9b1b))
* test cases verifying that the positions index for mutation distribution are now 1-indexed ([fdf972a](https://github.com/GenSpectrum/LAPIS-SILO/commit/fdf972a631ec49e8186a00b089e5b96be1f0617a))
* test with deterministic results, remove 2 unused variables ([96a424b](https://github.com/GenSpectrum/LAPIS-SILO/commit/96a424b2610865203eae460baac7dc2a388f91db))
* unit test info number updates for new pango-lineages in test data ([39d07c2](https://github.com/GenSpectrum/LAPIS-SILO/commit/39d07c2e5e3970a1ad819751834cdb859a9c591c))
* unit tests and mock fixtures ([db506a1](https://github.com/GenSpectrum/LAPIS-SILO/commit/db506a1ff2eb03249f5ba58eb3faa42255a0a27a))
* update cmake version on ubuntu ([227a2dd](https://github.com/GenSpectrum/LAPIS-SILO/commit/227a2dde0f0d8fa3dfeaf4f4fe48c900f54561f2))
* Upper and lower bound should be inclusive in DateBetween filter ([53c6c05](https://github.com/GenSpectrum/LAPIS-SILO/commit/53c6c05f8c7a66b6dbb67d9a9535831d7f0fa21e))
* Wrong compare function used in multi-threaded case, which displayed wrong tuples in the details endpoint when a limit was used ([650bf36](https://github.com/GenSpectrum/LAPIS-SILO/commit/650bf367427e2d92a6e29be4c01438dffa7358f6))
* zstd dependency ([c145722](https://github.com/GenSpectrum/LAPIS-SILO/commit/c1457228bdca3996a58ea4b785aa330caf96802d))
