#include "stencil.h"

/*
 * NB: iter = 0: initialization phase, TAG_U(z, 0) = TAG_INIT
 * dir is -1 or +1.
 */

#define BIND_LAST 1

#if 0
    #define DEBUG(fmt, ...) fprintf(stderr,fmt,##__VA_ARGS__)
#else
    #define DEBUG(fmt, ...)
#endif

/* Define starpu codelet */
void null_func(void *descr[], void *arg)
{
    (void) descr;
    (void) arg;
}

static double null_cost_function(struct starpu_task *task, unsigned nimpl)
{
	(void) task;
	(void) nimpl;
	return 0.000001;
}

static struct starpu_perfmodel null_model =
{
	.type = STARPU_COMMON,
	.cost_function = null_cost_function,
	.symbol = "null"
};

static struct starpu_codelet null = 
{
    .modes = {STARPU_W, STARPU_W},
    .cpu_funcs = {null_func},
    .cpu_funcs_name = {"null_func"},
    .cuda_funcs = {null_func},
    .opencl_funcs = {null_func},
    .nbuffers = 2,
    .model = &null_model,
    .name = "start"
};


/* SAVE: create task_save local??? */
/* R(z) = R(z+d) = local, just call the save kernel */
static void create_task_save_local(unsigned iter, unsigned z, int dir)
{
    struct starpu_task *save_task =  starpu_task_create();
    struct block_description *descr = get_block_description(z);

    save_task->cl = (dir == -1) ? &save_cl_bottom : &save_cl_top;
    save_task->cl_arg = descr;

    // saving the borders
	save_task->handles[0] = descr->layers_handle[0];
	save_task->handles[1] = descr->layers_handle[1];

    // ... to the neighbor's copy
    struct block_description *neighbour = descr->boundary_blocks[(1+dir)/2];
    save_task->handles[2] = neighbour->boundaries_handle[(1-dir)/2][0];
    save_task->handles[3] = neighbour->boundaries_handle[(1-dir)/2][1];

    // printf("[task_save_local-iter%d] block %d (dir=%d): block_neighbour[%d]=%d, neighbour_boundaries_handle[%d][0/1]\n", iter, z, dir, (1+dir)/2, neighbour->bz, (1-dir)/2);

    // binding ...
    if (iter <= BIND_LAST)
        save_task->execute_on_a_specific_worker = get_bind_tasks();
    save_task->workerid = descr->preferred_worker;

    int ret = starpu_task_submit(save_task);
    if (ret){
        FPRINTF(stderr, "Couldn't submit task_save: %d\n", ret);
        if (ret == -ENODEV)
            exit(77);
        STARPU_ABORT();
    }
}


/* R(z) = local & R(z+d) != local */
/* We need to send our save over MPI */

static void send_done(void *arg)
{
	uintptr_t z = (uintptr_t) arg;
	(void) z;
	DEBUG("DO SEND %d\n", (int)z);
}

/* Post MPI send */
// #if STARPU_USE_MPI
static void create_task_save_mpi_send(unsigned iter, unsigned z, int dir, int local_rank)
{
	struct block_description *descr = get_block_description(z);
	STARPU_ASSERT(descr->mpi_node == local_rank);

	struct block_description *neighbour = descr->boundary_blocks[(1+dir)/2];
	int dest = neighbour->mpi_node;
	STARPU_ASSERT(neighbour->mpi_node != local_rank);

	/* Send neighbour's border copy to the neighbour */
	starpu_data_handle_t handle0 = neighbour->boundaries_handle[(1-dir)/2][0];
	starpu_data_handle_t handle1 = neighbour->boundaries_handle[(1-dir)/2][1];

	starpu_mpi_isend_detached(handle0, dest, MPI_TAG0(z, iter, dir), MPI_COMM_WORLD, send_done, (void*)(uintptr_t)z);
	starpu_mpi_isend_detached(handle1, dest, MPI_TAG1(z, iter, dir), MPI_COMM_WORLD, send_done, (void*)(uintptr_t)z);
}
// #endif /* STARPU_USE_MPI */


/* R(z) != local & R(z+d) = local */
/* We need to receive over MPI */
static void recv_done(void *arg)
{
	uintptr_t z = (uintptr_t) arg;
	(void) z;
	DEBUG("DO RECV %d\n", (int)z);
}

