#ifndef POOL_H_INCLUDED
#define POOL_H_INCLUDED

struct slot_struct {
	// always lock the pool mutex
	// when reading OR writing the status
	int status;
	// once you flag the slot as STATUS_BUSY,
	// unlock the mutex and you are
	// free to mess with this stuff below
	char* name;
	time_t load;
	lua_State* state;
} typedef slot_t;

struct pool_struct {
	int count;
	pthread_mutex_t mutex;
	slot_t* slot;
} typedef pool_t;

pool_t* pool_open(int count);
void pool_close(pool_t* pool);

void slot_load(slot_t *p, lua_State* L, char* name);
void slot_flush(slot_t* p);

#endif // POOL_H_INCLUDED
