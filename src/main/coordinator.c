
#include <coordinator.h>

#include <aerospike/as_sleep.h>

#include <transaction.h>


/*
 * TODO add error checking (best way to do this cleanly?)
 */
int thr_coordinator_init(struct thr_coordinator* coord, uint32_t n_threads)
{
	pthread_condattr_t attr;

	pthread_condattr_init(&attr);
	// set the clock to MONOTONIC (the default is REALTIME)
	pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);

	pthread_cond_init(&coord->complete, &attr);
	pthread_condattr_destroy(&attr);

	pthread_mutex_init(&coord->c_lock, NULL);

	// the one extra thread is this thread
	pthread_barrier_init(&coord->barrier, NULL, n_threads + 1);
	coord->n_threads = n_threads;
	// unfinished threads includes this thread
	coord->unfinished_threads = n_threads;

	return 0;
}

void thr_coordinator_free(struct thr_coordinator* coord)
{
	pthread_barrier_destroy(&coord->barrier);
	pthread_mutex_destroy(&coord->c_lock);
	pthread_cond_destroy(&coord->complete);
}


void thr_coordinator_wait(struct thr_coordinator* coord)
{
	// wait at the barrier twice: once to sync every thread up, and again to
	// wait for the coordinator to execute initialization code before resuming
	// everything
	pthread_barrier_wait(&coord->barrier);
	pthread_barrier_wait(&coord->barrier);
}


void thr_coordinator_complete(struct thr_coordinator* coord)
{
	// first acquire the complete lock
	pthread_mutex_lock(&coord->c_lock);

	uint32_t rem_threads = coord->unfinished_threads - 1;
	coord->n_threads = rem_threads;
	// commit this write before signaling the condition variable and releasing
	// the lock, since it was not atomic
	as_fence_memory();

	if (rem_threads == 0) {
		pthread_cond_broadcast(&coord->complete);
	}
	pthread_mutex_unlock(&coord->c_lock);
}


/*
 * checks whether the time has not passed "time" yet
 */
static int _has_not_happened(const struct timespec* time)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec < time->tv_sec ||
		(now.tv_sec == time->tv_sec && now.tv_nsec < time->tv_nsec);
}


void thr_coordinator_sleep(struct thr_coordinator* coord,
		const struct timespec* wakeup_time)
{
	uint32_t rem_threads;
	pthread_mutex_lock(&coord->c_lock);

	// since condition variable waits may supriously return, we have to check
	// that the time hasn't expired each time. we also want to check that there
	// are still unfinished threads left, since if this value is 0, we don't
	// want to continue waiting any longer
	while ((rem_threads = as_load_uint32(&coord->unfinished_threads)) != 0 &&
			_has_not_happened(wakeup_time)) {
		pthread_cond_timedwait(&coord->complete, &coord->c_lock,
				wakeup_time);
	}
	pthread_mutex_unlock(&coord->c_lock);

	// if there are no threads left which still have required work, it's time
	// for the stage to end
	if (rem_threads == 0) {
		thr_coordinator_wait(coord);
	}
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

	uint32_t n_stages = cdata->stages.n_stages;
	uint32_t stage_idx = 0;
	for (;;) {
		struct stage* stage = &cdata->stages.stages[stage_idx];
		printf("Stage %d: %s\n", stage_idx + 1, stage->desc);

		if (stage->duration > 0) {
			// first sleep the minimum duration of the stage
			as_sleep(stage->duration * 1000);
		}
		// TODO

		// halt threads, increment stage indices, then continue
		_halt_threads(coord, tdatas, n_threads);
		stage_idx++;

		if (stage_idx == n_stages) {
			// all done, terminate threads and exit
			_terminate_threads(coord, tdatas, n_threads);
			break;
		}
		else {
			// advance to the next stage
			for (uint32_t t_idx = 0; t_idx < n_threads; t_idx++) {
				tdatas[t_idx]->stage_idx = stage_idx;
			}
			// reset unfinished_threads count
			as_store_uint32(&coord->unfinished_threads, n_threads);

			_release_threads(coord, tdatas, n_threads);
		}
	}

	return NULL;
}

