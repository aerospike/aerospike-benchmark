
#include <assert.h>
#include <stdio.h>

#include <aerospike/as_arraylist.h>
#include <aerospike/as_bytes.h>
#include <aerospike/as_double.h>
#include <aerospike/as_hashmap.h>
#include <aerospike/as_integer.h>
#include <aerospike/as_list.h>
#include <aerospike/as_string.h>
#include <aerospike/as_vector.h>
#include <citrusleaf/alloc.h>

#include <common.h>
#include <object_spec.h>


#define DEFAULT_LIST_BUILDER_CAPACITY 8


#define BIN_SPEC_TYPE_INT    0x0
#define BIN_SPEC_TYPE_STR    0x1
#define BIN_SPEC_TYPE_BYTES  0x2
#define BIN_SPEC_TYPE_DOUBLE 0x3
#define BIN_SPEC_TYPE_LIST   0x4
#define BIN_SPEC_TYPE_MAP    0x5

#define BIN_SPEC_TYPE_MASK 0x7


/*
 * the maximum length of a word in randomly generated strings
 */
#define BIN_SPEC_MAX_STR_LEN 9

struct bin_spec {

	union {

		/*
		 * one of the four main types of bins:
		 *	int: a random int, within certain bounds (described below)
		 *	string: a string of fixed length, consisting of [a-z]{1,9}
		 *			space-separated words
		 *	bytes array: array of random bytes of data
		 *	double: any 8-byte double floating point
		 *	list: a list of bin_specs
		 *	map: a map from some type of scalar bin_spec to another bin_spec
		 */
		uint8_t type;

		struct {
			uint64_t __unused;
			/*
			 * integer range is between 0-7, and are defined as follows:
			 * 	0: values from 0 - 255
			 * 	1: values from 256 - 65536
			 * 	2: values from 65536 - 2^24-1
			 * 	3: values from 2^24 - 2^32-1
			 * 	4: values from 2^32 - 2^40-1
			 * 	5: values from 2^40 - 2^48-1
			 * 	6: values from 2^48 - 2^56-1
			 * 	7: values from 2^56 - 2^64-1
			 */
			uint8_t range;
		} integer;

		struct {
			uint64_t __unused;
			/*
			 * length of strings to be generated (excluding the
			 * null-terminating bit)
			 */
			uint32_t length;
		} string;

		struct {
			uint64_t __unused;
			/*
			 * number of random bytes
			 */
			uint32_t length;
		} bytes;

		struct {
			uint32_t __unused;
			/*
			 * a list of the types of elements in this list (in the order they
			 * appear in the list)
			 *
			 * note that this is the length of the list accounting for multiples
			 * of elements, and the true length of the list (as in the number of
			 * struct bin_spec pointers) is <= this value
			 */
			uint32_t length;
			struct bin_spec* list;
		} list;

		struct {
			/*
			 * the number of entries in a map is specified by the key's
			 * n_repeats field, so we don't need to put the number of elements
			 * to be generated this level of the struct
			 */

			/*
			 * a pointer to the key type
			 *
			 * the key pointers must be aligned by 8 bytes, since the first 3
			 * bits of this pointer is aliased by the type of this bin_spec
			 */
			struct bin_spec* key;
			/*
			 * a pointer to the value type
			 */
			struct bin_spec* val;
		} map;

	};

	uint32_t n_repeats;
};


// parsing a list currently
#define CONSUMER_TYPE_LIST 0x0
// parsing a map currently
#define CONSUMER_TYPE_MAP  0x1

// in a map and expecting the key next
#define MAP_KEY  0x0
// in a map and expecting the value next
#define MAP_VAL  0x1
// key and value already given, expecing the map to be closed with a '}'
#define MAP_DONE 0x2

struct consumer_state {
	// the character which denotes the end of this bin spec
	char delimiter;

	// one of CONSUMER_TYPE_LIST or CONSUMER_TYPE_MAP
	uint8_t type;

	// used only for the map, can be one of MAP_KEY, MAP_VAL, or MAP_DONE
	uint8_t state;

	// a pointer to the bin spec that is being built at this layer
	struct bin_spec* bin_spec;

	struct consumer_state* parent;

