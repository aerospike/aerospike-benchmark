
#include <coordinator.h>

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


void* coordinator_worker(void* udata)
{
	clientdata* cdata = (clientdata*) udata;

	return NULL;
}