/* Post MPI recv */
// #if STARPU_USE_MPI
static void create_task_save_mpi_recv(unsigned iter, unsigned z, int dir, int local_rank)
{
	struct block_description *descr = get_block_description(z);
	STARPU_ASSERT(descr->mpi_node != local_rank);

	struct block_description *neighbour = descr->boundary_blocks[(1+dir)/2];
	int source = descr->mpi_node;
	STARPU_ASSERT(neighbour->mpi_node == local_rank);

	/* Receive our neighbour's border in our neighbour copy */
	starpu_data_handle_t handle0 = neighbour->boundaries_handle[(1-dir)/2][0];
	starpu_data_handle_t handle1 = neighbour->boundaries_handle[(1-dir)/2][1];

	starpu_mpi_irecv_detached(handle0, source, MPI_TAG0(z, iter, dir), MPI_COMM_WORLD, recv_done, (void*)(uintptr_t)z);
	starpu_mpi_irecv_detached(handle1, source, MPI_TAG1(z, iter, dir), MPI_COMM_WORLD, recv_done, (void*)(uintptr_t)z);
}
// #endif /* STARPU_USE_MPI */


/* Create start tasks??? */
void create_start_task(int z, int dir)
{
    // ------------------------ begin VT -----------------------------
    #ifdef TRACE
    int event_c_starttask = -1;
    char eventtag_c_starttask[12] = "c_start_task";
    int itac_err = VT_funcdef(eventtag_c_starttask, VT_NOCLASS, &event_c_starttask);
    VT_BEGIN_CONSTRAINED(event_c_starttask);
    #endif
    // ---------------------------------------------------------------

    /* Dumb task depending on the init task and simulating writing the
	   neighbour buffers, to avoid communications and computation running
	   before we start measuring time */
    struct starpu_task *wait_init = starpu_task_create();
    struct block_description *descr = get_block_description(z);
    starpu_tag_t tag_init = TAG_INIT_TASK;
    wait_init->cl = &null;
    wait_init->use_tag = 1;
    wait_init->tag_id = TAG_START(z, dir);
    wait_init->handles[0] = descr->boundaries_handle[(1 + dir) / 2][0];
    wait_init->handles[1] = descr->boundaries_handle[(1 + dir) / 2][1];
    starpu_tag_declare_deps_array(wait_init->tag_id, 1, &tag_init);
    // printf("[create_start_task] task->block %d: tag_id %ld depends on task %p\n", z, wait_init->tag_id, &tag_init);

    int ret = starpu_task_submit(wait_init);
    if (ret)
    {
        FPRINTF(stderr, "Could not submit task initial wait: %d\n", ret);
        if (ret == -ENODEV){
            exit(77);
        }
        STARPU_ABORT();
    }

    // ------------------------ end VT -------------------------------
    #ifdef TRACE
    VT_END_W_CONSTRAINED(event_c_starttask);
    #endif
    // ---------------------------------------------------------------
    
}


/*
 * Schedule update computation in computation buffer
 */
void create_task_update(unsigned iter, unsigned z, int local_rank)
{
    // ------------------------ begin VT -----------------------------
    #ifdef TRACE
    int event_c_taskupdate = -1;
    char eventtag_c_taskupdate[12] = "c_taskupdate";
    int itac_err = VT_funcdef(eventtag_c_taskupdate, VT_NOCLASS, &event_c_taskupdate);
    VT_BEGIN_CONSTRAINED(event_c_taskupdate);
    #endif
    // ---------------------------------------------------------------

	(void)local_rank; // unneeded parameter, we keep it to have a similar function prototype to the implicit case
	STARPU_ASSERT(iter != 0);

	struct starpu_task *task = starpu_task_create();

	unsigned niter = get_niter();

	/* We are going to synchronize with the last tasks */
	if (iter == niter)
	{
		task->use_tag = 1;
		task->tag_id = TAG_FINISH(z);
	}

	unsigned old_layer = (K*(iter-1)) % 2;
	unsigned new_layer = (old_layer + 1) % 2;

	struct block_description *descr = get_block_description(z);
	task->handles[0] = descr->layers_handle[new_layer];
	task->handles[1] = descr->layers_handle[old_layer];

	task->handles[2] = descr->boundaries_handle[T][new_layer];
	task->handles[3] = descr->boundaries_handle[T][old_layer];

	task->handles[4] = descr->boundaries_handle[B][new_layer];
	task->handles[5] = descr->boundaries_handle[B][old_layer];

	task->cl = &cl_update;
	task->cl_arg = descr;

	if (iter <= BIND_LAST)
		task->execute_on_a_specific_worker = get_bind_tasks();
	task->workerid = descr->preferred_worker;

	int ret = starpu_task_submit(task);
	if (ret)
	{
		FPRINTF(stderr, "Could not submit task update block: %d\n", ret);
		if (ret == -ENODEV)
			exit(77);
		STARPU_ABORT();
	}

    // ------------------------ end VT -------------------------------
    #ifdef TRACE
    VT_END_W_CONSTRAINED(event_c_taskupdate);
    #endif
    // ---------------------------------------------------------------
}

/*
 * Schedule saving boundaries of blocks to communication buffers
 */
