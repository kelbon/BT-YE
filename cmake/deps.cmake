include(cmake/get_cpm.cmake)

# cmake tools
CPMAddPackage(
  NAME CCOZY
  GITHUB_REPOSITORY kelbon/ccozy
  GIT_TAG v0.9.0
)

include(${CCOZY_SOURCE_DIR}/ccozy_tools.cmake)

CPMAddPackage(
  NAME SVARG
  GITHUB_REPOSITORY kelbon/svarg
  GIT_TAG 0008061893f2d7308ce9c69153b57fafa14e4132
)

if (btye_ENABLE_TESTING)
  CPMAddPackage(
    NAME MOKO3
    GITHUB_REPOSITORY kelbon/moko3
    GIT_TAG v0.11.0
  )
endif()

CPMAddPackage(
  NAME CLINOK
  GITHUB_REPOSITORY kelbon/clinok
  GIT_TAG v2.1.0
)

find_package(Threads REQUIRED)