	union {
		struct {
			// the vector of bin_specs used to build a list
			as_vector* list_builder;

			// the length of the list, accounting for multipliers
			uint32_t list_len;
		};

		// the key/value bin_specs used to build a map
		struct {
			struct bin_spec* key;
			struct bin_spec* value;
		};
	};
};

static void bin_spec_set_key_and_type(struct bin_spec* b, struct bin_spec* key)
{
	b->map.key = (struct bin_spec*) (((uint64_t) key) | BIN_SPEC_TYPE_MAP);
}

static uint8_t bin_spec_get_type(const struct bin_spec* b)
{
	return b->type & BIN_SPEC_TYPE_MASK;
}

static struct bin_spec* bin_spec_get_key(const struct bin_spec* b)
{
	return (struct bin_spec*) (((uint64_t) b->map.key) & ~BIN_SPEC_TYPE_MASK);
}

/*
 * gives the number of entries to put in the map
 */
static uint32_t bin_spec_map_n_entries(const struct bin_spec* b)
{
	return bin_spec_get_key(b)->n_repeats;
}


static void _print_parse_error(const char* err_msg, const char* obj_spec_str,
		const char* err_loc)
{
	const char* last_newline;
	const char* next_newline;

	last_newline = memrchr(obj_spec_str, '\n',
			((uint64_t) err_loc) - ((uint64_t) obj_spec_str));
	next_newline = strchrnul(err_loc, '\n');

	if (last_newline == NULL) {
		last_newline = obj_spec_str - 1;
	}

	int32_t line_length = (int32_t)
		(((uint64_t) next_newline) - ((uint64_t) last_newline) - 1);
	int32_t err_offset = (int32_t)
		(((uint64_t) err_loc) - ((uint64_t) last_newline) - 1);

	fprintf(stderr,
			"Object Spec parse error: %s\n"
			"%*s\n"
			"%*s^\n",
			err_msg,
			line_length, last_newline + 1,
			err_offset, "");
}


// forward declare helper method for obj_spec_free, to be used in
// _destroy_consumer_states
static void bin_spec_free(struct bin_spec* bin_spec);

/*
 * to be called when an error is encountered while parsing, and cleanup of the
 * consumer state managers and bin_specs is necessary
 */
static void _destroy_consumer_states(struct consumer_state* state)
{
	struct consumer_state* parent;
	as_vector* list_builder;

	while (state != NULL) {
		switch (state->type) {
			case CONSUMER_TYPE_LIST:
				// go through everything in list_builder that has, so far, been
				// fully initialized
				list_builder = state->list_builder;
				for (uint32_t i = 0;
						(int32_t) i < (int32_t) (list_builder->size - 1); i++) {
					// since all bin_spec objects but the last have been fully
					// initialized, we can call free on each
					bin_spec_free(
							(struct bin_spec*) as_vector_get(list_builder, i));
				}
				// the last bin_spec is guaranteed to not be initialized yet,
				// so ignore it
				as_vector_destroy(list_builder);
				break;
			case CONSUMER_TYPE_MAP:
				// free the key/value if they've been initialized
				switch (state->state) {
					case MAP_DONE:
						bin_spec_free(state->value);
						cf_free(state->value);
						// fall through to free key too
					case MAP_VAL:
						bin_spec_free(state->key);
						cf_free(state->key);
					case MAP_KEY:
						break;
					default:
						__builtin_unreachable();
				}
				break;
			default:
				__builtin_unreachable();
		}

		parent = state->parent;
		// since the root state is allocated on the stack, don't try freeing it
		if (parent != NULL) {
			cf_free(state);
		}
		state = parent;
	}
}


