
# svarg_generate_cxx, но с дефолт аргументами TASK_CONTEXT и FILE_PREFIX
# линкует таргет к btyelib
function(btye_generate_cxx)
  cmake_parse_arguments(ARGS "" "TARGET;TASK_CONTEXT;FILE_PREFIX" "" ${ARGN})
  if(NOT ARGS_TARGET)
    message(FATAL_ERROR "btye_generate_cxx: TARGET is required")
  endif()
  if(NOT ARGS_TASK_CONTEXT)
    set(ARGS_TASK_CONTEXT btye::task_context)
  endif()
  if(NOT ARGS_FILE_PREFIX)
    set(ARGS_FILE_PREFIX "#include <btye/task.hpp>")
  endif()
  svarg_generate_cxx(
      TARGET       ${ARGS_TARGET}
      TASK_CONTEXT ${ARGS_TASK_CONTEXT}
      FILE_PREFIX  "${ARGS_FILE_PREFIX}"
      ${ARGS_UNPARSED_ARGUMENTS}
  )
  target_link_libraries(${ARGS_TARGET} PUBLIC btyelib)
endfunction()
