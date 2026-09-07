# Repository Guidelines

## Project Structure & Module Organization

dagP is a mixed C/C++ directed acyclic graph partitioner. Core graph types, readers, traversal, clustering, and utilities live in `src/common/`. Recursive bisection and multilevel partitioning code lives in `src/recBisection/`; `rMLGP.c` provides the main CLI and `dagP.h` exposes the library API. `src/useapi.cpp` is a small integration example. Sample input graphs are stored in `data/`. SCons places generated executables in `exe/`, the static library in `lib/`, and object files in the build tree; do not commit these outputs.

## Architecture & Code Documentation

Start with [docs/README.md](docs/README.md) for an overview and reading order.

- [docs/architecture.md](docs/architecture.md) maps source modules and entry points, explains graph storage and indexing, and describes execution flow and memory ownership.
- [docs/algorithms.md](docs/algorithms.md) explains the edge-cut objective, balance and acyclicity constraints, recursive bisection, multilevel coarsening, initial partitioning, and refinement.
- [docs/usage.md](docs/usage.md) covers build configuration, supported CLI options, graph formats, API integration, and implementation limitations.

The guides describe baseline commit `333c3f2822cd300983c9ad71ed6c8961e4d11f1d`; check current source before assuming every detail still applies. For the main algorithm flow, follow `rVCycle` in `src/recBisection/rvcycle.c` into `VCycle2way` in `src/recBisection/vcycle2way.c`. Keep the recursive bisection tree distinct from the coarsening chain within each bisection.

## Build, Test, and Development Commands

Install SCons, then create a local configuration before building:

```sh
cp config.py.template config.py
scons
```

`scons` builds the default `exe/rMLGP` and `exe/ginfo` targets. Use `scons all` to explicitly build the library, CLI, and tools, or `scons -j8` for a parallel build. Configuration variables may be supplied in `config.py`; for example, enable symbols with `debug = 1`, or configure METIS/Scotch through `metis`, `scotch`, `extincludes`, `extlibs`, and `extlibpath`.

Run `scons test` for objective unit tests and Python 3 CLI integration tests in
`tests/`. After changes, also run a smoke test:

```sh
./exe/rMLGP data/2mm_10_20_30_40.dot 4 --print 1 --ratio 1.1
./exe/ginfo data/2mm_10_20_30_40.dot
```

The reader prefers cached `.bin` inputs. Remove a graph's generated `.bin` file when validating edits to its source `.dot`, `.dg`, or `.mtx` file.

## Coding Style & Naming Conventions

Follow the style of the file being edited: four-space indentation, K&R braces, and compact C functions. Keep public declarations in matching headers and use the established domain types (`idxType`, `ecType`, `dgraph`, `MLGP_option`) rather than introducing incompatible primitives. Existing names mix camel case (`readDGraph`) and lowercase prefixes (`dagP_*`); preserve the convention of the surrounding module. Builds enable `-Wall` and strict prototype checks, so address new warnings. No formatter is configured; avoid unrelated reformatting.

## Testing Guidelines

Exercise every changed reader with a representative format and verify partition count, acyclicity, balance, and objective output where applicable. Add small reproducible fixtures under `data/` or generate temporary fixtures in `tests/`. Extend `scons test` for regressions.

## Commit & Pull Request Guidelines

History favors short, lowercase, imperative summaries such as `minor fix: ...` and references issues inline (`fixes issue #2`). Keep commits focused and explain algorithmic or allocation changes in the body. Pull requests should describe the problem, approach, build configuration, and exact verification commands; link relevant issues and include before/after output for behavior or performance changes.