static int _parse_bin_types(as_vector* bin_specs, uint32_t* n_bins,
		const char* const obj_spec_str)
{
	struct consumer_state begin_state;
	struct consumer_state* state;
	struct bin_spec* bin_spec;
	const char* str;

	begin_state.delimiter = '\0';
	begin_state.type = CONSUMER_TYPE_LIST;
	begin_state.bin_spec = NULL;
	begin_state.parent = NULL;
	begin_state.list_builder = bin_specs;
	begin_state.list_len = 0;

	state = &begin_state;
	str = obj_spec_str;

	do {
		uint8_t delim;
		uint8_t type;
		uint8_t map_state;
		as_vector* list_builder;

_top:
		delim = state->delimiter;
		type = state->type;

		// this variable will be initialized when reading a map, but we always
		// start with a list so it will technically be uninitialized first
		// time around, which gcc will complain about
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
		map_state = state->state;
#pragma GCC diagnostic pop

		if (*str == delim) {
			// we are finished parsing this collection, commit results back to
			// the bin_spec object being built
			bin_spec = state->bin_spec;
			if (bin_spec != NULL) {
				switch (type) {
					case CONSUMER_TYPE_LIST:
						if (state->list_builder->size == 0) {
							_print_parse_error("Cannot have empty list",
									obj_spec_str, str);
							_destroy_consumer_states(state);
							return -1;
						}
						bin_spec->type = BIN_SPEC_TYPE_LIST;
						bin_spec->list.list =
							as_vector_to_array(state->list_builder,
									&bin_spec->list.length);
						// the length we'll be storing is the effective length,
						// so overwrite what was stored by as_vector_to_array
						bin_spec->list.length = state->list_len;
						as_vector_destroy(state->list_builder);
						break;
					case CONSUMER_TYPE_MAP:
						if (map_state != MAP_DONE) {
							_print_parse_error("Unexpected token '}' before "
									"both key and value have been provided "
									"for a map",
									obj_spec_str, str);
							_destroy_consumer_states(state);
							return -1;
						}
						bin_spec_set_key_and_type(bin_spec, state->key);
						bin_spec->map.val = state->value;
						break;
					default:
						// this is an impossible condition
						__builtin_unreachable();
				}
				str++;
			}
			struct consumer_state* new_state = state->parent;
			if (new_state != NULL) {
				cf_free(state);

				delim = new_state->delimiter;
				type = new_state->type;
				map_state = new_state->state;
			}
			else {
				// this is the root-level list, so commit the effective length
				// to the n_bins variable passed by the caller
				*n_bins = state->list_len;
			}
			state = new_state;
		}
		else {

			// consume the next bin_type
			switch (type) {
				case CONSUMER_TYPE_LIST:
					list_builder = state->list_builder;
					bin_spec = (struct bin_spec*) as_vector_reserve(list_builder);
					break;
				case CONSUMER_TYPE_MAP:
					bin_spec =
						(struct bin_spec*) cf_malloc(sizeof(struct bin_spec));
					assert((((uint64_t) bin_spec) & BIN_SPEC_TYPE_MASK) == 0);
					switch (map_state) {
						case MAP_KEY:
							state->key = bin_spec;
							break;
						case MAP_VAL:
							state->value = bin_spec;
							break;
						default:
							// not actually possible to reach this case, since
							// it's checked for in the previous iteration of the
							// loop
							__builtin_unreachable();
					}
					break;
				default:
					// this is an impossible condition
					__builtin_unreachable();
			}

			// first, check to see if a multiplier has been applied to this
			// bin_type
			uint64_t mult;
			char* endptr;
			mult = strtoul(str, &endptr, 10);
			if (*str >= '1' && *str <= '9' && endptr != str) {

				if (type == CONSUMER_TYPE_MAP && map_state == MAP_VAL &&
						mult != 1) {
					_print_parse_error("Map value cannot have a multiplier",
							obj_spec_str, str);
					goto _destroy_state;
				}

				if (((uint32_t) mult) != mult) {
					_print_parse_error("Multiplier exceeds maximum unsigned "
							"32-bit integer value",
							obj_spec_str, str);
					goto _destroy_state;
				}

				// a multiplier has been specified, expect a '*' next, followed by
				// the bin_spec
				str = endptr;
				if (*str != '*') {
					_print_parse_error("Expect a '*' to follow a multiplier",
							obj_spec_str, str);
					goto _destroy_state;
				}
				str++;
			}
			else {
				// no multiplier has been specified, default to 1
				mult = 1;
			}

			if (type == CONSUMER_TYPE_LIST) {
				uint32_t new_list_len = state->list_len + mult;
				if (new_list_len < state->list_len) {
					// we overflowed! That means too many elements were placed
					// in a single list (> 2^32)
					_print_parse_error("Too many elements in a list (>= 2^32)",
							obj_spec_str, str);
					goto _destroy_state;
				}
				state->list_len = new_list_len;
			}
			bin_spec->n_repeats = mult;
			switch (*str) {
				case 'I': {
					uint8_t next_char = *(str + 1);
					if (next_char < '1' || next_char > '8') {
						_print_parse_error("Expect a digit between 1-8 after 'I'",
								obj_spec_str, str + 1);
						goto _destroy_state;
					}
					bin_spec->type = BIN_SPEC_TYPE_INT;
					bin_spec->integer.range = next_char - '1';

					str += 2;
					break;
				}
				case 'S': {
					uint64_t str_len;
					char* endptr;
					str_len = strtoul(str + 1, &endptr, 10);
					if (endptr == str + 1) {
						_print_parse_error("Expect a number following an 'S' specifier",
								obj_spec_str, str + 1);
						goto _destroy_state;
					}
					if (str_len == 0 || str_len != (uint32_t) str_len) {
						_print_parse_error("Invalid string length",
								obj_spec_str, str + 1);
						goto _destroy_state;
					}
					bin_spec->type = BIN_SPEC_TYPE_STR;
					bin_spec->string.length = (uint32_t) str_len;;

					str = endptr;
					break;
				}
				case 'B': {
					uint64_t bytes_len;
					char* endptr;
					bytes_len = strtoul(str + 1, &endptr, 10);
					if (endptr == str + 1) {
						_print_parse_error("Expect a number following a 'B' specifier",
								obj_spec_str, str + 1);
						goto _destroy_state;
					}
					if (bytes_len == 0 || bytes_len != (uint32_t) bytes_len) {
						_print_parse_error("Invalid bytes length",
								obj_spec_str, str + 1);
						goto _destroy_state;
					}
					bin_spec->type = BIN_SPEC_TYPE_BYTES;
					bin_spec->bytes.length = (uint32_t) bytes_len;

					str = endptr;
					break;
				}
				case 'D':
					bin_spec->type = BIN_SPEC_TYPE_DOUBLE;
					str++;
					break;
				case '[': {
					if (type == CONSUMER_TYPE_MAP && map_state == MAP_KEY) {
						_print_parse_error("Map key must be scalar type, "
								"cannot be list",
								obj_spec_str, str);
						goto _destroy_state;
					}
					// begin list parse
					as_vector* list_builder = as_vector_create(sizeof(struct bin_spec),
							DEFAULT_LIST_BUILDER_CAPACITY);
					struct consumer_state* list_state =
						(struct consumer_state*) cf_malloc(sizeof(struct consumer_state));
					list_state->delimiter = ']';
					list_state->type = CONSUMER_TYPE_LIST;
					list_state->bin_spec = bin_spec;
					list_state->parent = state;
					list_state->list_builder = list_builder;
					list_state->list_len = 0;

					str++;
					state = list_state;
					goto _top;
				}
				case '{': {
					if (type == CONSUMER_TYPE_MAP && map_state == MAP_KEY) {
						_print_parse_error("Map key must be scalar type, "
								"cannot be map",
								obj_spec_str, str);
						goto _destroy_state;
					}
					// begin map parse
					struct consumer_state* map_state =
						(struct consumer_state*) cf_malloc(sizeof(struct consumer_state));
					map_state->delimiter = '}';
					map_state->type = CONSUMER_TYPE_MAP;
					map_state->state = MAP_KEY;
					map_state->bin_spec = bin_spec;
					map_state->parent = state;

					str++;
					state = map_state;
					goto _top;
				}
				default:
					_print_parse_error("Expect 'I', 'S', 'B', or 'D' specifier, "
							"or a list/map",
							obj_spec_str, str);
					goto _destroy_state;
			}

			if (0) {
_destroy_state:
				if (type == CONSUMER_TYPE_MAP) {
					cf_free(bin_spec);
				}
				_destroy_consumer_states(state);
				return -1;
			}
		}

		switch (type) {
			case CONSUMER_TYPE_LIST:
				if (*str == ',') {
					str++;
				}
				else if (*str != delim) {
					_print_parse_error("Expect ',' separating bin specifiers in a list",
							obj_spec_str, str);
					_destroy_consumer_states(state);
					return -1;
				}
				break;
			case CONSUMER_TYPE_MAP:
				// advance map to the next state since the key/value for this
				// state has now been fully initialized
				state->state++;

				switch (map_state) {
					case MAP_KEY:
						if (*str != ':') {
							_print_parse_error("Expect ':' separating key and "
									"value pair in a map",
									obj_spec_str, str);
							_destroy_consumer_states(state);
							return -1;
						}
						str++;
						break;
					case MAP_VAL:
						if (*str != delim) {
							_print_parse_error("Expect '}' after key/value "
									"pair specifier in a map",
									obj_spec_str, str);
							_destroy_consumer_states(state);
							return -1;
						}
						break;
				}
				break;
			default:
				// this is an impossible condition
				__builtin_unreachable();
		}

	} while (state != NULL);

	return 0;
}

