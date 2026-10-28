# Fixed Microsoft ConPTY deployment for the selected Windows product target.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/CopyConptyBackend.cmake")

function(termforge_use_bundled_conpty target)
  if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The fixed ConPTY package requires Windows x64")
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "ConPTY target does not exist: ${target}")
  endif()
  if(NOT TERMFORGE_CONPTY_BACKEND_DIR OR NOT IS_ABSOLUTE "${TERMFORGE_CONPTY_BACKEND_DIR}")
    message(FATAL_ERROR "TERMFORGE_CONPTY_BACKEND_DIR must name the absolute fixed-package directory")
  endif()
  get_filename_component(repository "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
  set(manifest "${repository}/third_party/conpty/manifest.json")
  set(license "${repository}/third_party/conpty/LICENSE")
  termforge_validate_conpty_pair("${TERMFORGE_CONPTY_BACKEND_DIR}" "${manifest}" "${license}")
  target_compile_definitions(${target} PRIVATE TERMFORGE_CONPTY_SIDEBYSIDE=1)
  target_link_libraries(${target} PRIVATE bcrypt)

  # An always-run dependency also repairs a missing bundle when no source
  # requires relinking. The script checks the inputs again before copying.
  add_custom_target(${target}_conpty_bundle
    COMMAND "${CMAKE_COMMAND}"
      "-DCONPTY_BUNDLE_INPUT_DIRECTORY=${TERMFORGE_CONPTY_BACKEND_DIR}"
      "-DCONPTY_BUNDLE_MANIFEST=${manifest}"
      "-DCONPTY_BUNDLE_LICENSE=${license}"
      "-DCONPTY_BUNDLE_OUTPUT_DIRECTORY=$<TARGET_FILE_DIR:${target}>/conpty-backend"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CopyConptyBackend.cmake"
    DEPENDS "${TERMFORGE_CONPTY_BACKEND_DIR}/conpty.dll"
      "${TERMFORGE_CONPTY_BACKEND_DIR}/OpenConsole.exe" "${manifest}" "${license}"
    VERBATIM)
  add_dependencies(${target} ${target}_conpty_bundle)
endfunction()
