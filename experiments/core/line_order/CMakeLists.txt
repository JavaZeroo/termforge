cmake_minimum_required(VERSION 3.20)
project(termforge_line_order_r164 LANGUAGES CXX)
# Isolated preparation: neither the product build nor CTest includes this target.
set(R164_GENERATED_DIR "" CACHE PATH "Explicit generated directory bound by the reviewed plan")
if(R164_GENERATED_DIR STREQUAL "" OR NOT IS_ABSOLUTE "${R164_GENERATED_DIR}")
  message(FATAL_ERROR "Materialize with prepare.py and select that plan's absolute R164_GENERATED_DIR")
endif()
file(REAL_PATH "${R164_GENERATED_DIR}" R164_GENERATED_ABSOLUTE)
set(R164_SELECTION_HEADER "${R164_GENERATED_ABSOLUTE}/selection.hpp")
set(R164_SEARCH_HEADER "${R164_GENERATED_ABSOLUTE}/search.hpp")
foreach(header IN ITEMS "${R164_SELECTION_HEADER}" "${R164_SEARCH_HEADER}")
  if(NOT EXISTS "${header}" OR IS_DIRECTORY "${header}")
    message(FATAL_ERROR "Both reviewed generated caller headers must exist: ${header}")
  endif()
endforeach()
# Absolute macro includes also let compiler dependency tracking observe the
# actual reviewed headers, independently of the ordinary include search order.
add_executable(r164_probe EXCLUDE_FROM_ALL probe.cpp "${R164_SELECTION_HEADER}" "${R164_SEARCH_HEADER}")
target_include_directories(r164_probe PRIVATE . ..)
target_compile_definitions(r164_probe PRIVATE
  "R164_SELECTION_HEADER=\"${R164_SELECTION_HEADER}\""
  "R164_SEARCH_HEADER=\"${R164_SEARCH_HEADER}\"")
target_compile_features(r164_probe PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(r164_probe PRIVATE /W4 /utf-8)
else()
  target_compile_options(r164_probe PRIVATE -Wall -Wextra -Wpedantic)
endif()
