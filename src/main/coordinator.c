
#include <coordinator.h>

#include <transaction.h>


int thr_coordinator_init(struct thr_coordinator* coord, uint32_t n_threads)
{
	coord->n_threads = n_threads;
	return 0;
}

void thr_coordinator_free(struct thr_coordinator* coord)
{
}


void* coordinator_worker(void* udata)
{

}

