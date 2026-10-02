// Smoke/integration test for the RhyDB WebAssembly build.
//
// Requires wasm to already be built, e.g. via `make wasm`.

import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, test } from "node:test";

import createRhydbModule32 from "../dist/rhydb_wasm.js";
import createRhydbModule64 from "../dist/wasm64/rhydb_wasm.js";

type CreateModule32 = typeof createRhydbModule32;
type MainModule32 = Awaited<ReturnType<CreateModule32>>;

type CreateModule64 = typeof createRhydbModule64;
type MainModule64 = Awaited<ReturnType<CreateModule64>>;

type MainModule = MainModule32 | MainModule64;

const variants: { label: string; create: CreateModule32 | CreateModule64 }[] = [
    { label: "wasm32", create: createRhydbModule32 },
    { label: "wasm64", create: createRhydbModule64 },
];

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = join(here, "..", "..");
const fixtureDir = join(repoRoot, "testBaseData", "unitTestDummyDataset");

const EXPECTED_SEQUENCE_COUNT = 5;

// Compresses a buffer with the `zstd` CLI (available in the CI image and the
// dev container). Kept as a helper so the .zst input test fails loudly with a
// clear message if zstd is missing rather than producing a corrupt fixture.
function zstdCompress(input) {
    const result = spawnSync("zstd", ["-q", "-c"], { input, maxBuffer: 64 * 1024 * 1024 });
    if (result.error) {
        throw result.error;
    }
    assert.equal(result.status, 0, `zstd exited with ${result.status}: ${result.stderr}`);
    return result.stdout;
}

function mkdirp(module: MainModule, path) {
    let current = "";
    for (const part of path.split("/").filter(Boolean)) {
        current += `/${part}`;
        if (!module.FS.analyzePath(current).exists) {
            module.FS.mkdir(current);
        }
    }
}

function readDirectoryEntries(module: MainModule, path) {
    return module.FS.readdir(path).filter((entry) => entry !== "." && entry !== "..");
}

// Embind throws C++ exceptions to JS as an opaque value. With
// `-sEXPORTED_RUNTIME_METHODS=...,getExceptionMessage` the message can be
// recovered from it. Returns the best available string for the thrown value.
function exceptionMessage(module: MainModule, error) {
    try {
        const message = module.getExceptionMessage(error);
        if (Array.isArray(message)) {
            return message.filter(Boolean).join(": ");
        }
        return String(message);
    } catch {
        return error?.message ?? String(error);
    }
}

// Runs `fn`, asserts it threw, and returns the recovered exception message.
function expectThrows(module: MainModule, fn, description) {
    let thrown;
    try {
        fn();
    } catch (error) {
        thrown = error;
    }
    assert.notEqual(thrown, undefined, `expected ${description} to throw`);
    return exceptionMessage(module, thrown);
}

// Writes the unitTestDummyDataset fixture into the virtual filesystem and
// returns the path to a preprocessing config pointing at it. The config
// overrides `inputDirectory` to the in-memory location, since the checked-in
// preprocessing_config.yaml uses a path relative to the repository root that
// only makes sense for the native build.
function writeFixture(module: MainModule, inputDir) {
    mkdirp(module, inputDir);

    for (const filename of [
        "input.ndjson",
        "database_config.yaml",
        "reference_genomes.json",
        "test_lineage_definition.yaml",
        "phylogenetic_tree.nwk",
    ]) {
        module.FS.writeFile(
            `${inputDir}/${filename}`,
            readFileSync(join(fixtureDir, filename), "utf8")
        );
    }

    const preprocessingConfig = `
inputDirectory: "."
outputDirectory: "./output/"
ndjsonInputFilename: "input.ndjson"
lineageDefinitionFilenames:
  - "test_lineage_definition.yaml"
phyloTreeFilename: "phylogenetic_tree.nwk"
referenceGenomeFilename: "reference_genomes.json"
`;
    module.FS.writeFile(`${inputDir}/preprocessing_config.yaml`, preprocessingConfig);
}