int obj_spec_parse(struct obj_spec* base_obj, const char* obj_spec_str)
{
	int err;
	// use as_vector to build the list of bin_specs, as it has dynamic sizing
	as_vector bin_specs;
	uint32_t n_bins;

	// begin with a capacity of 8
	as_vector_inita(&bin_specs, sizeof(struct bin_spec),
			DEFAULT_LIST_BUILDER_CAPACITY);

	err = _parse_bin_types(&bin_specs, &n_bins, obj_spec_str);
	if (err) {
		goto cleanup;
	}

	// copy the vector into base_obj before cleaning up
	base_obj->bin_specs = as_vector_to_array(&bin_specs, &base_obj->n_bin_specs);
	base_obj->n_bin_specs = n_bins;

cleanup:
	as_vector_destroy(&bin_specs);
	return err;
}


static void bin_spec_free(struct bin_spec* bin_spec)
{
	struct bin_spec* key;
	switch (bin_spec_get_type(bin_spec)) {
		case BIN_SPEC_TYPE_INT:
		case BIN_SPEC_TYPE_STR:
		case BIN_SPEC_TYPE_BYTES:
		case BIN_SPEC_TYPE_DOUBLE:
			// no-op, scalar types use no disjointed memory
			break;
		case BIN_SPEC_TYPE_LIST:
			for (uint32_t i = 0, cnt = 0; cnt < bin_spec->list.length; i++) {
				cnt += bin_spec->list.list[i].n_repeats;
				bin_spec_free(&bin_spec->list.list[i]);
			}
			cf_free(bin_spec->list.list);
			break;
		case BIN_SPEC_TYPE_MAP:
			key = bin_spec_get_key(bin_spec);
			bin_spec_free(key);
			cf_free(key);
			bin_spec_free(bin_spec->map.val);
			cf_free(bin_spec->map.val);
			break;
	}
}

