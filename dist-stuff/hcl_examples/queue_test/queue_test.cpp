#include <sys/types.h>
#include <unistd.h>

#include <functional>
#include <utility>
#include <mpi.h>
#include <iostream>
#include <signal.h>
#include <execinfo.h>
#include <chrono>
#include <queue>
#include <fstream>
#include <hcl/common/data_structures.h>
#include <hcl/queue/queue.h>

struct KeyType{
    double a;
    
    // constructor 1
    KeyType(): a(0) { }
    // constructor 2
    KeyType(double val): a(val) { }

#ifdef HCL_ENABLE_RPCLIB
    MSGPACK_DEFINE(a);
#endif

    /* equal operator for comparing two Matrix. */
    bool operator==(const KeyType &o) const {
        return a == o.a;
    }
    KeyType& operator=( const KeyType& other ) {
        a = other.a;
        return *this;
    }
    bool operator<(const KeyType &o) const {
        return a < o.a;
    }
    bool operator>(const KeyType &o) const {
        return a > o.a;
    }
    bool Contains(const KeyType &o) const {
        return a==o.a;
    }

#if defined(HCL_ENABLE_THALLIUM_TCP) || defined(HCL_ENABLE_THALLIUM_ROCE)
    template<typename A>
    void serialize(A &ar) {
        ar & a.a;
    }
#endif

};

// namespace std {
//     template<>
//     struct hash<KeyType> {
//         size_t operator()(const KeyType &k) const {
//             return k.a;
//         }
//     };
// }

/* Try a simple struct with a single double element */
struct DoubleType {
    double a;

    // constructor 1
    DoubleType(): a(1.0) { }
    // constructor 2
    DoubleType(double val): a(val) { }

    // define operator for the hash
    DoubleType& operator=( const DoubleType& other ) {
        a = other.a;
        return *this;
    }

};

// try to get the serialization out of the struct define
#if defined(HCL_ENABLE_THALLIUM_TCP) || defined(HCL_ENABLE_THALLIUM_ROCE)
template<typename A>
void serialize(A &ar, DoubleType &a) {
    ar & a.a;
}
#endif

namespace std {
    template<>
    struct hash<DoubleType> {
        size_t operator()(const DoubleType &k) const {
            return k.a;
        }
    };
}


