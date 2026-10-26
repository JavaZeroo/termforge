# Experimental, explicit side-by-side ConPTY selection. No downloads/fallback.
add_library(termforge_conpty_backend INTERFACE)
target_link_libraries(termforge_conpty_backend INTERFACE bcrypt)

if(TERMFORGE_CONPTY_BACKEND_DIR)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The reviewed ConPTY package is Windows x64 only")
  endif()
  if(NOT TERMFORGE_BUILD_CANDIDATE_ADAPTER)
    message(FATAL_ERROR "Experimental side-by-side ConPTY requires TERMFORGE_BUILD_CANDIDATE_ADAPTER=ON")
  endif()
  if(NOT IS_ABSOLUTE "${TERMFORGE_CONPTY_BACKEND_DIR}")
    message(FATAL_ERROR "TERMFORGE_CONPTY_BACKEND_DIR must be an absolute directory")
  endif()
  foreach(name conpty.dll OpenConsole.exe)
    if(NOT EXISTS "${TERMFORGE_CONPTY_BACKEND_DIR}/${name}")
      message(FATAL_ERROR "Reviewed ConPTY package is incomplete: ${name}")
    endif()
    file(SHA256 "${TERMFORGE_CONPTY_BACKEND_DIR}/${name}" actual)
    if(name STREQUAL "conpty.dll")
      set(expected "feeef341d891643c62d30b6b07800bc70f0bc148f44cb8c3bee84aa557ae805a")
    else()
      set(expected "3d66b23d0a71bb8eed2b77edc8b9df9bf54ce6c8fb74c863a30e760997f80586")
    endif()
    if(NOT actual STREQUAL expected)
      message(FATAL_ERROR "Reviewed ConPTY identity mismatch: ${name}")
    endif()
  endforeach()
  target_compile_definitions(termforge_conpty_backend INTERFACE TERMFORGE_CONPTY_SIDEBYSIDE=1)
  add_custom_target(termforge_conpty_bundle
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:termforge_shell>/conpty-backend"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${TERMFORGE_CONPTY_BACKEND_DIR}/conpty.dll"
      "${TERMFORGE_CONPTY_BACKEND_DIR}/OpenConsole.exe"
      "${PROJECT_SOURCE_DIR}/third_party/conpty/manifest.json"
      "${PROJECT_SOURCE_DIR}/third_party/conpty/LICENSE"
      "$<TARGET_FILE_DIR:termforge_shell>/conpty-backend"
    VERBATIM)
endif()

function(termforge_use_conpty_backend target)
  target_link_libraries(${target} PRIVATE termforge_conpty_backend)
  if(TERMFORGE_CONPTY_BACKEND_DIR)
    add_dependencies(${target} termforge_conpty_bundle)
  endif()
endfunction()
