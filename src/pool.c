#include "main.h"

pool_t* pool_open(int count) {
	pool_t* pool = NULL;
	if (count) {
		/* alloc pool */
		pool = (pool_t*)malloc(sizeof(pool_t));
		memset(pool, 0, sizeof(pool_t));
		assert(pool);
		pool->slot = (slot_t*)malloc(sizeof(slot_t) * count);
		assert(pool->slot);
		memset(pool->slot, 0, sizeof(slot_t) * count);
		/* init pool */
		pool->count = count;
		pool->mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	}
	return pool;
}

void pool_close(pool_t* pool) {
	int i = 0;
    if (pool) {
		// dealloc pool
    	pool_lock(pool);
		for (i = 0; i < pool->count; i++) pool_flush(pool, i);
    	pool_unlock(pool);
		if (pool->slot) free(pool->slot);
		free(pool);
    }
}

/*
 * scan pool for matching idle slot
 * locks slot and returns index if found,
 * (-1 - clones located) if not found
 */
int pool_scan_idle(pool_t* pool, char* name) {
	int i;
	int found = -1;
	int clones = 0;
	slot_t* slot = NULL;
	pool_lock(pool);
	for (i = 0; i < pool->count; i++) {
		slot = pool_slot(pool, i);
		/* do the names match and is it a valid state? */
		if (((!name && !slot->name) || ((name && slot->name)
				&& (strcmp(name, slot->name) == 0))) && slot->state) {
			#ifdef CHATTER
			logit("\t\tfound script '%s' in state [%d]", name, i);
			#endif
			/* count the clones */
			clones++;
			/* is the slot available? */
			if (!slot->status) {
				/* lock it */
				slot->access = time(NULL);
				slot->count++;
				slot->status = STATUS_BUSY;
				found = i;
				break;
			}
		}
	}
	pool_unlock(pool);
	if (found > -1) {
		return found;
	} else {
		return found - clones;
	}
}

/*
 * scan pool for free slot
 * if no free slots then flush quietest one,
 * locks slot and returns index if found,
 * -1 if not found
 */
int pool_scan_free(pool_t* pool) {
	int i;
	int found = -1;
	int access = 0;
	slot_t* slot = NULL;
	pool_lock(pool);
	for (i = 0; i < pool->count; i++) {
		slot = pool_slot(pool, i);
		if (!slot->status) {
			if (!slot->state) {
				found = i;
				break;
			} else {
				if ((access == 0) || (slot->access < access)) {
					access = slot->access;
					found = i;
				}
			}
		}
	}
	if (found >= 0) {
		/* no free slots found, flush the quietest one */
		if (i == pool->count) pool_flush(pool, found);
		/* lock it up */
		(pool_slot(pool, found))->status = STATUS_BUSY;
	}
	pool_unlock(pool);
	return found;
}

/*
 * load a Lua state into the pool at slot[index]
 */
void pool_load(pool_t* pool, int index, lua_State* L, char* name) {
	slot_t* slot = pool_slot(pool, index);
	/* toss the Lua state into the pool slot */
	slot->state = L;
	/* slap a label on it */
	if (name) {
		slot->name = (char*)malloc(strlen(name) + 1);
		strcpy(slot->name, name);
	}
	/* timestamp for aging */
	slot->load = time(NULL);
	slot->access = slot->load;
}

/*
 * flush the Lua state out of the pool at slot[index]
 */
void pool_flush(pool_t* pool, int index) {
#ifdef CHATTER
	logit("flushing slot [%d]", index);
#endif
	slot_t* slot = pool_slot(pool, index);
	/* shut it down */
	if(slot->state) {
		lua_close(slot->state);
        // TODO: run state shutdown hook
        slot->state = NULL;
	}
	/* sweep it up */
	if(slot->name) free(slot->name);
	slot->name = NULL;
	slot->access = 0;
	slot->load = 0;
	slot->count = 0;
}
