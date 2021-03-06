project(chameleon_src)

## Build options
option(CHAMELEON_ENABLE_DEV_OUTPUT "Enable detailed outputs" off)
option(CHAMELEON_STATS_RECORD "Record Chameleon internal statistics" on)
option(CHAMELEON_STATS_PRINT "Print statistics at the end" on)
option(CHAMELEON_TRACE_ITAC "Enable tracing with Intel Trace Analyzer " off)
option(CHAMELEON_TOOL_SUPPORT "Enable tool support for Chameleon" on)
option(CHAMELEON_PRINT_CONFIG_VALUES "Print chameleon configuration values per process" on)
option(CHAMELEON_MIGRATE_ANNOTATIONS "Whether to migrate task annotations when a task is migrated" off)
#TODO: add more flags

# Chameleon source and header files
file(GLOB_RECURSE CHAMELEON_LIBRARY_SOURCES "*.h" "*.cpp")
file(GLOB_RECURSE CHAMELEON_LIBRARY_HEADERS "chameleon.h" "chameleon_tools.h")
file(GLOB_RECURSE CHAMELEON_LIBRARY_SOURCES_F90 "*.f90")

# Add current source directory
set(CHAMELEON_LIBRARY_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CHAMELEON_LIBRARY_INCLUDE_DIRS})

if (CHAMELEON_TRACE_ITAC)
  include_directories($ENV{VT_ROOT}/include)
  link_directories($ENV{VT_ROOT}/lib)
endif()

# generate version header file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/chameleon_version.h.in
    ${CMAKE_CURRENT_SOURCE_DIR}/chameleon_version.h
)

set (ADDITIONAL_COMPILE_FLAGS ${ADDITIONAL_COMPILE_FLAGS} "")
if (CHAMELEON_ENABLE_DEV_OUTPUT)
    set (ADDITIONAL_COMPILE_FLAGS "${ADDITIONAL_COMPILE_FLAGS} -DCHAM_DEBUG")
endif()
if (CHAMELEON_STATS_RECORD)
    set (ADDITIONAL_COMPILE_FLAGS "${ADDITIONAL_COMPILE_FLAGS} -DCHAM_STATS_RECORD")
endif()
if (CHAMELEON_STATS_PRINT)
    set (ADDITIONAL_COMPILE_FLAGS "${ADDITIONAL_COMPILE_FLAGS} -DCHAM_STATS_PRINT")
endif()
if (CHAMELEON_TRACE_ITAC)
    set (ADDITIONAL_COMPILE_FLAGS "${ADDITIONAL_COMPILE_FLAGS} -DTRACE -trace ")
endif()
if (CHAMELEON_TOOL_SUPPORT)
    set (ADDITIONAL_COMPILE_FLAGS "${ADDITIONAL_COMPILE_FLAGS} -DCHAMELEON_TOOL_SUPPORT=1")
else()
    set (ADDITIONAL_COMPILE_FLAGS "${ADDITIONAL_COMPILE_FLAGS} -DCHAMELEON_TOOL_SUPPORT=0")
endif()
if (CHAMELEON_PRINT_CONFIG_VALUES)
    set (ADDITIONAL_COMPILE_FLAGS "${ADDITIONAL_COMPILE_FLAGS} -DPRINT_CONFIG_VALUES=1")
else()
    set (ADDITIONAL_COMPILE_FLAGS "${ADDITIONAL_COMPILE_FLAGS} -DPRINT_CONFIG_VALUES=0")
endif()
if (CHAMELEON_MIGRATE_ANNOTATIONS)
    set (ADDITIONAL_COMPILE_FLAGS "${ADDITIONAL_COMPILE_FLAGS} -DCHAM_MIGRATE_ANNOTATIONS=1")
else()
    set (ADDITIONAL_COMPILE_FLAGS "${ADDITIONAL_COMPILE_FLAGS} -DCHAM_MIGRATE_ANNOTATIONS=0")
endif()
#TODO: add more flags

set (ADDITIONAL_LIBRARIES 
    ${ADDITIONAL_LIBRARIES} 
    pthread 
    hwloc
    ffi
)

## Build targets
set (CHAMELEON_LIBRARY_NAME "chameleon")

# F90 module file
add_library(
    "chameleon_lib"
    MODULE
    chameleon_lib.f90
)

# define chameleon shared library
add_library(
    ${CHAMELEON_LIBRARY_NAME}
    SHARED
    ${CHAMELEON_LIBRARY_SOURCES}
    ${CHAMELEON_LIBRARY_SOURCES_F90}
    ${CHAMELEON_LIBRARY_HEADERS}
)

set_target_properties(
    ${CHAMELEON_LIBRARY_NAME}
    PROPERTIES 
    POSITION_INDEPENDENT_CODE TRUE
)

# Linker settings
if (CHAMELEON_TRACE_ITAC)
    set (ADDITIONAL_LIBRARIES 
      ${ADDITIONAL_LIBRARIES} 
      VT
      vtunwind
      dwarf
      elf
    )
    set_property(TARGET ${CHAMELEON_LIBRARY_NAME} APPEND_STRING PROPERTY LINK_FLAGS " -DTRACE -trace ")
endif()

target_link_libraries(
    ${CHAMELEON_LIBRARY_NAME}
    ${ADDITIONAL_LIBRARIES}
    ${MPI_LIBRARIES}
    ${MPI_C_LIBRARIES}
    ${MPI_CXX_LIBRARIES}
)

# Compile flags used for build
set_target_properties(
    ${CHAMELEON_LIBRARY_NAME}
    PROPERTIES 
    COMPILE_FLAGS "${ADDITIONAL_COMPILE_FLAGS}"
)

if(MPI_COMPILE_FLAGS)
    set_property(TARGET ${CHAMELEON_LIBRARY_NAME} 
      APPEND_STRING PROPERTY COMPILE_FLAGS " ${MPI_COMPILE_FLAGS} ")
endif()

if(MPI_LINK_FLAGS)
    set_property(TARGET ${CHAMELEON_LIBRARY_NAME} 
      APPEND_STRING PROPERTY LINK_FLAGS " ${MPI_LINK_FLAGS} ")
endif()

include_directories(${ADDITIONAL_INCLUDES})

## Installation
install(TARGETS ${CHAMELEON_LIBRARY_NAME} DESTINATION lib)                      # shared library
install(FILES "chameleon.h" "chameleon_tools.h" DESTINATION include)            # headers
install(FILES ${CMAKE_BINARY_DIR}/src/chameleon_lib.mod DESTINATION include)    # fortran mod file
