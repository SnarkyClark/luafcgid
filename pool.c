#include "main.h"

pool_t* pool_open(int count) {
	pool_t* pool = NULL;
	if (count) {
		// alloc pool
		pool = (pool_t*)malloc(sizeof(pool_t));
		memset(pool, 0, sizeof(pool_t));
		assert(pool);
		pool->slot = (slot_t*)malloc(sizeof(slot_t) * count);
		assert(pool->slot);
		memset(pool->slot, 0, sizeof(slot_t) * count);
		// init pool
		pool->count = count;
		pool->mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	}
	return pool;
}

void pool_close(pool_t* pool) {
	int i = 0;
    if (pool) {
		// dealloc pool
    	pthread_mutex_lock(&pool->mutex);
		for (i = 0; i < pool->count; i++) slot_flush(&pool->slot[i]);
		pthread_mutex_unlock(&pool->mutex);
		if (pool->slot) free(pool->slot);
		free(pool);
    }
}

void slot_load(slot_t* slot, lua_State* L, char* name) {
	// toss the Lua state into the pool slot
	slot->state = L;
	// slap a label on it
	if (name) {
		slot->name = (char*)malloc(strlen(name) + 1);
		strcpy(slot->name, name);
	}
	// timestamp for aging
	slot->load = time(NULL);
}

void slot_flush(slot_t* slot) {
	// shut it down
	if(slot->state) {
		lua_close(slot->state);
        // TODO: run state shutdown hook
        slot->state = NULL;
	}
	// sweep it up
	if(slot->name) free(slot->name);
	slot->name = NULL;
}
