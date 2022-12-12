
//==========================================================
// Includes.
//

#include <stdio.h>
#include <stdatomic.h>

#include <pthread.h>

#include <citrusleaf/alloc.h>

#include <common.h>
#include <queue.h>


//==========================================================
// Forward declarations.
//

LOCAL_HELPER uint32_t next_pow2(uint32_t n);


//==========================================================
// Public API.
//

int
queue_init(queue_t* q, uint32_t q_len)
{
	if (q_len == 0) {
		fprintf(stderr, "Queue length cannot be 0\n");
		return -1;
	}

	// len is the next power of 2 strictly greater than q_len
	uint32_t len = next_pow2(q_len);
	q->items = (_Atomic(void*)*) cf_calloc(len, sizeof(_Atomic(void*)));

	for(uint32_t i = 0; i < len; i++) {
		atomic_init(&q->items[i], NULL);
	}

	q->len_mask = len - 1;
	q->head = 0;
	atomic_init(&q->pos, 0);
	return 0;
}

void
queue_free(queue_t* q)
{
	cf_free(q->items);
}


void
queue_push(queue_t* q, void* item)
{
	uint32_t pos = atomic_fetch_add(&q->pos, 1);
	// no race condition because 'head' is only incremented in pop if item is not null
	q->items[pos & q->len_mask] = item;
}


void*
queue_pop(queue_t* q)
{
	void* item;
	uint32_t head = q->head;

	// Since uint32_t can overflow with only ~4B transactions, use !=, not <
	if (head != q->pos) {
		item = atomic_exchange(&q->items[head & q->len_mask], NULL);
		// can be non-atomic since this thread is the only modifier of head
		q->head += (item != NULL);
		return item;
	}
	return NULL;
}


//==========================================================
// Local helpers.
//

LOCAL_HELPER uint32_t
next_pow2(uint32_t n)
{
	uint32_t leading_bits = __builtin_clz(n);
	// safe as long as n < 2**31
	return 0x80000000LU >> (leading_bits - 1);
}