void create_task_save(unsigned iter, unsigned z, int dir, int local_rank)
{
    // ------------------------ begin VT -----------------------------
    #ifdef TRACE
    int event_c_tasksave = -1;
    char eventtag_c_tasksave[12] = "c_tasksave";
    int itac_err = VT_funcdef(eventtag_c_tasksave, VT_NOCLASS, &event_c_tasksave);
    VT_BEGIN_CONSTRAINED(event_c_tasksave);
    #endif
    // ---------------------------------------------------------------

	int node_z = get_block_mpi_node(z);             // get mpi rank of block z
	int node_z_and_d = get_block_mpi_node(z+dir);   // 

#if STARPU_USE_MPI
	if (node_z == local_rank){
		/* Save data from update */
		create_task_save_local(iter, z, dir);
		if (node_z_and_d != local_rank){
			/* R(z) = local & R(z+d) != local, We have to send the data */
			create_task_save_mpi_send(iter, z, dir, local_rank);
            // printf("[bz %d (r%d), bz_d %d (r%d)] sent saved_data to r%d\n", z, node_z, (z+dir), node_z_and_d, local_rank);
		}
	}
	else
	{
		/* node_z != local_rank, this MPI node doesn't have the saved data */
		if (node_z_and_d == local_rank)
		{
			create_task_save_mpi_recv(iter, z, dir, local_rank);
            // printf("[bz %d (r%d), bz_d %d (r%d)] received saved_data from ...\n", z, node_z, (z+dir), node_z_and_d);
		}
		else
		{
			/* R(z) != local & R(z+d) != local We don't have
			   the saved data and don't need it, we shouldn't
			   even have been called! */
			STARPU_ABORT();
		}
	}
#else /* !STARPU_USE_MPI */
	STARPU_ASSERT((node_z == local_rank) && (node_z_and_d == local_rank));
	create_task_save_local(iter, z, dir);
#endif /* STARPU_USE_MPI */

    // ------------------------ end VT -------------------------------
    #ifdef TRACE
    VT_END_W_CONSTRAINED(event_c_tasksave);
    #endif
    // ---------------------------------------------------------------
}


/*
 * Create all the tasks
 */
void create_tasks(int rank)
{
    // ------------------------ begin VT -----------------------------
    #ifdef TRACE
    int event_c_task = -1;
    char eventtag_c_task[12] = "c_task";
    int itac_err = VT_funcdef(eventtag_c_task, VT_NOCLASS, &event_c_task);
    VT_BEGIN_CONSTRAINED(event_c_task);
    #endif
    // ---------------------------------------------------------------

    int iter;
    int bz;
    int niter = get_niter();
    int nbz = get_nbz();
    for (bz = 0; bz < nbz; bz++)
    {
        if ((get_block_mpi_node(bz) == rank) || (get_block_mpi_node(bz+1) == rank)){
            create_start_task(bz, +1);
        }
        if ((get_block_mpi_node(bz) == rank) || (get_block_mpi_node(bz-1) == rank)){
            create_start_task(bz, -1);
        }   
    }

    for (iter = 0; iter <= niter; iter++)
    {
        starpu_iteration_push(iter);
        for (bz = 0; bz < nbz; bz++){
		    if ((iter > 0) && (get_block_mpi_node(bz) == rank)){
                create_task_update(iter, bz, rank);
            }
	    }

        for (bz = 0; bz < nbz; bz++){
            if (iter != niter){
                int bz_mpi_node = get_block_mpi_node(bz);
                int bz_pos1_mpi_node = get_block_mpi_node(bz+1);
                int bz_neg1_mpi_node = get_block_mpi_node(bz-1);
                if ((bz_mpi_node == rank) || (bz_pos1_mpi_node == rank)){
                    create_task_save(iter, bz, +1, rank);
                }

                if ((bz_mpi_node == rank) || (bz_neg1_mpi_node == rank)){
                    create_task_save(iter, bz, -1, rank);
                }
            }
        }

        starpu_iteration_pop();
    }

    // ------------------------ end VT -------------------------------
    #ifdef TRACE
    VT_END_W_CONSTRAINED(event_c_task);
    #endif
    // ---------------------------------------------------------------
}


/* Wait for termination */
void wait_end_tasks(int rank)
{
    int bz;
    int nbz = get_nbz();

    for (bz = 0; bz < nbz; bz++)
    {
        if (get_block_mpi_node(bz) == rank)
        {
            /* Wait for the task producing block "bz" */
			starpu_tag_wait(TAG_FINISH(bz));

			/* Get the result back to memory */
			struct block_description *block = get_block_description(bz);
			starpu_data_acquire(block->layers_handle[0], STARPU_R);
			starpu_data_acquire(block->layers_handle[1], STARPU_R);
			/* the data_acquire here is done to make sure
			 * the data is sent back to the ram memory, we
			 * can safely do a data_release, to avoid the
			 * data_unregister to block later on
			 */
			starpu_data_release(block->layers_handle[0]);
			starpu_data_release(block->layers_handle[1]);
        }
    }
}