#ifdef __APPLE__

#include <assert.h>
#include <errno.h>
#include <pthread.h>

#include <osx_pthread_barrier.h>

#define BARRIER_IN_THRESHOLD ((2147483647 * 2U + 1U) / 2)


int32_t
pthread_barrier_init(pthread_barrier_t* barrier, void* attr,
		uint32_t count)
{
	// attr is not implemented
	assert(attr == NULL);

	if (count == 0 || count > BARRIER_IN_THRESHOLD) {
		return EINVAL;
	}

	/* Initialize the individual fields.  */
	barrier->in = 0;
	barrier->out = 0;
	barrier->count = count;
	barrier->current_round = 0;

	return 0;
}

int32_t
pthread_barrier_destroy(pthread_barrier_t* barrier)
{
	return 0;
}

int32_t
pthread_barrier_wait(pthread_barrier_t* barrier)
{
	return 0;
}

#endif /* __APPLE__ */
