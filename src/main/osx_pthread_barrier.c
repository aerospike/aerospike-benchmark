#ifdef __APPLE__

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>

#include <osx_pthread_barrier.h>

#define BARRIER_IN_THRESHOLD ((2147483647 * 2U + 1U) / 2)

#define atomic_add_fetch_explicit(object, operand, order) atomic_fetch_add_explicit(object, operand, order) + operand

int32_t
pthread_barrier_init(pthread_barrier_t* barrier, void* attr,
		uint32_t count)
{
	// attr is not implemented
	assert(attr == NULL);

	if (count == 0 || count > BARRIER_IN_THRESHOLD) {
		return EINVAL;
	}

	pthread_cond_init(&barrier->cond, NULL);
	pthread_mutex_init(&barrier->lock, NULL);

	barrier->count = count;
	atomic_init(&barrier->in, 0);
	atomic_init(&barrier->current_round, 0);

	return 0;
}

int32_t
pthread_barrier_destroy(pthread_barrier_t* barrier)
{
	pthread_cond_destroy(&barrier->cond);
	pthread_mutex_destroy(&barrier->lock);
	return 0;
}

int32_t
pthread_barrier_wait(pthread_barrier_t* barrier)
{
	// read the current round before incrementing the in variable, since we
	// require that current round be correct, and incrementing in before reading
	// current_round would induce a race
	uint32_t round = atomic_load_explicit(&barrier->current_round, memory_order_acquire);
	// increment the in variable with relaxed memory ordering since this is the
	// only modification we've made to memory
	uint32_t i = atomic_add_fetch_explicit(&barrier->in, 1, memory_order_relaxed);
	uint32_t count = barrier->count;

	if (i < count) {
		// acquire the condition lock before reading cur_round, since we don't
		// want the broadcast signal to be sent before waiting on the condition
		// variable
		pthread_mutex_lock(&barrier->lock);
		// read the current round before waiting on the condition variable
		uint32_t cur_round =
			atomic_load_explicit(&barrier->current_round, memory_order_acquire);

		while (cur_round == round) {
			pthread_cond_wait(&barrier->cond, &barrier->lock);
			cur_round = atomic_load_explicit(&barrier->current_round, memory_order_acquire);
		}

		pthread_mutex_unlock(&barrier->lock);
	}
	else {
		// reset the in-thread count to zero, preventing any of the other
		// threads at the barrier from leaving before the round is incremented
		atomic_store_explicit(&barrier->in, 0, memory_order_relaxed);
		// go to the next round, allowing all other threads waiting at the
		// barrier to leave. At this point, the state of the barrier is
		// completely reset
		atomic_store_explicit(&barrier->current_round, round + 1, memory_order_release);

		// acquire the condition lock so no thread can wait after checking the
		// condition and after the broadcast wakeup is executed
		pthread_mutex_lock(&barrier->lock);
		pthread_cond_broadcast(&barrier->cond);
		pthread_mutex_unlock(&barrier->lock);

		return PTHREAD_BARRIER_SERIAL_THREAD;
	}

	return 0;
}

#endif /* __APPLE__ */
