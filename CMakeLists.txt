 cmake_minimum_required(VERSION 3.16)

# delete .out/CmakeFiles folder to make this work
option(PRODUCTION_BUILD "Make this a production build" OFF)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Release>:Release>")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# project name
set(PROJECT_NAME "editor")

# Set the Compiler
# set(CMAKE_CXX_COMPILER /usr/bin/g++)

# set to C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CXX_STANDARD 17)
set(CXX_STANDARD_REQUIRED ON)

# Set the CMP0069 policy, because warning annoying
if(POLICY CMP0069)
    cmake_policy(SET CMP0069 NEW)
endif()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION PRODUCTION_BUILD)	

# address sanitizer, incompatible with gdb
# add_compile_options(/ZI-)
# add_link_options(-fsanitize=address)
# add_compile_options(-fsanitize=address)

if(MSVC)
    add_compile_options(/arch:AVX2 /W4 /EHsc)
endif()

# Enable Hot Reload for MSVC compilers if supported, came with the default file
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("${PROJECT_NAME}")

# lsp stuff
# set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# add_definitions(-DSOME_DEFINITION)

# Add source files recursively from the src directory
file(GLOB_RECURSE MY_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# Add source to this project's executable
add_executable(${PROJECT_NAME} ${MY_SOURCES})

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)

if(PRODUCTION_BUILD)
    # setup the ASSETS_PATH macro to be in the root folder of your exe
    target_compile_definitions(${PROJECT_NAME} PUBLIC RESOURCES_PATH="./") 
    # remove the option to debug asserts.
    target_compile_definitions(${PROJECT_NAME} PUBLIC PRODUCTION_BUILD=1) 
else()
    # This is useful to get an ASSETS_PATH in your IDE during development
    target_compile_definitions(${PROJECT_NAME} PUBLIC RESOURCES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/resources/")
    target_compile_definitions(${PROJECT_NAME} PUBLIC PRODUCTION_BUILD=0) 
endif()

if(MSVC) # If using the VS compiler...
	target_compile_definitions("${CMAKE_PROJECT_NAME}" PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()
# add .h and .hpp files
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include/")

# Specify the output directory for the build
# set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
# Specify the output directory for the build (/build/bin directory)
# set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")