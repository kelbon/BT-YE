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
  GIT_TAG 2e5937376779c7f673399e65560a4047233633a2
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
