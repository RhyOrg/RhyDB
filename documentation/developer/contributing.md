# Contributing to RhyDB

All commands (except linting) which are regularly invoked when working with the RhyDB source are contained in `Makefile`.

## Build Requirements

For building RhyDB you require the following tools:
- cmake (installable via apt / homebrew)
- uv (installable via pip, or see https://docs.astral.sh/uv/)
  - used to install the build tool conan which we use to install dependencies
  - used to create python bindings
- clang-format (for `make format-cpp`)
  - We currently use `clang-format-19` as this is the version in `debian:oldstable`, which we use as our base build image in CI.

## Building

Use `make build/Debug/rhydb` or `make build/Release/rhydb` to build RhyDB.

To limit the number of threads during build use e.g. `export CMAKE_BUILD_PARALLEL_LEVEL=4;` before invoking the Makefile targets. We default to using 16 threads

Executables are located in `build/` upon a successful build.

The conan center has been updated, if you installed conan before November 2024 you might need to update your center: `conan remote update conancenter --url="https://center2.conan.io"`

We took the approach to scan directories for .cpp files to include instead of listing them manually in the
CMakeLists.txt. This has the advantage that we don't need to maintain a list of files in the CMakeLists.txt.

It has the disadvantage that after a successful build on local machines, CMake can't detect whether files were
added or deleted. Therefore, a `.src_file_list` is built as part of the `Makefile` in the directory root. This
will retrigger the Makefile target for the cmake configuration.

### Conan

We use Conan to install dependencies for local development. See Dockerfile for how to set up Conan and its requirements.
This has been tested on Ubuntu 22.04 and is not guaranteed to work on other systems.

The Conan profile (myProfile) on your system might differ: Create a new profile `~/.conan2/profiles/myProfile`

```shell
conan profile detect
```

Copy `conanprofile.example` to `conanprofile` and insert the values of `os`, `os_build`, `arch` and `arch_build` from
myProfile.

Build RhyDB in `./build`. This build will load and build the required libraries to `~/.conan2/data/` (can not be set by
hand).

```shell
make dependencies
```

## Building for WebAssembly (Browser)

RhyDB can be compiled to WebAssembly to run preprocessing and SaneQL queries in the browser.

### Prerequisites

- The Emscripten SDK, activated so that `emcc`, `em++`, and `emcmake` are on your `PATH`.
  Install it: https://emscripten.org/docs/getting_started/downloads.html
- cmake, ninja, and uv (same as the native build).

### Build

From the repository root:

```shell
make wasm
```

### Test

`wasm/test` contains a Node.js smoke test that runs the built module end-to-end (preprocess,
query, save, load) against the `testBaseData/unitTestDummyDataset` fixture:

```shell
make wasm-test
```

See [`wasm/README.md`](../../wasm/README.md) for the exposed API, the example app, and the
cross-origin isolation headers required to serve the pthread-enabled build.

## Building Python Bindings

RhyDB provides Python bindings via Cython. The bindings wrap the core C++ `Database` class.

### Prerequisites

- Python 3.8+
- Cython >= 3.0
- C++ dependencies built via conan (see [Building](#building))

### Build Steps

First, build the C++ dependencies:

```shell
make dependencies
```

Then install the Python package:

```shell
pip install .
```

For development (editable install):

```shell
pip install -e .
```

The build process:
1. Locates pre-built conan dependencies in `build/Release/generators` or `build/Debug/generators`
2. Runs CMake with `-DBUILD_PYTHON_BINDINGS=ON`
3. Builds the C++ library and Cython extension
4. Installs to your Python environment

## Running RhyDB Locally with CLion

`.run` contains run configurations for CLion that are ready to use.
They assume that you configured CMake in CLion to use `./build` as build directory.
CLion should be able to detect those files automatically.

## Testing

Before committing, run `make ci` to execute the formatter and all tests (unit and e2e) locally.

### Unit Tests

For testing, we use the framework [gtest](http://google.github.io/googletest/)
and [gmock](http://google.github.io/googletest/gmock_cook_book.html) for mocking. Tests are built using the same Makefile
as the production code: `make build/Debug/rhydb_test` / `make build/Release/rhydb_test`.

We use the convention, that each tested source file has its own test file, ending with `*.test.cpp`. The test file is
placed in the same folder as the source file. If the function under test is described in a header file, the test file is
located in the corresponding source folder.

To run all tests, run

```shell
build/Release/rhydb_test
```

For linting we use clang-tidy. The config is stored in `.clang-tidy`. The CI/dependency image uses clang-tidy 23, including `readability-trailing-comma` for multi-line initializer lists.

When pushing to GitHub, the linter will run on all changed files in the dependencies image, which has clang-tidy installed.
To run the same changed-files lint path locally, use `make lint-changes`.

On main, or if requested using the `trigger-linter` label, the linter will run on all files. This builds a separate Docker image, which runs the formatter. (This was a workaround, because
building with clang-tidy under alpine was not possible yet. Should be changed in [#1167](https://github.com/GenSpectrum/LAPIS-SILO/issues/1167))

### Functional End-To-End Tests

End-to-end tests are located in `/endToEndTests`. Those tests are used to verify the overall functionality of the RhyDB
queries. To execute the tests on a `RHYDB_IMAGE`:

- `cd endToEndTests`
- `RHYDB_IMAGE=ghcr.io/rhyorg/rhydb docker compose -f docker-compose-for-tests-preprocessing-from-ndjson.yml up`
- `RHYDB_IMAGE=ghcr.io/rhyorg/rhydb docker compose -f docker-compose-for-tests-api.yml up -d --wait`
- `npm install`
- `RHYDB_URL=localhost:8080 npm run test`
- `RHYDB_IMAGE=ghcr.io/rhyorg/rhydb docker compose -f docker-compose-for-tests-api.yml down`

## Local Debugging

We recommend using [LLDB](https://lldb.llvm.org/) with CMake for local debugging.

If you are using VSCode we recommend installing the extensions listed in the `.vscode.extensions.json`. This will add a new icon for CMake, to debug using CMake and LLDB first configure the project (by selecting configure in the CMake panel) and update the CMake `settings.json` to use LLDB. This means adding the following to your settings.json.
```
"cmake.debugConfig": {
    "MIMode": "lldb"
  }
```

## Notes On Building The Docker Image

Building Docker images locally relies on the local Docker cache.
Docker will cache layers, and it will cache the dependencies built by Conan via cache mounts.

However, cache mounts don't work in GitHub Actions (https://github.com/docker/build-push-action/issues/716),
so there we only rely on Docker's layer cache via Docker's gha cache backend.

## Creating A Release

This project uses [Release Please](https://github.com/google-github-actions/release-please-action) to generate releases.
On every commit on the `main` branch, it will update a Pull Request with a changelog.
When the PR is merged, the release will be created.
Creating a release means:

- A new Git tag is created.
- The Docker images of RhyDB are tagged with the new version.
  - Suppose the created version is `2.4.5`, then it creates the tags `2`, `2.4` and `2.4.5` on the current `latest` image.

The changelog and the version number are determined by the commit messages.
Therefore, commit messages should follow the [Conventional Commits](https://www.conventionalcommits.org/) specification.
Also refer to the Release Please documentation for more information on how to write commit messages
or see [Conventional Commits](#conventional-commits) below.

## Code Style Guidelines

### Naming

We mainly follow the styleguide provided by [google](https://google.github.io/styleguide/cppguide.html), with a few
additions. The naming is enforced by clang-tidy. Please refer to `.clang-tidy` for more details on naming inside the
code. Clang-tidy can not detect filenames. We decided to use snake_case for filenames.

### Formatting

We use clang-format as a code formatter. To run locally install [clang-format](https://github.com/llvm/llvm-project/releases/), update your PATH variables and run

```bash
find src -iname '*.h' -o -iname '*.cpp' | xargs clang-format -i
```

Note that your clang-format version should be exactly the same as that used by CI (based on `debian:oldstable`) for tests to pass. Currently we use `19.1.7`.

### Includes

The includes are sorted in the following order:

1. Corresponding header file (for source files)
2. System includes
3. External includes
4. Internal includes

Internal includes are marked by double quotes. External includes are marked by angle brackets.

## Conventional Commits

We follow the [conventional commits](https://www.conventionalcommits.org/) guidelines for commit messages.
This will allow to automatically generate a changelog.

Please make sure to mention a reference in the commit message so that the generated changelog can be linked to
either an issue or a pull request.
This can be done via:

- Referencing an issue via "resolves" to the commit footer (preferred solution):

```
feat: my fancy new feature

some description

resolves #123
```

- Referencing an issue in the commit message header: `feat: my fancy new feature (#123)`
- Squash-merging on GitHub and adding the PR number to the commit message
  (useful for smaller changes that don't have a corresponding issue).

We use [commitlint](https://commitlint.js.org/) to enforce the commit message format.
The configuration lives in `.commitlintrc.js`.
CI checks all commit messages of a pull request and additionally the pull request title itself
(see `.github/workflows/commitlint.yml`), so the title must also be a valid conventional commit header.

If a scope is given, it must be one of the allowed scopes:

- any git-tracked top-level directory of the repository (e.g. `app`, `performance`, `python`, `wasm`),
- any subdirectory of `src/rhydb/` (e.g. `query_engine`, `storage`, `preprocessing`),
- or the literal value `rhydb`.

Both directory lists are determined when commitlint runs (via `git ls-tree`, see `.commitlintrc.js`),
so new directories are picked up automatically and results are consistent between local runs and CI.
Gitignored directories such as `build/` are not valid scopes.
A commit message without a scope (e.g. `feat: my fancy new feature`) is also allowed.

Pick the most specific scope that covers the change:
if the change is restricted to a single subsystem or subdirectory, use that directory as the scope
(e.g. `fix(query_engine): ...` for a change confined to `src/rhydb/query_engine/`).
If the change spans multiple subsystems, use `rhydb`.

The last commit message can be checked with

```shell
npm run commitlint:last-commit
```

To check commit messages of a branch to the commit where it branches off from `main`, run

```shell
npm run commitlint:merge-base
```

### Testing The Generated Changelog

To test the generated changelog, run

```shell
npm run release-please-dry-run -- --token=<GitHub PAT> --target-branch=<name of the upstream branch>
```

where

- `<GitHub PAT>` is a GitHub Personal Access Token. It doesn't need any permissions.
- `<name of the upstream branch>` is the name of the branch for which the changelog should be generated.

**NOTE: This command does not respect local changes. It will pull the commit messages from the remote repository.**
