# TrenchBroom Development Context

## Project Overview

TrenchBroom is a modern cross-platform level editor for Quake-engine based games. It's a C++ application with a Qt6-based user interface that supports editing levels for various games including Quake, Quake 2, Quake 3, Hexen 2, Daikatana, and generic custom engines.

### Key Features
- Full support for editing in 3D and in up to three 2D views
- High performance renderer with support for huge maps
- Unlimited Undo and Redo
- Robust brush editing with vertex manipulation, clipping, scaling, and CSG operations
- Entity editing with support for FGD and DEF files
- Support for multiple game formats (Quake, Quake 2, Quake 3, Hexen 2, Daikatana, and Generic)

### Architecture
- **Main Framework**: Qt6 for the user interface
- **Language**: C++17 (with plans to move to C++20)
- **Build System**: CMake
- **Package Manager**: vcpkg for dependencies (except Qt)
- **Code Style**: Uses clang-format and clang-tidy for enforcement

### Dependencies
- Qt 6.7+ (for the GUI)
- Assimp (for 3D model loading)
- FreeType (for font rendering)
- FreeImage (for image processing)
- GLEW (for OpenGL extensions)
- Catch2 (for testing)
- FMT (for formatting)
- Miniz (for compression)
- TinyXML2 (for XML parsing)
- FastFloat (for floating-point parsing on macOS)

## Building and Running

### Prerequisites
1. Clone with submodules: `git clone --recursive https://github.com/TrenchBroom/TrenchBroom.git`
2. Install Qt 6.7+ (6.9+ for macOS)
3. Install CMake 3.25+
4. Install pandoc
5. Platform-specific tools (Visual Studio 2022 for Windows, GCC 13+ for Linux, Xcode for macOS)

### Build Process
1. Create a `build` directory in the project root
2. Configure with CMake:
   - **Windows**: `cmake .. -G"Visual Studio 17 2022" -T v143 -A x64 -DCMAKE_PREFIX_PATH="<QT_INSTALL_DIR>\msvc2022_64"`
   - **Linux**: `cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="cmake/packages;<QT_INSTALL_DIR>/gcc_64"`
   - **macOS**: `cmake .. -GNinja -DCMAKE_PREFIX_PATH="<QT_INSTALL_DIR>/macos"`
3. Build: `cmake --build . --target TrenchBroom`

### Testing
The project uses Catch2 for unit testing. Tests can be built and run using the CMake system.

## Development Conventions

### Coding Standards
- Use lowercase for top-level namespaces (`mdl`, `render`, etc.)
- Use camel case for class names (`MyUsefulClass`)
- Functions, methods, variables, and parameters use camel case starting with lowercase
- Private member variables are prefixed with `m_`
- Constants use camel case starting with uppercase
- Follow "almost always auto" style for variable declarations
- Use `const` wherever possible
- Prefer value semantics over reference semantics
- Use `std::optional` over magic constants
- Use `std::variant` over inheritance when appropriate
- Use RAII and smart pointers over raw pointers
- Avoid exceptions - use `Result` and `Error` instead

### Formatting
- Code formatting is enforced using `clang-format` with rules in `.clang-format`
- Style checking is done using `clang-tidy` with rules in `.clang-tidy`
- Both are enforced by CI

### Compilation Time Optimization
- Avoid including headers in other headers
- Use forward declarations wherever possible
- Limit includes to what's necessary in header files

### Project Structure
- `/app` - Main application code
- `/common` - Common utilities and shared code
- `/lib` - External libraries and third-party code (kdl, stackwalker, upd, vm)
- `/dump-shortcuts` - Utility for dumping keyboard shortcuts
- `/cmake` - CMake build utilities
- `/vcpkg` - Package manager for dependencies
- `/www` - Web-related content
- `/app/resources` - Application resources (graphics, shaders, etc.)

### Code Organization
- Each class should have its own header file
- Class members are ordered: type aliases and static const members, member variables, constructors/destructors, operators, public functions, protected functions, private functions, and extension interface
- Prefer anonymous namespaces over private member functions for helper functions
- Follow left-to-right style: `auto entity = Entity{...};` instead of `Entity entity{...};`