void obj_spec_free(struct obj_spec* obj_spec)
{
	for (uint32_t i = 0, cnt = 0; cnt < obj_spec->n_bin_specs; i++) {
		cnt += obj_spec->bin_specs[i].n_repeats;
		bin_spec_free(&obj_spec->bin_specs[i]);
	}
	cf_free(obj_spec->bin_specs);
}


static as_val* _gen_random_int(uint8_t range, as_random* random)
{
	as_integer* val;
	uint64_t r;
	uint64_t min;
	uint64_t range_size;
	if (range > 7) {
		fprintf(stderr, "bin_spec integer range must be between 0-7, got %u\n",
				range);
		return NULL;
	}

	/*
	 * min values for ranges are:
	 * 	0: 0
	 * 	1: 256
	 * 	2: 2^16
	 * 	3: 2^24
	 * 	...
	 * 	so they are just 2^(8*range), with the exception of 0
	 *
	 * 	and the sizes of each range is just 2^(8*range + 8) - 2^(8*range) =
	 * 	    0xff << (8*range), with the exception of range 0, which has one
	 * 	    extra possible value (0)
	 */
	min = (0x001LU << (range * 8)) & ~0x1LU;
	range_size = (0xffLU << (range * 8)) + !range;

	r = gen_rand_range_64(random, range_size) + min;

	val = as_integer_new(r);
	return (as_val*) val;
}

