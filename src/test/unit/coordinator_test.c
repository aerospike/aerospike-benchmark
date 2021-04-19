
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include <check.h>

#include <coordinator.h>

#include "main.h"


#define TEST_SUITE_NAME "thread coordinator"


/*
 * forward declare all local functions from coordinator (which aren't visible
 * from the header file)
 */
extern int _has_not_happened(const struct timespec* time);
extern int _sleep_for(uint64_t n_secs);
extern void _halt_threads(thr_coord_t* coord,
		tdata_t** tdatas, uint32_t n_threads);
extern void _terminate_threads(thr_coord_t* coord,
		tdata_t** tdatas, uint32_t n_threads);
extern void _release_threads(thr_coord_t* coord,
		tdata_t** tdatas, uint32_t n_threads);
extern void _finish_req_duration(thr_coord_t* coord);
extern void clear_cdata_counts(cdata_t* cdata);


/*
 * fixtures to use to silence stderr
 */
static void 
silence_stderr_setup(void) 
{
	// redirect stdout and stderr to /dev/null
	freopen("/dev/null", "w", stdout);
	freopen("/dev/null", "w", stderr);
}

static void
silence_stderr_teardown(void)
{
}


#define SLEEP_FOR_TEST(n_secs) \
	do { \
		struct timespec before, after; \
		clock_gettime(CLOCK_MONOTONIC, &before); \
		ck_assert_int_eq(_sleep_for((n_secs)), 0); \
		clock_gettime(CLOCK_MONOTONIC, &after); \
		ck_assert_int_gt((after.tv_sec - before.tv_sec) * 1000000000LU + (after.tv_nsec - before.tv_nsec), (n_secs) * 1000000000LU); \
	} while (0)


static void
sigusr1_handle(int signum)
{
	// do nothing
	(void) signum;
}


START_TEST(test_sleep_for_simple)
{
	SLEEP_FOR_TEST(1);
}
END_TEST


START_TEST(test_sleep_for_longer)
{
	SLEEP_FOR_TEST(10);
}
END_TEST


/*
 * with signal interrupts, the sleep should still last the specified duration
 */
START_TEST(test_sleep_for_signal_interrupts)
{
	pid_t pid;
	if ((pid = fork()) == 0) {
		// set the fork status of libtest to true so the assertions will call
		// exit(1) instead of jumping to the exit handler on failure
		set_fork_status(CK_FORK);

		sighandler_t prev_sig_handle = signal(SIGUSR1, sigusr1_handle);
		ck_assert_ptr_ne(prev_sig_handle, SIG_ERR);

		SLEEP_FOR_TEST(2);

		// return with exit code 0
		exit(0);
	}
	else {
		// raise some signals
		for (int i = 0; i < 1000; i++) {
			usleep(500);
			kill(pid, SIGUSR1);
		}

		int status;
		ck_assert_int_eq(waitpid(pid, &status, 0), pid);
		ck_assert_msg(WIFEXITED(status) && (WEXITSTATUS(status) == 0),
				"The sleep did not last the specified duration");
	}
}
END_TEST


Suite* coordinator_suite(void)
{
	Suite* s;
	TCase* tc_core;

	s = suite_create("Thread Coordinator");

	tc_core = tcase_create("Core");
	tcase_set_timeout(tc_core, 100);
	tcase_add_checked_fixture(tc_core, silence_stderr_setup,
			silence_stderr_teardown);
	tcase_add_test(tc_core, test_sleep_for_simple);
	tcase_add_test(tc_core, test_sleep_for_longer);
	tcase_add_test(tc_core, test_sleep_for_signal_interrupts);
	suite_add_tcase(s, tc_core);

	return s;
}

