
//==========================================================
// Includes
//

#include <coordinator.h>

#include <errno.h>

#include <common.h>
#include <transaction.h>


//==========================================================
// Forward declarations.
//

LOCAL_HELPER int _has_not_happened(const struct timespec* time);
LOCAL_HELPER int _sleep_for(uint64_t n_secs);
LOCAL_HELPER void _halt_threads(thr_coord_t* coord,
		tdata_t** tdatas, uint32_t n_threads);
LOCAL_HELPER void _terminate_threads(thr_coord_t* coord,
		tdata_t** tdatas, uint32_t n_threads);
LOCAL_HELPER void _release_threads(thr_coord_t* coord,
		tdata_t** tdatas, uint32_t n_threads);
LOCAL_HELPER void _finish_req_duration(thr_coord_t* coord);
LOCAL_HELPER void clear_cdata_counts(cdata_t* cdata);


//==========================================================
// Public API.
//

/*
 * TODO add error checking (best way to do this cleanly?)
 */
int
thr_coordinator_init(thr_coord_t* coord, uint32_t n_threads)
{
#ifndef __APPLE__
	pthread_condattr_t attr;

	pthread_condattr_init(&attr);
	// set the clock to MONOTONIC (the default is REALTIME)
	pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);

	pthread_cond_init(&coord->complete, &attr);

	pthread_condattr_destroy(&attr);
#else
	pthread_cond_init(&coord->complete, NULL);
#endif /* __APPLE_ */

	pthread_mutex_init(&coord->c_lock, NULL);

	// the one extra thread is this thread
	pthread_barrier_init(&coord->barrier, NULL, n_threads + 1);
	coord->n_threads = n_threads;
	// unfinished threads includes this thread
	atomic_init(&coord->unfinished_threads, n_threads + 1);

	return 0;
}

void
thr_coordinator_free(thr_coord_t* coord)
{
	pthread_barrier_destroy(&coord->barrier);
	pthread_mutex_destroy(&coord->c_lock);
	pthread_cond_destroy(&coord->complete);
}

void
thr_coordinator_wait(thr_coord_t* coord)
{
	// wait at the barrier twice: once to sync every thread up, and again to
	// wait for the coordinator to execute initialization code before resuming
	// everything
	pthread_barrier_wait(&coord->barrier);
	pthread_barrier_wait(&coord->barrier);
}

void
thr_coordinator_complete(thr_coord_t* coord)
{
	// first acquire the complete lock
	pthread_mutex_lock(&coord->c_lock);

	uint32_t rem_threads = atomic_fetch_sub(&coord->unfinished_threads, 1) - 1;

	if (rem_threads == 0) {
		pthread_cond_broadcast(&coord->complete);
	}
	pthread_mutex_unlock(&coord->c_lock);
}

int
thr_coordinator_sleep(thr_coord_t* coord,
		const struct timespec* wakeup_time)
{
	uint32_t rem_threads;
	pthread_mutex_lock(&coord->c_lock);

	// since condition variable waits may spuriously return, we have to check
	// that the time hasn't expired each time. we also want to check that there
	// are still unfinished threads left, since if this value is 0, we don't
	// want to continue waiting any longer
	while ((rem_threads = coord->unfinished_threads) != 0 &&
			_has_not_happened(wakeup_time)) {
		pthread_cond_timedwait(&coord->complete, &coord->c_lock,
				wakeup_time);
	}
	pthread_mutex_unlock(&coord->c_lock);

	return rem_threads == 0 ? COORD_SLEEP_INTERRUPTED : COORD_SLEEP_TIMEOUT;
}

void*
coordinator_worker(void* udata)
{
	struct coordinator_worker_args_s* args =
		(struct coordinator_worker_args_s*) udata;
	thr_coord_t* coord = args->coord;
	cdata_t* cdata = args->cdata;
	tdata_t** tdatas = args->tdatas;
	uint32_t n_threads = args->n_threads;
	as_random random;

	uint32_t n_stages = cdata->stages.n_stages;
	uint32_t stage_idx = 0;

	as_random_init(&random);

	for (;;) {
		stage_t* stage = &cdata->stages.stages[stage_idx];
		fprint_stage(stdout, &cdata->stages, stage_idx);

		if (stage->workload.type == WORKLOAD_TYPE_I && stage->batch_write_size > 1) {
			uint64_t nkeys = stage->key_end - stage->key_start;
			if (nkeys % stage->batch_write_size != 0) {
				blog_warn("--keys is not divisible by --batch-write-size so more than "
							"--keys records will be written\n");
			}

			if (nkeys % (stage->batch_write_size * n_threads) != 0) {
				blog_warn("--keys is not divisible by (--batch-write-size * --threads) so some records "
							"outside the range defined by --keys will be written\n");
			}

			if (!stage->async) {
				if (stage->batch_write_size * n_threads > nkeys) {
					blog_warn("--batch-write-size * --threads is greater than --keys so "
								"more than --keys records will be written\n");
				}
			}
		}

		if (stage->workload.type == WORKLOAD_TYPE_D && stage->batch_delete_size > 1) {
			uint64_t nkeys = stage->key_end - stage->key_start;
			if (nkeys % stage->batch_delete_size != 0) {
				blog_warn("--keys is not divisible by --batch-delete-size so some records "
							"outside the range defined by --keys will be deleted\n");
			}

			if (nkeys % (stage->batch_delete_size * n_threads) != 0) {
				blog_warn("--keys is not divisible by (--batch-delete-size * --threads) so some records "
							"outside the range defined by --keys will be deleted\n");
			}

			if (!stage->async) {
				if (stage->batch_delete_size * n_threads > nkeys) {
					blog_warn("--batch-delete-size * --threads is greater than --keys so some "
								"records outside the range defined by --keys will be deleted\n");
				}
			}
		}

		if (stage->duration > 0) {
			// first sleep the minimum duration of the stage
			_sleep_for(stage->duration);
		}
		_finish_req_duration(coord);

		// at this point, all threads have completed their required tasks, so
		// halt threads, increment stage indices, then continue
		_halt_threads(coord, tdatas, n_threads);
		stage_idx++;

		clear_cdata_counts(cdata);

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

			stage_random_pause(&random, &cdata->stages.stages[stage_idx]);

			// reset unfinished_threads count
			coord->unfinished_threads = n_threads + 1;

			_release_threads(coord, tdatas, n_threads);
		}
	}

	return NULL;
}


