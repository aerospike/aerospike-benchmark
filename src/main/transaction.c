
#include <transaction.h>

#include <benchmark.h>
#include <workload.h>


struct threaddata* init_tdata(clientdata* cdata, struct thr_coordinator* coord,
		uint32_t t_idx)
{
	struct threaddata* tdata =
		(struct threaddata*) cf_malloc(sizeof(struct threaddata));

	tdata->cdata = cdata;
	tdata->coord = coord;
	tdata->random = as_random_instance();
	tdata->t_idx = t_idx;
	tdata->do_work = false;
	tdata->finished = false;

	return tdata;
}


void* transaction_worker(void* udata)
{
	struct threaddata* tdata = (struct threaddata*) udata;
	clientdata* cdata = tdata->cdata;
	struct stage* stage = &cdata->stages.stages[0];

	switch (stage->workload.type) {
		case WORKLOAD_TYPE_LINEAR:
			break;
		case WORKLOAD_TYPE_RANDOM:
			break;
		case WORKLOAD_TYPE_DELETE:
			break;
	}

	return NULL;
}

void* transaction_worker_async(void* udata)
{
	return NULL;
}

