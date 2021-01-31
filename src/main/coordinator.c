
#include <coordinator.h>

#include <aerospike/as_sleep.h>

#include <transaction.h>


int thr_coordinator_init(struct thr_coordinator* coord, uint32_t n_threads)
{
	// the one extra thread is this thread
	int ret = pthread_barrier_init(&coord->barrier, NULL, n_threads + 1);
	coord->n_threads = n_threads;

	return ret;
}

void thr_coordinator_free(struct thr_coordinator* coord)
{
	pthread_barrier_destroy(&coord->barrier);
}


void thr_coordinator_wait(struct thr_coordinator* coord)
{
	// wait at the barrier twice: once to sync every thread up, and again to
	// wait for the coordinator to execute initialization code before resuming
	// everything
	pthread_barrier_wait(&coord->barrier);
	pthread_barrier_wait(&coord->barrier);
}

/*
 * halt all threads and return once all threads have called thr_coordinator_wait
 */
static void _halt_threads(struct thr_coordinator* coord,
		struct threaddata** tdatas, uint32_t n_threads)
{
	for (uint32_t i = 0; i < n_threads; i++) {
		as_store_uint8((uint8_t*) &tdatas[i]->do_work, false);
	}
	pthread_barrier_wait(&coord->barrier);
}


/*
 * signal to all threads to stop execution and return,
 * must be called after a call to _halt_threads
 */
static void _terminate_threads(struct thr_coordinator* coord,
		struct threaddata** tdatas, uint32_t n_threads)
{
	for (uint32_t i = 0; i < n_threads; i++) {
		as_store_uint8((uint8_t*) &tdatas[i]->finished, true);
	}
	pthread_barrier_wait(&coord->barrier);
}

/*
 * signal to all threads to continue execution,
 * must be called after a call to _halt_threads
 */
static void _release_threads(struct thr_coordinator* coord,
		struct threaddata** tdatas, uint32_t n_threads)
{
	for (uint32_t i = 0; i < n_threads; i++) {
		as_store_uint8((uint8_t*) &tdatas[i]->do_work, true);
	}
	pthread_barrier_wait(&coord->barrier);
}


void* coordinator_worker(void* udata)
{
	struct coordinator_worker_args* args =
		(struct coordinator_worker_args*) udata;
	struct thr_coordinator* coord = args->coord;
	clientdata* cdata = args->cdata;
	struct threaddata** tdatas = args->tdatas;
	uint32_t n_threads = args->n_threads;

	as_sleep(3000);
	_halt_threads(coord, tdatas, n_threads);
	printf("yo we got there!!\n");
	_release_threads(coord, tdatas, n_threads);

	printf("ok back in business, now time to shut down\n");
	_halt_threads(coord, tdatas, n_threads);
	printf("yo we got there!!\n");
	_terminate_threads(coord, tdatas, n_threads);


	return NULL;
}