for (const variant of variants) {
    describe(variant.label, () => {
        const loadModule = (): Promise<MainModule> => variant.create();

        test("preprocess, query, save, and load a database end-to-end", async () => {
            const module = await loadModule();

            writeFixture(module, "/input");
            module.FS.chdir("/input");

            const handle = module.preprocess("preprocessing_config.yaml");
            assert.notEqual(handle, undefined);

            const info = JSON.parse(module.info(handle));
            assert.equal(info.sequenceCount, EXPECTED_SEQUENCE_COUNT);

            const ndjson = module.query(handle, "default.groupBy({count:=count()})");
            const rows = ndjson
                .trim()
                .split("\n")
                .filter((line) => line.length > 0)
                .map((line) => JSON.parse(line));
            assert.equal(rows.length, 1);
            assert.equal(rows[0].count, EXPECTED_SEQUENCE_COUNT);

            mkdirp(module, "/output");
            module.save(handle, "/output");
            // save() creates a versioned data directory under /output (e.g. /output/<version>/),
            // matching the native on-disk layout that load() also expects.
            const savedEntries = readDirectoryEntries(module, "/output");
            assert.ok(savedEntries.length > 0, "save() should create a versioned state directory");

            const loadedHandle = module.load("/output");
            const loadedInfo = JSON.parse(module.info(loadedHandle));
            assert.equal(loadedInfo.sequenceCount, EXPECTED_SEQUENCE_COUNT);

            module.dispose(handle);
            module.dispose(loadedHandle);
        });

        test("save/query/info reject an unknown database handle", async () => {
            const module = await loadModule();

            const unknownHandle = 999999;
            for (const call of [
                () => module.save(unknownHandle, "/unused"),
                () => module.query(unknownHandle, "default.groupBy({count:=count()})"),
                () => module.info(unknownHandle),
            ]) {
                const message = expectThrows(module, call, "call with unknown handle");
                assert.match(message, /Unknown RhyDB database handle/);
            }
        });

        test("load rejects a directory without a compatible RhyDB state", async () => {
            const module = await loadModule();

            mkdirp(module, "/empty-state");
            const message = expectThrows(
                module,
                () => module.load("/empty-state"),
                "load of an empty directory"
            );
            assert.match(message, /No compatible RhyDB state/);
        });

        test("dispose invalidates the handle and is idempotent", async () => {
            const module = await loadModule();

            writeFixture(module, "/dispose-input");
            module.FS.chdir("/dispose-input");
            const handle = module.preprocess("preprocessing_config.yaml");

            // The handle works before disposal.
            assert.equal(JSON.parse(module.info(handle)).sequenceCount, EXPECTED_SEQUENCE_COUNT);

            module.dispose(handle);

            // After disposal the handle is unknown.
            const message = expectThrows(
                module,
                () => module.info(handle),
                "info on a disposed handle"
            );
            assert.match(message, /Unknown RhyDB database handle/);

            // Disposing an already-disposed (or never-known) handle is a no-op.
            assert.doesNotThrow(() => module.dispose(handle));
            assert.doesNotThrow(() => module.dispose(123456));
        });

        test("preprocess reads a .zst-compressed NDJSON input", async () => {
            const module = await loadModule();

            const inputDir = "/zst-input";
            mkdirp(module, inputDir);
            for (const filename of [
                "database_config.yaml",
                "reference_genomes.json",
                "test_lineage_definition.yaml",
                "phylogenetic_tree.nwk",
            ]) {
                module.FS.writeFile(
                    `${inputDir}/${filename}`,
                    readFileSync(join(fixtureDir, filename), "utf8")
                );
            }

            // Provide the NDJSON input in zstd-compressed form only. The WASM build
            // detects the `.zst` ending and decompresses it (the `.xz` path is not
            // compiled into the browser target).
            module.FS.writeFile(
                `${inputDir}/input.ndjson.zst`,
                zstdCompress(readFileSync(join(fixtureDir, "input.ndjson")))
            );

            const preprocessingConfig = `
inputDirectory: "."
outputDirectory: "./output/"
ndjsonInputFilename: "input.ndjson.zst"
lineageDefinitionFilenames:
  - "test_lineage_definition.yaml"
phyloTreeFilename: "phylogenetic_tree.nwk"
referenceGenomeFilename: "reference_genomes.json"
`;
            module.FS.writeFile(`${inputDir}/preprocessing_config.yaml`, preprocessingConfig);

            module.FS.chdir(inputDir);
            const handle = module.preprocess("preprocessing_config.yaml");
            assert.equal(JSON.parse(module.info(handle)).sequenceCount, EXPECTED_SEQUENCE_COUNT);

            module.dispose(handle);
        });
    });
}
