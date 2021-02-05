
#include <math.h>

#include <citrusleaf/cf_clock.h>

#include <common.h>
#include <dynamic_throttle.h>


int dyn_throttle_init(dyn_throttle_t* thr, float target_period)
{
	thr->target_period = target_period;
	thr->avg_fn_delay = 0;
	thr->n_records = 0;
	return 0;
}

void dyn_throttle_reset_time(dyn_throttle_t* thr, uint64_t new_rec)
{
	thr->last_rec = new_rec - ((int64_t) nearbyintf(thr->avg_fn_delay));
	thr->n_records = ramp(thr->n_records - 1);
}

uint64_t dyn_throttle_pause_for(dyn_throttle_t* dt, uint64_t rec)
{
	uint64_t pause_for;
	uint64_t n_records = dt->n_records;

	// this will only happen the first DYN_THROTTLE_N calls, after which it
	// will never happen again, so go ahead and tell the compiler to expect
	// the second case
	if (__builtin_expect(n_records < DYN_THROTTLE_N, 0)) {
		if (n_records == 0) {
			pause_for = 0;
		}
		else {
			float alpha = 1.f / n_records;
			uint64_t last_rec = dt->last_rec;
			float avg = dt->avg_fn_delay;
			avg = (1 - alpha) * avg + alpha * (rec - last_rec);
			dt->avg_fn_delay = avg;

			pause_for = (uint64_t) nearbyintf(dt->target_period - avg);
			pause_for = ramp(pause_for);
		}
	}
	else {
		uint64_t last_rec = dt->last_rec;
		float avg = dt->avg_fn_delay;
		avg = (1 - DYN_THROTTLE_ALPHA) * avg + DYN_THROTTLE_ALPHA *
			(rec - last_rec);
		dt->avg_fn_delay = avg;

		pause_for = (uint64_t) nearbyintf(dt->target_period - avg);
		pause_for = ramp(pause_for);
	}

	// account for the pause in last_rec
	dt->last_rec = rec + pause_for;
	dt->n_records = n_records + 1;
	return pause_for;
}

