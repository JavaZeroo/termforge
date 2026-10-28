# App-local deployment of the manifest-pinned Microsoft desktop CRT.
include_guard(GLOBAL)

function(termforge_use_msvc_runtime target)
  if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The fixed Microsoft CRT requires an MSVC Windows x64 build")
  endif()
  if(NOT TARGET ${target})
    message(FATAL_ERROR "CRT target does not exist: ${target}")
  endif()
  if(NOT TERMFORGE_MSVC_RUNTIME_DIR OR NOT IS_ABSOLUTE "${TERMFORGE_MSVC_RUNTIME_DIR}")
    message(FATAL_ERROR "TERMFORGE_MSVC_RUNTIME_DIR must name the absolute fixed-runtime directory")
  endif()

  # The compiler's 19.x self-version is not the installed VC Tools 14.x
  # directory version. Use the selected toolset metadata, then the actual
  # selected compiler path when CMake leaves its default toolset unset.
  if(CMAKE_VS_PLATFORM_TOOLSET_VERSION)
    set(toolset_version "${CMAKE_VS_PLATFORM_TOOLSET_VERSION}")
  else()
    file(TO_CMAKE_PATH "${CMAKE_CXX_COMPILER}" compiler_path)
    if(compiler_path MATCHES "/VC/Tools/MSVC/(14[.][0-9]+[.][0-9]+)/bin/[^/]+/[^/]+/cl[.]exe$")
      set(toolset_version "${CMAKE_MATCH_1}")
    else()
      message(FATAL_ERROR "Cannot identify the selected VC Tools directory version")
    endif()
  endif()
  if(NOT toolset_version MATCHES "^14[.][0-9]+[.][0-9]+$")
    message(FATAL_ERROR "The selected VC Tools version must have the form 14.minor.build")
  endif()

  get_filename_component(repository "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
  set(deploy_tool "${repository}/tools/msvc_runtime.py")
  set(manifest "${repository}/third_party/msvc_runtime/manifest.json")
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${deploy_tool}" verify
      --source "${TERMFORGE_MSVC_RUNTIME_DIR}" --toolset-version "${toolset_version}"
    RESULT_VARIABLE runtime_verification
    OUTPUT_QUIET ERROR_QUIET TIMEOUT 30)
  if(NOT runtime_verification STREQUAL "0")
    message(FATAL_ERROR "Fixed CRT input identity or VC Tools compatibility validation failed")
  endif()

  # Always validate and repair app-local files, including builds that do not
  # relink. No installer or network operation belongs in this build step.
  add_custom_target(${target}_msvc_runtime
    COMMAND "${Python3_EXECUTABLE}" "${deploy_tool}" deploy
      --source "${TERMFORGE_MSVC_RUNTIME_DIR}"
      --destination "$<TARGET_FILE_DIR:${target}>"
      --toolset-version "${toolset_version}"
    DEPENDS "${deploy_tool}" "${manifest}"
    VERBATIM)
  add_dependencies(${target} ${target}_msvc_runtime)
endfunction()
