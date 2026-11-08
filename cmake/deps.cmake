include(cmake/get_cpm.cmake)

# cmake tools
CPMAddPackage(
  NAME CCOZY
  GITHUB_REPOSITORY kelbon/ccozy
  GIT_TAG v0.8.2
)

include(${CCOZY_SOURCE_DIR}/ccozy_tools.cmake)

CPMAddPackage(
  NAME SVARG
  GITHUB_REPOSITORY kelbon/svarg
  GIT_TAG 569c051038df78e861558b49997daa0a0f5baa14
)

if (btye_ENABLE_TESTING)
  CPMAddPackage(
    NAME MOKO3
    GITHUB_REPOSITORY kelbon/moko3
    GIT_TAG v0.10.4
  )
endif()

CPMAddPackage(
  NAME CLINOK
  GITHUB_REPOSITORY kelbon/clinok
  GIT_TAG v2.1.0
)

find_package(Threads REQUIRED)
