# require a minimum version of cmake
cmake_minimum_required(VERSION 3.10)

# project name
project(mxm_task_stealing_prototype)

# find MPI lib
find_package(MPI REQUIRED COMPONENTS CXX)
message(STATUS "found mpi.h at ${MPI_CXX_INCLUDE_DIRS}")

# find OpenMP lib
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    set(DEFAULT_OPENMP_FLAG OpenMP::OpenMP_CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -gdwarf-2 -g3")

option(HCL_ENABLE_RPCLIB "allow hcl to use RPCLIB" OFF)
option(HCL_ENABLE_THALLIUM_TCP "allow hcl to use THALLIUM_TCP" OFF)
option(HCL_ENABLE_THALLIUM_ROCE "allow hcl to use THALLIUM_ROCE" OFF)
option(ENABLE_ITAC_TRACE "allow the program to use Intel ITAC" OFF)
option(TEST_ON_LAPTOP "testing on the local laptop" OFF)

if(HCL_ENABLE_RPCLIB)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHCL_ENABLE_RPCLIB")
    message("HCL_ENABLE_RPCLIB: ${HCL_ENABLE_RPCLIB}")
elseif(HCL_ENABLE_THALLIUM_TCP)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHCL_ENABLE_THALLIUM_TCP")
    message("HCL_ENABLE_THALLIUM_TCP: ${HCL_ENABLE_THALLIUM_TCP}")
elseif(HCL_ENABLE_THALLIUM_ROCE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHCL_ENABLE_THALLIUM_ROCE")
    message("HCL_ENABLE_THALLIUM_ROCE: ${HCL_ENABLE_THALLIUM_ROCE}")
endif()

if(HCL_ENABLE_RPCLIB)
    set(RPC_LIB_FLAGS -lrpc ${RPC_LIB_FLAGS})
endif()

if(HCL_ENABLE_THALLIUM_ROCE OR HCL_ENABLE_THALLIUM_TCP)
    set(RPC_LIB_FLAGS -lthallium -lmercury -lmercury_util -lmargo -labt ${RPC_LIB_FLAGS})
endif()

# the main flag for compiling the program
set(LIB_FLAGS ${RPC_LIB_FLAGS} ${DEFAULT_OPENMP_FLAG} -lmpicxx -lpthread -lrt -lhcl)

if(ENABLE_ITAC_TRACE)
    set(ITAC_FLAGS -DTRACE -I${VT_ROOT}/include -L${VT_LIB_DIR} -lVT ${VT_ADD_LIBS})
    set(LIB_FLAGS ${LIB_FLAGS} ${ITAC_FLAGS})
endif()


# Compile the example with linking hcl
add_executable(main main.cpp mxm_task_types.h mxm_kernel.h util.h)
target_compile_options(main PRIVATE -Wall -Wextra)
target_link_libraries(main "${LIB_FLAGS}") # MPI::MPI_CXX