static as_val* _gen_random_str(uint32_t length, as_random* random)
{
	as_string* val;
	char* buf;
	uint32_t i = 0, j;

	buf = (char*) cf_malloc(length + 1);

	while (i < length) {
		uint32_t word_length;
		uint32_t diff = length - i;

		if (diff > BIN_SPEC_MAX_STR_LEN) {
			word_length = gen_rand_range(random, BIN_SPEC_MAX_STR_LEN) + 1;
		}
		else if (diff == 1) {
			word_length = 1;
		}
		else {
			word_length = gen_rand_range(random, diff - 1);
			word_length = word_length == 0 ? diff : word_length;
		}

		// allowed characters are only [a-z], and with 26 possibilities and a
		// max of 9 characters in the string, we need at least log_2(26^9) < 43
		// bits of entropy, so just draw one random 64-bit number
		uint64_t seed = as_random_next_uint64(random);

		for (j = 0; j < word_length; j++) {
			// with 26 letters in the alphabet, choose uniformly randomly from
			// it by looking at the least significant digit of the number
			// base 26, then divide out that least significant digit
			buf[i + j] = 'a' + (seed % 26);
			seed /= 26;
		}

		buf[i + word_length] = ' ';
		i += word_length + 1;
	}

	// null-terminate the string (do at the end to please the prefetcher)
	buf[length] = '\0';

	val = as_string_new_wlen(buf, length, 1);
	return (as_val*) val;
}

static as_val* _gen_random_bytes(uint32_t length, as_random* random)
{
	as_bytes* val;
	uint8_t* buf;

	buf = (uint8_t*) cf_malloc(length);
	as_random_next_bytes(random, buf, length);

	val = as_bytes_new_wrap(buf, length, 1);
	return (as_val*) val;
}

static as_val* _gen_random_double(as_random* random)
{
	as_double* val;
	// for now, just generate random uint64 and reinterpret as double
	uint64_t bytes = as_random_next_uint64(random);
	val = as_double_new(*(double*) &bytes);
	return (as_val*) val;
}


/*
 * forward declare for use in list/map construction helpers
 */
static as_val* bin_spec_random_val(const struct bin_spec* bin_spec,
		as_random* random);


static as_val* _gen_random_list(const struct bin_spec* bin_spec,
		as_random* random)
{
	as_arraylist* list;

	/*
	 * we'll build the list as an arraylist, which we can't do directly in the
	 * bin, since the bin is only large enough to hold as_list fields
	 */
	list = as_arraylist_new(bin_spec->list.length, 0);

	// iterate over the list elements and recursively generate their
	// values
	for (uint32_t i = 0; i < bin_spec->list.length; i++) {
		const struct bin_spec* ele_bin = &bin_spec->list.list[i];

		for (uint32_t j = 0; j < ele_bin->n_repeats; i++, j++) {
			as_val* val = bin_spec_random_val(ele_bin, random);

			if (val) {
				as_list_append((as_list*) list, val);
			}
			else {
				as_list_destroy((as_list*) list);
				return NULL;
			}
		}
	}

	return (as_val*) list;
}


static as_val* _gen_random_map(const struct bin_spec* bin_spec,
		as_random* random)
{
	as_hashmap* map;

	uint32_t n_entries = bin_spec_map_n_entries(bin_spec);

	map = as_hashmap_new(2 * n_entries);

	for (uint32_t i = 0; i < n_entries; i++) {
		as_val* key = bin_spec_random_val(bin_spec_get_key(bin_spec), random);
		as_val* val = bin_spec_random_val(bin_spec->map.val, random);

		as_hashmap_set(map, key, val);
	}

	return (as_val*) map;
}


static as_val* bin_spec_random_val(const struct bin_spec* bin_spec,
		as_random* random)
{
	as_val* val;
	switch (bin_spec_get_type(bin_spec)) {
		case BIN_SPEC_TYPE_INT:
			val = _gen_random_int(bin_spec->integer.range, random);
			break;
		case BIN_SPEC_TYPE_STR:
			val = _gen_random_str(bin_spec->string.length, random);
			break;
		case BIN_SPEC_TYPE_BYTES:
			val = _gen_random_bytes(bin_spec->bytes.length, random);
			break;
		case BIN_SPEC_TYPE_DOUBLE:
			val = _gen_random_double(random);
			break;
		case BIN_SPEC_TYPE_LIST:
			val = _gen_random_list(bin_spec, random);
			break;
		case BIN_SPEC_TYPE_MAP:
			val = _gen_random_map(bin_spec, random);
			break;
		default:
			fprintf(stderr, "Unknown bin_spec type (%d)\n",
					bin_spec_get_type(bin_spec));
			val = NULL;
	}

	return val;
}


