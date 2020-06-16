cmake_minimum_required(VERSION 2.9)
project(debop)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

file(MAKE_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
file(MAKE_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/wrappers)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

find_package(LLVM REQUIRED)
find_package(Clang REQUIRED)

set(CMAKE_CXX_FLAGS "${LLVM_COMPILE_FLAGS} ${CMAKE_CXX_FLAGS} -std=c++11 -lstdc++ -w -Os -march=native -fexceptions -pthread")
if(CMAKE_BUILD_TYPE MATCHES DEBUG)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fsanitize=address")
endif(CMAKE_BUILD_TYPE MATCHES DEBUG)

message(STATUS "CXX flags: " ${CMAKE_CXX_FLAGS})

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CLANG_INCLUDE_DIRS})
include_directories(include)
include_directories(/home/qxin6/llvm-project/clang/include) #Change to your own path!
include_directories(/home/qxin6/llvm-project/build/tools/clang/include) #Change to your own path!

add_executable(reducer
  src/cpp/reducer/Main.cpp
  src/cpp/reducer/core/Frontend.cpp
  src/cpp/reducer/core/SourceManager.cpp
  src/cpp/reducer/core/Transformation.cpp
  src/cpp/reducer/core/Reduction.cpp
  src/cpp/reducer/core/GlobalReduction.cpp
  src/cpp/reducer/core/LocalReduction.cpp
  src/cpp/reducer/core/DeadcodeElimination.cpp
  src/cpp/reducer/core/Reformat.cpp
  src/cpp/reducer/core/ProbabilisticModel.cpp
  src/cpp/reducer/utils/FileManager.cpp
  src/cpp/reducer/utils/IntegrationManager.cpp
  src/cpp/reducer/utils/OptionManager.cpp
  src/cpp/reducer/utils/Report.cpp
  src/cpp/reducer/utils/Profiler.cpp
  src/cpp/reducer/utils/StatsManager.cpp
  )

add_custom_target(lib ALL
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/lib/wrappers/* ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/wrappers/
)

target_link_libraries(reducer ${CLANG_LIBS} ${LLVM_LIBS_CORE} ${LLVM_LDFLAGS})

# for make test
enable_testing()
add_subdirectory(test)

# for documents
option(BUILD_DOCS "Build and Install Documents (Requires Doxygen)")
if (BUILD_DOCS)
  find_package(Doxygen)
  if (DOXYGEN_FOUND)
    # set input and output files
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    # request to configure the file
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    message("Doxygen build started")

    # note the option ALL which allows to build the docs together with the application
    add_custom_target(doc ALL
      COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      COMMENT "Generating API documentation with Doxygen"
      VERBATIM )
  else (DOXYGEN_FOUND)
    message("Doxygen need to be installed to generate the doxygen documentation")
  endif (DOXYGEN_FOUND)
endif(BUILD_DOCS)
