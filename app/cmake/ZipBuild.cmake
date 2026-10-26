# Zips the editor's build output folder into PACKAGE_DIR after a Windows build.
# The archive is named from git describe at build time, so a new tag takes effect without
# reconfiguring. PDB debug symbols are left out to keep the archive small.
#
# Expects: UTILS, GIT_EXECUTABLE, SOURCE_DIR, BUILD_DIR, PACKAGE_DIR, CONFIG

if(NOT CONFIG STREQUAL "Release")
  return()
endif()

include("${UTILS}")

get_git_describe("${GIT_EXECUTABLE}" "${SOURCE_DIR}" GIT_DESCRIBE)
get_build_platform(APP_PLATFORM_NAME)

set(ZIP_FILE "${PACKAGE_DIR}/TrenchBroom-${APP_PLATFORM_NAME}-${GIT_DESCRIBE}-${CONFIG}.zip")

file(GLOB ENTRIES RELATIVE "${BUILD_DIR}" "${BUILD_DIR}/*")
list(FILTER ENTRIES EXCLUDE REGEX "\\.(pdb|ilk|exp|lib)$")

file(MAKE_DIRECTORY "${PACKAGE_DIR}")
file(REMOVE "${ZIP_FILE}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar cf "${ZIP_FILE}" --format=zip ${ENTRIES}
  WORKING_DIRECTORY "${BUILD_DIR}"
  RESULT_VARIABLE ZIP_RESULT)
if(NOT ZIP_RESULT EQUAL 0)
  message(FATAL_ERROR "Could not create ${ZIP_FILE}")
endif()

message(STATUS "Packaged build: ${ZIP_FILE}")
