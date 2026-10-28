# Shared configure/build check for the immutable pair in the package manifest.
function(termforge_validate_conpty_pair directory manifest license)
  if(NOT IS_ABSOLUTE "${directory}" OR NOT IS_DIRECTORY "${directory}")
    message(FATAL_ERROR "Fixed ConPTY input must be an absolute directory")
  endif()
  file(READ "${manifest}" metadata)
  foreach(name conpty.dll OpenConsole.exe)
    if(NOT EXISTS "${directory}/${name}")
      message(FATAL_ERROR "Fixed ConPTY package is incomplete: ${name}")
    endif()
    string(JSON expected GET "${metadata}" files "${name}")
    file(SHA256 "${directory}/${name}" actual)
    if(NOT actual STREQUAL expected)
      message(FATAL_ERROR "Fixed ConPTY identity mismatch: ${name}")
    endif()
  endforeach()
  string(JSON expected_license GET "${metadata}" bundled_license_sha256)
  file(SHA256 "${license}" actual_license)
  if(NOT actual_license STREQUAL expected_license)
    message(FATAL_ERROR "Fixed ConPTY license identity mismatch")
  endif()
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  termforge_validate_conpty_pair("${CONPTY_BUNDLE_INPUT_DIRECTORY}"
    "${CONPTY_BUNDLE_MANIFEST}" "${CONPTY_BUNDLE_LICENSE}")
  if(NOT IS_ABSOLUTE "${CONPTY_BUNDLE_OUTPUT_DIRECTORY}")
    message(FATAL_ERROR "Fixed ConPTY output must be an absolute directory")
  endif()
  file(MAKE_DIRECTORY "${CONPTY_BUNDLE_OUTPUT_DIRECTORY}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CONPTY_BUNDLE_INPUT_DIRECTORY}/conpty.dll"
    "${CONPTY_BUNDLE_INPUT_DIRECTORY}/OpenConsole.exe"
    "${CONPTY_BUNDLE_MANIFEST}" "${CONPTY_BUNDLE_LICENSE}"
    "${CONPTY_BUNDLE_OUTPUT_DIRECTORY}"
    RESULT_VARIABLE copy_status)
  if(NOT copy_status EQUAL 0)
    message(FATAL_ERROR "Fixed ConPTY bundle copy failed: ${copy_status}")
  endif()
endif()
