cmake_minimum_required(VERSION 3.21)
project(saturday-testgen VERSION 0.1
                  DESCRIPTION "course assignment for Software Analyze and Test 2025"
                  LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17 CACHE STRING "The C++ standard to use")

find_package(fmt CONFIG REQUIRED)
find_package(antlr4-runtime CONFIG REQUIRED)
find_package(antlr4-generator CONFIG REQUIRED)
find_package(Z3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
# path to ANTLR generator
set(ANTLR4_JAR_LOCATION ${PROJECT_SOURCE_DIR}/thirdparty/antlr-4.13.2-complete.jar)

message(STATUS "ANTLR4 includes: ${ANTLR4_INCLUDE_DIR} ")

antlr4_generate(
    C11
    ${PROJECT_SOURCE_DIR}/gram/C.g4
    BOTH
    FALSE
    TRUE
    "c11parser"
)

add_executable(main src/main.cpp src/utils.cpp src/parser.cpp)
target_include_directories(main PRIVATE src)

message(STATUS "ANTLR generated headers: ${ANTLR4_INCLUDE_DIR_C11}, ${ANTLR4_SRC_FILES_C11}")
message(STATUS "Z3 headers: " ${Z3_C_INCLUDE_DIRS})
message(STATUS "Z3 lib: " ${Z3_LIBRARIES})

target_include_directories(main PRIVATE "${ANTLR4_INCLUDE_DIR_C11}")
target_sources(main PRIVATE ${ANTLR4_SRC_FILES_C11})
target_include_directories(main PRIVATE "${ANTLR4_INCLUDE_DIR}")
target_link_libraries(main PRIVATE antlr4_shared)

target_link_libraries(main PRIVATE fmt::fmt)

target_include_directories(main PRIVATE ${Z3_C_INCLUDE_DIRS})
target_link_libraries(main PRIVATE ${Z3_LIBRARIES})

target_link_libraries(main PRIVATE nlohmann_json::nlohmann_json)