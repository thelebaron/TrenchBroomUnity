# Repository Guidelines

## Project Structure & Module Organization
TrenchBroom is a C++17/Qt 6 editor. Core UI code lives in `app/`, with widgets, controllers, and Qt resources under `app/resources`. Shared engine logic, map formats, and utilities are in `common/`. Purpose-specific libraries reside in `lib/` (for example `lib/kdl` and `lib/upd` have their own sources plus `test/` folders). Web documentation assets are served from `www/`. Scripts such as `dump-shortcuts/` generate editor metadata during development.

## Build (Windows)

### Prerequisites
- **Visual Studio 2026 (v18)** with "Desktop development with C++" workload
- **Qt 6** installed under `C:\Qt` with the `msvc2022_64` kit (e.g. `C:\Qt\6.10.2\msvc2022_64`)
- **CMake** (bundled with VS or standalone)
- **Pandoc** (required for help docs)

### Quick Build
Run the automated script from the repo root:
```bat
buildwindows.bat
```
This auto-detects Qt and Visual Studio, configures, and builds a Release binary.

### Manual Steps

1. **Sync submodules**
   ```bat
   git submodule update --init --recursive
   ```

2. **Configure** (one-time, or after CMake changes)
   ```bat
   cmake -S . -B build-vs18 -G "Visual Studio 18 2026" -T v143 -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.10.2\msvc2022_64" -DTB_ENABLE_PCH=0 -DTB_ENABLE_CCACHE=0
   ```

3. **Build the editor**
   ```bat
   cmake --build build-vs18 --target TrenchBroom --config Release
   ```

4. **Output binary**
   ```
   build-vs18\app\Release\TrenchBroom.exe
   ```

### Build Targets
| Target | Command |
|---|---|
| Editor | `cmake --build build-vs18 --target TrenchBroom --config Release` |
| All tests | `cmake --build build-vs18 --target common-test --config Debug` |
| Single test lib | `cmake --build build-vs18 --target kdl-test --config Debug` |

### Running Tests
```bat
build-vs18\common\test\Debug\common-test.exe --reporter compact --success
```

### Build Directory
The only build directory is `build-vs18/`. All other build folders are stale and should be deleted.

## Coding Style & Naming Conventions
Follow `Coding Standards.md` and enforce formatting with the repo's `.clang-format` and `.clang-tidy`. Namespaces stay lowercase, classes use CamelCase, and functions or local variables use camelCase with private members prefixed by `m_`. Prefer `auto` and value semantics, avoid exceptions in favour of `Result`/`Error`, and structure helper code in anonymous namespaces. Run `clang-format` on touched files before raising a PR.

## Testing Guidelines
Unit tests rely on Catch2 v3. The main suites live in `common/test/src` with files prefixed `tst_`, while `lib/*/test` covers library-specific behaviour. Add focused tests whenever you change map parsing, geometry, or serialization—regressions in those areas surface quickly.

## Commit & Pull Request Guidelines
Recent history follows short, imperative commits ("Update kdl and upd to be static libraries", "Uninstall openexr"). Keep subjects under ~72 characters, mention affected modules, and note issue IDs when applicable. PRs should explain the problem, outline the fix, list verification steps or test binaries, and include screenshots for UI-facing changes. Call out dependency or submodule updates explicitly, and make sure CI and the `common-test` suite pass before requesting review.
