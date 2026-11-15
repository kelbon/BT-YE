set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

btye_generate_cxx(
    TARGET    btye_kv_api
    SPECS_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    OUT_DIR   ${CMAKE_CURRENT_BINARY_DIR}/generated
    SPECS     kv.yaml
)

add_library(btye_kv_store STATIC store.cpp)
target_link_libraries(btye_kv_store PUBLIC btye_kv_api)

add_executable(btye_kv main.cpp)
target_link_libraries(btye_kv PRIVATE btye_kv_store)

if(btye_ENABLE_TESTING)
  add_executable(test_btye_kv test_store.cpp)
  target_link_libraries(test_btye_kv PRIVATE btye_kv_store moko3lib)
  add_test(NAME test_btye_kv COMMAND test_btye_kv)
endif()
