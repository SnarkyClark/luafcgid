#ifndef POOL_H_INCLUDED
#define POOL_H_INCLUDED

struct slot_struct {
	/* always lock the pool mutex
	   when reading OR writing the status */
	volatile int status;
	/* once you flag the slot with the STATUS_BUSY,
	   unlock the mutex and you are
	   free to mess with this stuff below */
	char* name; /* script filename */
	time_t load; /* timestamp loaded */
	time_t access; /* timestamp accessed */
	unsigned int count; /* number of requests served */
	pthread_t tid; /* thread using the state */
	lua_State* state;
} typedef slot_t;

struct pool_struct {
	int count;
	pthread_mutex_t mutex;
	slot_t* slot;
} typedef pool_t;

#define pool_lock(p) pthread_mutex_lock(&p->mutex)
#define pool_unlock(p) pthread_mutex_unlock(&p->mutex)
#define pool_slot(p, i) &p->slot[i]

pool_t* pool_open(int count);
void pool_close(pool_t* pool);
int pool_scan_idle(pool_t* pool, char* name);
int pool_scan_free(pool_t* pool);
void pool_load(pool_t* pool, int index, lua_State* L, char* name);
void pool_flush(pool_t* pool, int index);

#endif // POOL_H_INCLUDED
