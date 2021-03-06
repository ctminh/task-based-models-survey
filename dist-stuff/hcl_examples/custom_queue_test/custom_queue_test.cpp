/*
 *
 * This example is referer from the queue_test example in part of hcl-src.
 *
 * HCL is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 * https://github.com/scs-lab/hcl
 *
 */
#include <sys/types.h>
#include <iostream>
#include <string>
#include <cmath>
#include <sys/syscall.h>

// for mpi, omp
#include <mpi.h>
#include <omp.h>

#ifndef TRACE
#define TRACE 0
#endif

#if TRACE==1

#include "VT.h"
static int _tracing_enabled = 1;

#ifndef VT_BEGIN_CONSTRAINED
#define VT_BEGIN_CONSTRAINED(event_id) if (_tracing_enabled) VT_begin(event_id);
#endif

#ifndef VT_END_W_CONSTRAINED
#define VT_END_W_CONSTRAINED(event_id) if (_tracing_enabled) VT_end(event_id);
#endif

#endif

// for hcl data-structures
#include <hcl/common/data_structures.h>
#include <hcl/queue/queue.h>

// for user-defined types
#include "types.h"

// for display hline
#define HLINE "-------------------------------------------------------------"

// ================================================================================
// Global Variables
// ================================================================================


// ================================================================================
// Main function
// ================================================================================
int main (int argc, char *argv[])
{
    /* /////////////////////////////////////////////////////////////////////////////
     * Initializing MPI
     * ////////////////////////////////////////////////////////////////////////// */

    // init mpi with mpi_init_thread
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        printf("Didn't receive appropriate MPI threading specification\n");
        exit(EXIT_FAILURE);
    }

    // variables for tracking mpi-processes
    int comm_size, my_rank;
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    bool debug = false;
    int num_requests = 100; // also num_tasks
    long size_of_request = 1000;
    if (comm_size != 4){
        printf("This test is designed for running with 4 mpi ranks...\n");
        exit(EXIT_FAILURE);
    }

    // get hostname of each rank
    int name_len;
    char processor_name[MPI_MAX_PROCESSOR_NAME];
    MPI_Get_processor_name(processor_name, &name_len);
    if (debug) {
        printf("[DBG] R%d: on %s | pid=%d\n", my_rank, processor_name, getpid());
    }

    // check num of ready ranks
    if(debug && my_rank == 0){
        printf("[DBG] %d ranks ready for HCL_CONF attaching\n", comm_size);
        fflush(stdout);
    }
    MPI_Barrier(MPI_COMM_WORLD);


    /* /////////////////////////////////////////////////////////////////////////////
     * Configuring HCL-setup
     * ////////////////////////////////////////////////////////////////////////// */

    // assume that total ranks is even, 2 nodes for testing
    int ranks_per_node = 2;
    int num_nodes = comm_size / ranks_per_node;
    int ranks_per_server = ranks_per_node;

    // choose the server, for simple, assign R0 as the server,
    // for example, the last rank is the server
    bool is_server = (my_rank + 1) % ranks_per_server == 0;

    // for simple each node has a server, so server_on_node is true
    bool server_on_node = false;
    if (is_server)
        server_on_node = true;

    // set my_server for R0, R1, others in the if
    int my_server = my_rank / ranks_per_server;

    // ser num of servers for each rank
    int num_servers = comm_size / ranks_per_server;

    /* /////////////////////////////////////////////////////////////////////////////
     * Writing server addresses
     * ////////////////////////////////////////////////////////////////////////// */

    // write the server address into file
    // if (is_server && my_rank == 0){
    //     std::ofstream server_list_file;
    //     server_list_file.open("./server_list");

        // temporarily put the hard-code ip of rome1, rome2 here
        // for tcp, we use processor_name and for simple, we use just R0 for writing
    //     server_list_file << "10.12.1.1";
    //     server_list_file << std::endl;
    //     server_list_file << "10.12.1.2";
    //     server_list_file.close();
    // }
    
    // just to make sure the shared-file system done in sync
    MPI_Barrier(MPI_COMM_WORLD);

    // configure hcl components before running, this configuration for each rank
    HCL_CONF->IS_SERVER = is_server;
    HCL_CONF->MY_SERVER = my_server;
    HCL_CONF->NUM_SERVERS = num_servers;
    HCL_CONF->SERVER_ON_NODE = server_on_node;
    HCL_CONF->SERVER_LIST_PATH = "./server_list";

    auto mem_size = SIZE * SIZE * (comm_size + 1) * num_requests;
    HCL_CONF->MEMORY_ALLOCATED = mem_size;
    std::cout << HLINE << std::endl;

    std::cout << "[CHECK] R" << my_rank
              << ": is_server=" << HCL_CONF->IS_SERVER
              << ", my_server=" << HCL_CONF->MY_SERVER
              << ", num_servers=" << HCL_CONF->NUM_SERVERS
              << ", server_on_node=" << HCL_CONF->SERVER_ON_NODE
              << ", mem_allocated=" << HCL_CONF->MEMORY_ALLOCATED
              << std::endl;

    /* /////////////////////////////////////////////////////////////////////////////
     * Creating HCL global queues over mpi ranks
     * ////////////////////////////////////////////////////////////////////////// */

    hcl::queue<MatTup_Type> *global_queue;

    // allocate the hcl queue at server-side
    if (is_server) {
        global_queue = new hcl::queue<MatTup_Type>();
    }
    MPI_Barrier(MPI_COMM_WORLD);

    // sounds like the server queue need to be created before the client ones
    if (!is_server) {
        global_queue = new hcl::queue<MatTup_Type>();
    }

    // declare a std-queue/rank at the local side for comparison
    std::queue<MatTup_Type> local_queue;

    /* /////////////////////////////////////////////////////////////////////////////
     * Split the mpi communicator from the server, just for client communicator
     * ////////////////////////////////////////////////////////////////////////// */

    MPI_Comm client_comm;
    MPI_Comm_split(MPI_COMM_WORLD, !is_server, my_rank, &client_comm);
    int client_comm_size;
    MPI_Comm_size(client_comm, &client_comm_size);
    MPI_Barrier(MPI_COMM_WORLD);

    // check task size
    MatTup_Type tmp_T = MatTup_Type(0, 0);
    size_t task_size = sizeof(tmp_T);
    if (is_server){
        std::cout << "[CHECK] R" << my_rank << ": task size = " << task_size << " bytes" << std::endl;
        std::cout << "        The first 10-elements of tmp_T: ";
        for (int i = 0; i < 10; i++){
            std::cout << tmp_T.A[i] << " ";
        } std::cout << std::endl;
        std::cout << HLINE << std::endl;
    }

    /* /////////////////////////////////////////////////////////////////////////////
     * Test throughput of the LOCAL QUEUES at client-side
     * ////////////////////////////////////////////////////////////////////////// */

    // Declare a keyHash object
    std::hash<MatTup_Type> keyHash;
    
    if (!is_server) {

        // for pushing local
        Timer t_push_local = Timer();
        for(int i = 0; i < num_requests; i++){
            size_t val = my_server;
            size_t key_hash = keyHash(MatTup_Type(i, val)) % num_servers;
            
            // allocate an arr_mat task
            // MatTup_Type lT= MatTup_Type();
            // local_queue.push(lT);

            // do nothing, is this important???
            if (key_hash == my_server && is_server) {}
                       
            // put T into the queue and record eslapsed-time
            t_push_local.resumeTime();
            local_queue.push(MatTup_Type(i, val));
            t_push_local.pauseTime();
        }
        // double throughput_push_local = (num_requests*task_size*1000) / (t_push_local.getElapsedTime()*1024*1024);
        double throughput_push_local = num_requests / t_push_local.getElapsedTime() * 1000 * SIZE * SIZE * 3 * sizeof(double) / 1024 / 1024;
        std::cout << "[THROUGHPUT] R" << my_rank << ": local_push = " << throughput_push_local << " MB/s" << std::endl;

        // Barrier here for the client_commm
        // MPI_Barrier(client_comm);

        // for deleting the local queue
        Timer t_pop_local = Timer();
        for (int i = 0;  i < num_requests; i++){

            size_t val = my_server;
            size_t key_hash = keyHash(MatTup_Type(i, val)) % num_servers;

            // do nothing, is this important???
            if (key_hash == my_server && is_server) {}

            t_pop_local.resumeTime();
            auto result = local_queue.front();
            local_queue.pop();
            t_pop_local.pauseTime();
        }
        // double throughput_pop_local = (num_requests*task_size*1000) / (t_pop_local.getElapsedTime()*1024*1024);
        double throughput_pop_local = num_requests / t_pop_local.getElapsedTime() * 1000 * SIZE * SIZE * 3 * sizeof(double) / 1024 / 1024;
        std::cout << "[THROUGHPUT] R" << my_rank << ": local_pop = " << throughput_pop_local << " MB/s" << std::endl;

        // Barrier here for the client_commm
        MPI_Barrier(client_comm);
        std::cout << HLINE << std::endl;
    }

    /* /////////////////////////////////////////////////////////////////////////////
     * Test throughput of the HCL QUEUES at client-side
     * /////////////////////////////////////////////////////////////////////////////  
     */
    if (!is_server) {

        /* Local Operations for the global-HCL queue */
        
        // set the server key for clients
        uint16_t my_server_key = my_server % num_servers;

        Timer t_localpush_on_globalqueue = Timer();
        for (int i = 0; i < num_requests; i++){
            size_t val = my_server;
            auto key = MatTup_Type(i, val);

            t_localpush_on_globalqueue.resumeTime();
            global_queue->Push(key, my_server_key);
            t_localpush_on_globalqueue.pauseTime();
        }
        // double throughput_localpush_on_globalqueue = (num_requests*task_size*1000) / (t_localpush_on_globalqueue.getElapsedTime()*1024*1024);
        double throughput_localpush_on_globalqueue = num_requests / t_localpush_on_globalqueue.getElapsedTime() * 1000 * SIZE * SIZE * 3 * sizeof(double) / 1024 / 1024;
        std::cout << "[THROUGHPUT] R" << my_rank
                  << " [my_local_server_key=" <<  my_server_key << "]"
                  << ": localpush_on_globalqueue = " << throughput_localpush_on_globalqueue << " MB/s" << std::endl;

        Timer t_localpop_on_globalqueue = Timer();
        for (int i = 0; i < num_requests; i++) {
            size_t val = my_server;
            auto key = MatTup_Type(i, val);
            size_t key_hash = keyHash(MatTup_Type(i, val)) % num_servers;

            // do nothing, is this important???
            if (key_hash == my_server && is_server) { }

            t_localpop_on_globalqueue.resumeTime();
            auto result = global_queue->Pop(my_server_key);
            t_localpop_on_globalqueue.pauseTime();
        }
        // double throughput_localpop_on_globalqueue = (num_requests*task_size*1000) / (t_localpop_on_globalqueue.getElapsedTime()*1024*1024);
        double throughput_localpop_on_globalqueue = num_requests / t_localpop_on_globalqueue.getElapsedTime() * 1000 * SIZE * SIZE * 3 * sizeof(double) / 1024 / 1024;
        std::cout << "[THROUGHPUT] R" << my_rank
                  << " [my_local_server_key=" << my_server_key << "]"
                  << ": localpop_on_globalqueue = " << throughput_localpop_on_globalqueue << " MB/s" << std::endl;

        MPI_Barrier(client_comm);
        
        // set the server key for clients
        uint16_t my_server_remote_key = (my_server + 1) % num_servers;

        // remote put tasks to the hcl-global-queue
        Timer t_push_remote = Timer();
        for(int i = 0; i < num_requests; i++){
            size_t val = my_server + 1;
            auto key = MatTup_Type(i, val);

            // allocate the task
            // Mattup_StdArr_t gT = Mattup_StdArr_t();
            
            // put tasks to the glob-queue and measure time
            t_push_remote.resumeTime();
            global_queue->Push(key, my_server_remote_key);
            t_push_remote.pauseTime();
        }

        // estimate the remote-push throughput
        // double throughput_push_remote = (num_requests*task_size*1000) / (t_push_remote.getElapsedTime()*1024*1024);
        double throughput_push_remote = num_requests / t_push_remote.getElapsedTime() * 1000 * SIZE * SIZE * 3 * sizeof(double) / 1024 / 1024;
        std::cout << "[THROUGHPUT] R" << my_rank 
                  << " [remote_key=" << my_server_remote_key << "]"
                  << ": remote_push = " << throughput_push_remote << " MB/s" << std::endl;
        
        // Barrier here for the client_commm
        MPI_Barrier(client_comm);

        // Create a temp_tasks for checking the elements are popped out
        MatTup_Type tmp_popped_T;
        
        // pop tasks from the hcl-global-queue
        Timer t_pop_remote = Timer();
        for(int i = 0; i < num_requests; i++){
            size_t val = my_server + 1;
            auto key = MatTup_Type(i, val);
            size_t key_hash = keyHash(MatTup_Type(i, val)) % num_servers;

            // do nothing
            if (key_hash == my_server && is_server) { }
            
            // pop tasks and measure time
            t_pop_remote.resumeTime();
            auto remote_pop_res = global_queue->Pop(my_server_remote_key);
            t_pop_remote.pauseTime();

            // if task i is the last one, assign it to the tmp_popped_T for checking internal-values
            if (i == num_requests-1)
                tmp_popped_T = remote_pop_res.second;
        }

        // estimate the remote-push throughput
        // double throughput_pop_remote = (num_requests*task_size*1000) / (t_pop_remote.getElapsedTime()*1024*1024);
        double throughput_pop_remote = num_requests / t_pop_remote.getElapsedTime() * 1000 * SIZE * SIZE * 3 * sizeof(double) / 1024 / 1024;
        std::cout << "[THROUGHPUT] R" << my_rank
                  << " [remote_key=" << my_server_remote_key << "]"
                  << ": remote_pop = " << throughput_pop_remote << " MB/s" << std::endl;
        
        // Barrier here for the client_commm
        MPI_Barrier(client_comm);
        std::cout << HLINE << std::endl;

        // Check one of the matrices which is popped out
        std::cout << "[CHECK] R" << my_rank << ": one of the tasks that are popped out remotely, " << std::endl;
        std::cout << "\t tid: " << tmp_popped_T.tid << std::endl;
        std::cout << "\t the first 10 elements: ";
        for (int i = 0; i < 10; i++){
            std::cout << tmp_popped_T.A[i] << " ";
        } std::cout << std::endl;
    }

    // wait for make sure finalizing MPI safe
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    
    exit(EXIT_SUCCESS);
}
