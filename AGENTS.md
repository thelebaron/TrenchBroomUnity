# Repository Guidelines

## Project Structure & Module Organization
TrenchBroom is a C++17/Qt 6 editor. Core UI code lives in `app/`, with widgets, controllers, and Qt resources under `app/resources`. Shared engine logic, map formats, and utilities are in `common/`. Purpose-specific libraries reside in `lib/` (for example `lib/kdl` and `lib/upd` have their own sources plus `test/` folders). Build artefacts should stay in `build/`, and web documentation assets are served from `www/`. Scripts such as `dump-shortcuts/` generate editor metadata during development.

## Build, Test, and Development Commands
Sync submodules before configuring: `git submodule update --init --recursive`. Configure a build with CMake, pointing `CMAKE_PREFIX_PATH` at your Qt install, e.g. `cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="cmake/packages;<QT_INSTALL_DIR>/gcc_64"`. Build the editor with `cmake --build build --target TrenchBroom`. On multi-config generators (Visual Studio, Xcode), add `--config Debug`. The produced binary lands in `build/app/TrenchBroom` (or the configuration subfolder). Use the CI scripts (`CI-linux.sh`, `CI-macos.sh`, `CI-windows.bat`) as references for packaging nuances.

## Coding Style & Naming Conventions
Follow `Coding Standards.md` and enforce formatting with the repo’s `.clang-format` and `.clang-tidy`. Namespaces stay lowercase, classes use CamelCase, and functions or local variables use camelCase with private members prefixed by `m_`. Prefer `auto` and value semantics, avoid exceptions in favour of `Result`/`Error`, and structure helper code in anonymous namespaces. Run `clang-format` on touched files before raising a PR.

## Testing Guidelines
Unit tests rely on Catch2 v3. The main suites live in `common/test/src` with files prefixed `tst_`, while `lib/*/test` covers library-specific behaviour. Build tests with `cmake --build build --target common-test vm-test kdl-test upd-test`. Execute them directly, e.g. `./build/common/test/common-test --reporter compact --success`. Add focused tests whenever you change map parsing, geometry, or serialization—regressions in those areas surface quickly.

## Commit & Pull Request Guidelines
Recent history follows short, imperative commits ("Update kdl and upd to be static libraries", "Uninstall openexr"). Keep subjects under ~72 characters, mention affected modules, and note issue IDs when applicable. PRs should explain the problem, outline the fix, list verification steps or test binaries, and include screenshots for UI-facing changes. Call out dependency or submodule updates explicitly, and make sure CI and the `common-test` suite pass before requesting review.