//==========================================================
// Local helpers.
//

/*
 * checks whether the time has not passed "time" yet
 */
LOCAL_HELPER int
_has_not_happened(const struct timespec* time)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec < time->tv_sec ||
		(now.tv_sec == time->tv_sec && now.tv_nsec < time->tv_nsec);
}

/*
 * sleep for at least the specified number of seconds, which is safe on signals
 *
 * returns 0 if the sleep was successful, otherwise -1 if an error occured
 */
LOCAL_HELPER int
_sleep_for(uint64_t n_secs)
{
	struct timespec sleep_time;
	int res;

	sleep_time.tv_sec = n_secs;
	sleep_time.tv_nsec = 0;

	do {
		res = nanosleep(&sleep_time, &sleep_time);
	} while (res != 0 && errno == EINTR);

	return res;
}

/*
 * halt all threads and return once all threads have called thr_coordinator_wait
 */
LOCAL_HELPER void
_halt_threads(thr_coord_t* coord,
		tdata_t** tdatas, uint32_t n_threads)
{
	for (uint32_t i = 0; i < n_threads; i++) {
		tdatas[i]->do_work = false;
	}
	pthread_barrier_wait(&coord->barrier);
}

/*
 * signal to all threads to stop execution and return,
 * must be called after a call to _halt_threads
 */
LOCAL_HELPER void
_terminate_threads(thr_coord_t* coord,
		tdata_t** tdatas, uint32_t n_threads)
{
	for (uint32_t i = 0; i < n_threads; i++) {
		tdatas[i]->finished = true;
	}
	pthread_barrier_wait(&coord->barrier);
}

/*
 * signal to all threads to continue execution,
 * must be called after a call to _halt_threads
 */
LOCAL_HELPER void
_release_threads(thr_coord_t* coord,
		tdata_t** tdatas, uint32_t n_threads)
{
	for (uint32_t i = 0; i < n_threads; i++) {
		tdatas[i]->do_work = true;
	}
	pthread_barrier_wait(&coord->barrier);
}

/*
 * to be called by the coordinator thread just after returning from the sleep
 * call (i.e. once the required sleep duration has passed). this will decrement
 * the count of unfinished threads and wait on the condition variable until
 * all threads have finished their required tasks
 */
LOCAL_HELPER void
_finish_req_duration(thr_coord_t* coord)
{
	pthread_mutex_lock(&coord->c_lock);

	uint32_t rem_threads = atomic_fetch_sub(&coord->unfinished_threads, 1) - 1;

	// if we're the last thread finishing, notify any threads waiting on the
	// complete condition variable
	if (rem_threads == 0) {
		pthread_cond_broadcast(&coord->complete);
	}

	// wait for the rest of the threads to complete
	while (rem_threads != 0) {
		pthread_cond_wait(&coord->complete, &coord->c_lock);
		rem_threads = coord->unfinished_threads;
	}
	pthread_mutex_unlock(&coord->c_lock);
}

/*
 * clear the transaction history in case some stragglers in RU workloads did
 * an extra transaction after the latency_output thread had already printed its
 * last status report
 *
 * note: this is not thread safe, only call this when all other threads are
 * halted
 */
LOCAL_HELPER void
clear_cdata_counts(cdata_t* cdata)
{
	cdata->write_count = 0;
	cdata->write_timeout_count = 0;
	cdata->write_error_count = 0;
	cdata->read_hit_count = 0;
	cdata->read_miss_count = 0;
	cdata->read_timeout_count = 0;
	cdata->read_error_count = 0;
	cdata->udf_count = 0;
	cdata->udf_timeout_count = 0;
	cdata->udf_error_count = 0;
}