int main (int argc,char* argv[])
{
    int provided;
    MPI_Init_thread(&argc,&argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        printf("Didn't receive appropriate MPI threading specification\n");
        exit(EXIT_FAILURE);
    }
    int comm_size,my_rank;
    MPI_Comm_size(MPI_COMM_WORLD,&comm_size);
    MPI_Comm_rank(MPI_COMM_WORLD,&my_rank);
    int ranks_per_server=comm_size,num_request=100;
    long size_of_request=1000;
    bool debug=false;
    bool server_on_node=false;
    if(argc > 1)    ranks_per_server = atoi(argv[1]);
    if(argc > 2)    num_request = atoi(argv[2]);
    if(argc > 3)    size_of_request = (long)atol(argv[3]);
    if(argc > 4)    server_on_node = (bool)atoi(argv[4]);
    if(argc > 5)    debug = (bool)atoi(argv[5]);

    int len;
    char processor_name[MPI_MAX_PROCESSOR_NAME];
    MPI_Get_processor_name(processor_name, &len);
    if (debug) {
        printf("%s/%d: %d\n", processor_name, my_rank, getpid());
    }
    
    if(debug && my_rank==0){
        printf("%d ready for attach\n", comm_size);
        fflush(stdout);
        getchar();
    }
    MPI_Barrier(MPI_COMM_WORLD);
    bool is_server=(my_rank+1) % ranks_per_server == 0;
    int my_server=my_rank / ranks_per_server;
    int num_servers=comm_size/ranks_per_server;

    // write server_list file
    if (is_server){
        std::ofstream server_list_file;
        server_list_file.open("./server_list");

#if defined(HCL_ENABLE_THALLIUM_TCP)
        server_list_file << processor_name;
#elif defined(HCL_ENABLE_THALLIUM_ROCE)
        server_list_file << "10.12.1.2";
#endif
        server_list_file.close();
    }

    // The following is used to switch to 40g network on Ares.
    // This is necessary when we use RoCE on Ares.
    std::string proc_name = std::string(processor_name);
    /* int split_loc = proc_name.find('.');
    std::string node_name = proc_name.substr(0, split_loc);
    std::string extra_info = proc_name.substr(split_loc+1, string::npos);
    proc_name = node_name + "-40g." + extra_info; */

    size_t size_of_elem = sizeof(int);

    printf("rank %d, is_server %d, my_server %d, num_servers %d\n",my_rank,is_server,my_server,num_servers);

    const int array_size=TEST_REQUEST_SIZE;

    if (size_of_request != array_size) {
        printf("Please set TEST_REQUEST_SIZE in include/hcl/common/constants.h instead. Testing with %d\n", array_size);
    }

    std::array<int,array_size> my_vals=std::array<int,array_size>();

    
    HCL_CONF->IS_SERVER = is_server;
    HCL_CONF->MY_SERVER = my_server;
    HCL_CONF->NUM_SERVERS = num_servers;
    HCL_CONF->SERVER_ON_NODE = server_on_node || is_server;
    HCL_CONF->SERVER_LIST_PATH = "./server_list";

    hcl::queue<DoubleType> *queue;
    if (is_server) {
        queue = new hcl::queue<DoubleType>();
    }
    MPI_Barrier(MPI_COMM_WORLD);
    if (!is_server) {
        queue = new hcl::queue<DoubleType>();
    }

    std::queue<DoubleType> lqueue=std::queue<DoubleType>();

    MPI_Comm client_comm;
    MPI_Comm_split(MPI_COMM_WORLD, !is_server, my_rank, &client_comm);
    int client_comm_size;
    MPI_Comm_size(client_comm, &client_comm_size);

    MPI_Barrier(MPI_COMM_WORLD);
    if (!is_server) {
        Timer llocal_queue_timer=Timer();
        // std::hash<KeyType> keyHash;
        std::hash<DoubleType> keyHash;

        /*Local std::queue test*/
        for(int i=0;i<num_request;i++){
            // size_t val=my_server;
            double val = my_server;
            llocal_queue_timer.resumeTime();
            size_t key_hash = keyHash(DoubleType(val))%num_servers;
            if (key_hash == my_server && is_server){}
            lqueue.push(DoubleType(val));
            llocal_queue_timer.pauseTime();
        }

        double llocal_queue_throughput=num_request/llocal_queue_timer.getElapsedTime()*1000*size_of_elem*my_vals.size()/1024/1024;

        Timer llocal_get_queue_timer=Timer();
        for(int i=0;i<num_request;i++){
            // size_t val=my_server;
            double val = my_server;
            llocal_get_queue_timer.resumeTime();
            size_t key_hash = keyHash(DoubleType(val))%num_servers;
            if (key_hash == my_server && is_server){}
            auto result = lqueue.front();
            lqueue.pop();
            llocal_get_queue_timer.pauseTime();
        }
        double llocal_get_queue_throughput=num_request/llocal_get_queue_timer.getElapsedTime()*1000*size_of_elem*my_vals.size()/1024/1024;

        if (my_rank == 0) {
            printf("llocal_queue_throughput put: %f\n",llocal_queue_throughput);
            printf("llocal_queue_throughput get: %f\n",llocal_get_queue_throughput);
        }
        MPI_Barrier(client_comm);

        Timer local_queue_timer=Timer();
        uint16_t my_server_key = my_server % num_servers;
        /*Local queue test*/
        for(int i=0;i<num_request;i++){
            // size_t val=my_server;
            double val = my_server;
            auto key=DoubleType(val);
            local_queue_timer.resumeTime();
            queue->Push(key, my_server_key);
            local_queue_timer.pauseTime();
        }
        double local_queue_throughput=num_request/local_queue_timer.getElapsedTime()*1000*size_of_elem*my_vals.size()/1024/1024;

        Timer local_get_queue_timer=Timer();
        /*Local queue test*/
        for(int i=0;i<num_request;i++){
            // size_t val=my_server;
            double val = my_server;
            auto key=DoubleType(val);
            local_get_queue_timer.resumeTime();
            size_t key_hash = keyHash(DoubleType(val))%num_servers;
            if (key_hash == my_server && is_server){}
            auto result = queue->Pop(my_server_key);
            local_get_queue_timer.pauseTime();
        }

        double local_get_queue_throughput=num_request/local_get_queue_timer.getElapsedTime()*1000*size_of_elem*my_vals.size()/1024/1024;

        double local_put_tp_result, local_get_tp_result;
        if (client_comm_size > 1) {
            MPI_Reduce(&local_queue_throughput, &local_put_tp_result, 1,
                       MPI_DOUBLE, MPI_SUM, 0, client_comm);
            MPI_Reduce(&local_get_queue_throughput, &local_get_tp_result, 1,
                       MPI_DOUBLE, MPI_SUM, 0, client_comm);
            local_put_tp_result /= client_comm_size;
            local_get_tp_result /= client_comm_size;
        }
        else {
            local_put_tp_result = local_queue_throughput;
            local_get_tp_result = local_get_queue_throughput;
        }

        if (my_rank==0) {
            printf("local_queue_throughput put: %f\n", local_put_tp_result);
            printf("local_queue_throughput get: %f\n", local_get_tp_result);
        }

        MPI_Barrier(client_comm);

        Timer remote_queue_timer=Timer();
        /*Remote queue test*/
        uint16_t my_server_remote_key = (my_server + 1) % num_servers;
        for(int i=0;i<num_request;i++){
            // size_t val = my_server+1;
            double val = my_server+1;
            auto key=DoubleType(val);
            remote_queue_timer.resumeTime();
            queue->Push(key, my_server_remote_key);
            remote_queue_timer.pauseTime();
        }
        double remote_queue_throughput=num_request/remote_queue_timer.getElapsedTime()*1000*size_of_elem*my_vals.size()/1024/1024;

        MPI_Barrier(client_comm);

        Timer remote_get_queue_timer=Timer();
        /*Remote queue test*/
        for(int i=0;i<num_request;i++){
            // size_t val = my_server+1;
            double val = my_server+1;
            auto key=DoubleType(val);
            remote_get_queue_timer.resumeTime();
            size_t key_hash = keyHash(DoubleType(val))%num_servers;
            if (key_hash == my_server && is_server){}
            queue->Pop(my_server_remote_key);
            remote_get_queue_timer.pauseTime();
        }
        double remote_get_queue_throughput=num_request/remote_get_queue_timer.getElapsedTime()*1000*size_of_elem*my_vals.size()/1024/1024;

        double remote_put_tp_result, remote_get_tp_result;
        if (client_comm_size > 1) {
            MPI_Reduce(&remote_queue_throughput, &remote_put_tp_result, 1,
                       MPI_DOUBLE, MPI_SUM, 0, client_comm);
            remote_put_tp_result /= client_comm_size;
            MPI_Reduce(&remote_get_queue_throughput, &remote_get_tp_result, 1,
                       MPI_DOUBLE, MPI_SUM, 0, client_comm);
            remote_get_tp_result /= client_comm_size;
        }
        else {
            remote_put_tp_result = remote_queue_throughput;
            remote_get_tp_result = remote_get_queue_throughput;
        }

        if(my_rank == 0) {
            printf("remote queue throughput (put): %f\n",remote_put_tp_result);
            printf("remote queue throughput (get): %f\n",remote_get_tp_result);
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);
    delete(queue);
    MPI_Finalize();
    exit(EXIT_SUCCESS);
}