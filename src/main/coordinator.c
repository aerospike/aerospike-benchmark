
#include <coordinator.h>

#include <transaction.h>


int thr_coordinator_init(struct thr_coordinator* coord)
{
	return 0;
}

void thr_coordinator_free(struct thr_coordinator* coord)
{
}


void* coordinator_worker(void* udata);

