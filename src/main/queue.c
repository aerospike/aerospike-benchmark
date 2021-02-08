
#include <stdio.h>

#include <aerospike/as_atomic.h>
#include <citrusleaf/alloc.h>

#include <queue.h>

static uint32_t next_pow2(uint32_t n)
{
	uint32_t leading_bits = __builtin_clz(n);
	// safe as long as n < 2**31
	return 0x80000000LU >> (leading_bits - 1);
}


int queue_init(queue_t* q, uint32_t q_len)
{
	if (q_len == 0) {
		fprintf(stderr, "Queue length cannot be 0\n");
		return -1;
	}

	// len is the next power of 2 strictly greater than q_len
	uint32_t len = next_pow2(q_len);
	q->items = (void**) cf_malloc(len * sizeof(void*));
	q->len_mask = len - 1;
	q->pos = 0;
	q->head = 0;
	return 0;
}

void queue_free(queue_t* q)
{
	cf_free(q->items);
}


void queue_push(queue_t* q, void* item)
{
	uint32_t pos = as_faa_uint32(&q->pos, 1);
	as_store_ptr(&q->items[pos & q->len_mask], item);
}


void* queue_pop(queue_t* q)
{
	void* item;
	uint32_t head = q->head;
	uint32_t pos = as_load_uint32(&q->pos);

	if (((head ^ pos) & q->len_mask)) {
		item = (void*) as_fas_uint64((uint64_t*) &q->items[head & q->len_mask],
				(uint64_t) NULL);
		// can be non-atomic since this thread is the only modifier of head
		q->head += (item != NULL);
		return item;
	}
	return NULL;
}