int obj_spec_populate_bins(const struct obj_spec* obj_spec, as_record* rec,
		as_random* random, const char* bin_name)
{
	uint32_t n_bin_specs = obj_spec->n_bin_specs;
	as_bins* bins = &rec->bins;

	if (n_bin_specs > bins->capacity) {
		fprintf(stderr, "Not enough bins allocated for obj_spec\n");
		return -1;
	}

	for (uint32_t i = 0, cnt = 0; cnt < n_bin_specs; i++) {
		const struct bin_spec* bin_spec = &obj_spec->bin_specs[i];

		for (uint32_t j = 0; j < bin_spec->n_repeats; j++, cnt++) {
			as_val* val = bin_spec_random_val(bin_spec, random);

			if (val == NULL) {
				return -1;
			}

			as_bin_name name;
			if (cnt == 0) {
				strncpy(name, bin_name, sizeof(name));
			}
			else {
				snprintf(name, sizeof(name), "%s_%d", bin_name, cnt + 1);
			}
			if (!as_record_set(rec, name, (as_bin_value*) val)) {
				// failed to set a record, meaning we ran out of space
				fprintf(stderr, "Not enough free bin slots in record\n");
				return -1;
			}
		}
	}
	return 0;
}


#ifdef _TEST


#define sprint(out_str, str_size, ...) \
	do { \
		size_t __w = snprintf(*(out_str), str_size, __VA_ARGS__); \
		*(out_str) += (str_size > __w ? __w : str_size); \
		str_size = (str_size > __w ? str_size - __w : 0); \
	} while (0)

static size_t _sprint_bin(const struct bin_spec* bin, char** out_str,
		size_t str_size)
{
	if (bin->n_repeats != 1) {
		sprint(out_str, str_size, "%d*", bin->n_repeats);
	}
	switch (bin_spec_get_type(bin)) {
		case BIN_SPEC_TYPE_INT:
			sprint(out_str, str_size, "I%d", bin->integer.range + 1);
			break;
		case BIN_SPEC_TYPE_STR:
			sprint(out_str, str_size, "S%u", bin->string.length);
			break;
		case BIN_SPEC_TYPE_BYTES:
			sprint(out_str, str_size, "B%u", bin->bytes.length);
			break;
		case BIN_SPEC_TYPE_DOUBLE:
			sprint(out_str, str_size, "D");
			break;
		case BIN_SPEC_TYPE_LIST:
			sprint(out_str, str_size, "[");
			for (uint32_t i = 0, cnt = 0; cnt < bin->list.length; i++) {
				str_size = _sprint_bin(&bin->list.list[i], out_str, str_size);
				cnt += bin->list.list[i].n_repeats;
				if (cnt != bin->list.length) {
					sprint(out_str, str_size, ",");
				}
			}
			sprint(out_str, str_size, "]");
			break;
		case BIN_SPEC_TYPE_MAP:
			sprint(out_str, str_size, "{");
			str_size = _sprint_bin(bin_spec_get_key(bin), out_str, str_size);
			sprint(out_str, str_size, ":");
			str_size = _sprint_bin(bin->map.val, out_str, str_size);
			sprint(out_str, str_size, "}");
			break;
	}
	return str_size;
}

void _dbg_sprint_obj_spec(const struct obj_spec* obj_spec, char* out_str,
		size_t str_size)
{
	for (uint32_t i = 0, cnt = 0; cnt < obj_spec->n_bin_specs; i++) {
		str_size = _sprint_bin(&obj_spec->bin_specs[i], &out_str, str_size);

		cnt += obj_spec->bin_specs[i].n_repeats;

		if (cnt != obj_spec->n_bin_specs && str_size > 0) {
			sprint(&out_str, str_size, ",");
		}
	}
}

#endif /* _TEST */

