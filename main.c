/*
 * luafcgid -- A simple multi-threaded Lua+FastCGI daemon.
 *
 * this code is provided under the "Simplified BSD License"
 * (c) STPeters 2009
 */


#include "main.h"

#define ERR_SIZE 1024

const char* env_var[] = {
    // standard CGI environment variables as per CGI Specification 1.1
    "SERVER_SOFTWARE",
    "SERVER_NAME",
    "GATEWAY_INTERFACE",
    "SERVER_PROTOCOL",
    "SERVER_PORT",
    "REQUEST_METHOD",
    "PATH_INFO",
    "PATH_TRANSLATED",
    "SCRIPT_NAME",
    "QUERY_STRING",
    "REMOTE_HOST",
    "REMOTE_ADDR",
    "AUTH_TYPE",
    "REMOTE_USER",
    "REMOTE_IDENT",
    "CONTENT_TYPE",
    "CONTENT_LENGTH", // WARNING! do NOT rely on this value
    // common client variables
    "HTTP_ACCEPT",
    "HTTP_USER_AGENT",
    // not spec, but required for this to work
    "SCRIPT_FILENAME",
    NULL,
};

// utility functions

BOOL luaL_getglobal_int(lua_State* L, const char* name, int* v) {
	lua_getglobal(L, name);
	if (lua_isnumber(L, -1)) {
        *v = lua_tointeger(L, -1);
        lua_pop(L, 1);
        return TRUE;
	}
    lua_pop(L, 1);
	return FALSE;
}

BOOL luaL_getglobal_str(lua_State* L, const char* name, char** v) {
	const char* r = NULL;
	size_t l = 0;
	lua_getglobal(L, name);
	if (lua_isstring(L, -1)) {
        r = lua_tolstring(L, -1, &l);
        if (r && l) {
        	if (*v) free(*v);
        	*v = (char*)malloc(l + 1);
        	strncpy(*v, r, l);
        	(*v)[l] = '\0';
			lua_pop(L, 1);
        	return TRUE;
        }
	}
    lua_pop(L, 1);
	return FALSE;
}

void luaL_pushcgienv(lua_State* L, request_t* r) {
    int i = 0;
    char* v = NULL;
    lua_newtable(L);
    lua_pushinteger(L, LUAFCGID_VERSION);
    lua_setfield(L, -2, "LUAFCGID_VERSION");
    while(env_var[i]) {
        v = FCGX_GetParam(env_var[i], r->fcgi.envp);
        if (v) {
            lua_pushstring(L, env_var[i]);
            lua_pushstring(L, v);
            lua_settable(L, -3);
        }
        i++;
    }
}

void luaL_pushcgicontent(lua_State* L, request_t* r) {
    char* buf = NULL;
    int size = r->conf->maxpost;
    size_t len = size;
    char* slen = FCGX_GetParam("CONTENT_LENGTH", r->fcgi.envp);

    // check if content length is provided
    if (slen) len = atoi(slen);
    if (len > size) len = size;
    // alloc and zero buffer
    buf = (char*)malloc(len);
    memset(buf, 0, len);
    // load and push buffer
    FCGX_GetStr(buf, len, r->fcgi.in);
    lua_pushlstring(L, buf, len);
    free(buf);
}

char* script_load(const char* fn, struct stat* fs) {
	FILE* fp = NULL;
	char* fbuf = NULL;

	// does it even exist?
	if (stat(fn, fs) == STATUS_OK) {
		// is it a file of more then 0 bytes?
		if (S_ISREG(fs->st_mode) && fs->st_size) {
			fp = fopen(fn, "rb");
			if (fp) {
				fbuf = (char*)malloc(fs->st_size);
				memset(fbuf, 0, fs->st_size);
				fread(fbuf, fs->st_size, 1, fp);
				fclose(fp);
			}
		}
	}

	return fbuf;
}

// worker and parent threads

static void *worker(void *a) {
	// shared vars
	static pthread_mutex_t accept_mutex = PTHREAD_MUTEX_INITIALIZER;
	// local private vars
    int rc, i, j, k;
    char errtype[ERR_STR_SIZE + 1];
    char errmsg[ERR_SIZE + 1];
	params_t* params = (params_t*)a;
	pool_t* pool = params->pool;
	slot_t* slot = NULL;
    request_t req;
    char* script = NULL;
    char* split = NULL;
	lua_State* L = NULL;
    struct stat fs;
	char* fbuf = NULL;

	#ifdef CHATTER
    fprintf(stderr, "[%d] starting\n", params->tid);
    fflush(stderr);
	#endif

    FCGX_InitRequest(&req.fcgi, params->sock, 0);
    req.conf = params->conf;

    for (;;) {

        // use accept() serialization
        pthread_mutex_lock(&accept_mutex);
        rc = FCGX_Accept_r(&req.fcgi);
        pthread_mutex_unlock(&accept_mutex);

        if (rc < 0) break;

		#ifdef CHATTER
		fprintf(stderr, "[%d] accepting connection\n", params->tid);
		fflush(stderr);
		#endif

		// init loop locals
		script = FCGX_GetParam("SCRIPT_FILENAME", req.fcgi.envp);
		rc = STATUS_404;
		errmsg[0] = '\0';
		slot = NULL;

		if (script) {
			#ifdef _WIN32
			// normalize path seperator
			j = strlen(script);
			for (i = 0; i < j; i++) {
				if (script[i] == '/') script[i] = SEP;
			}
			#endif
			// isolate the path in the script filename,
			// and change the cwd to it
			split = strrchr(script, SEP);
			if (split) {
				*split = '\0';
				chdir(script);
				*split = SEP;
			}
		}

		// search for script
        j = pool->count;
        k = 0;
		do {
			// give someone else a chance to flag out
			if (k) usleep(1);
			pthread_mutex_lock(&pool->mutex);
			for (i = 0; i < j; i++) {
				slot = &pool->slot[i];
				// is the slot available and loaded?
				if (!slot->status && slot->state) {
					// do the names match?
					if ((!script && !slot->name) ||
							((script && slot->name) &&
							(strcmp(script, slot->name) == 0))) {
						// lock it
						slot->status = STATUS_BUSY;
						#ifdef CHATTER
						fprintf(stderr, "\t[%d] found and locked [%d]\n", params->tid, i);
						#endif
						break;
					}
				}
			}
			pthread_mutex_unlock(&pool->mutex);
		} while ((i == j) && (++k < req.conf->retries));

		if (i < j) {
			// found a matching state
			L = slot->state;
			rc = STATUS_OK;
		} else {
			// make a new state
			L = lua_open();
			if (!L) {
				fprintf(stderr, "[%d] unable to init lua!\n", params->tid);
				fflush(stderr);
				return NULL;
			}
			luaL_openlibs(L);
			luaL_loadrequest(L);

			fbuf = script_load(script, &fs);

            if (fbuf) {
       	        // TODO: run state startup hook
				// load and run buffer
				rc = luaL_loadbuffer(L, fbuf, fs.st_size, script);
				if (rc == STATUS_OK) rc = lua_pcall(L, 0, 0, 0);
				// cleanup
				free(fbuf);
            }

			if (rc == STATUS_OK) {
			    // pick which part of the pool to use
                pthread_mutex_lock(&pool->mutex);
                // is there a free spot?
                for (i = 0; i < j; i++) {
                	slot = &pool->slot[i];
                    if (!slot->status && !slot->state) {
                        slot->status = STATUS_BUSY;
                        #ifdef CHATTER
                        fprintf(stderr, "\t[%d] locked free [%d]\n", params->tid, i);
                        #endif
                        break;
                    }
                }
                if (i == j) {
                    // time to kick someone out of the pool :(
                    // TODO: find a better way to pick a loser
                    do {
                        // search for an inactive state
                        for (i = 0; i < j; i++) {
                        	slot = &pool->slot[i];
                            if (!slot->status) {
                                // found one, so lock it for ourselves
                                slot->status = STATUS_BUSY;
								#ifdef CHATTER
								fprintf(stderr, "\t[%d] locked inactive [%d]\n", params->tid, i);
								#endif
                                break;
                            }
                        }
                        if (i == j) {
                            // the pool is full & everyone is busy!
                            // unlock the pool for a bit so someone else can flag out
                            pthread_mutex_unlock(&pool->mutex);
                            usleep((int)((rand() % 3) + 1));
                            pthread_mutex_lock(&pool->mutex);
                        }
                    } while (i == j);
					#ifdef CHATTER
                    fprintf(stderr, "\t[%d] kicked [%d] out of the pool\n", params->tid, i);
                    fflush(stderr);
					#endif
                }
                // 'slot' and 'i' should now reference a slot that is locked and free to use
                pthread_mutex_unlock(&pool->mutex);
                // scrub it clean and load it up
                slot_flush(slot);
                slot_load(slot, L, script);
				#ifdef CHATTER
                fprintf(stderr, "\t[%d] loaded '%s' into [%d]\n", params->tid, script, i);
                fflush(stderr);
				#endif
            } else {
                i = j;
            }
		}

        if (rc == STATUS_OK) {
        	// we have a valid VM state, time to roll!
            lua_getglobal(L, "main");
            if (lua_isfunction(L, -1)) {
                // prepare request object
                req.headers_sent = FALSE;
                // prepare main() args
                luaL_pushcgienv(L, &req);
                luaL_pushrequest(L, &req);
            	// call script handler
                rc = lua_pcall(L, 2, 0, 0);
            } else {
                rc = LUA_ERRRUN;
				lua_pushstring(L, "main() function not found");
            }
        }

		// translate for the puny humans
		switch (rc) {
			case STATUS_OK:
				break;
			case LUA_ERRFILE:
				strncpy(errtype, LUA_ERRFILE_STR, ERR_STR_SIZE);
               	errtype[ERR_STR_SIZE] = '\0';
				fprintf(stderr, "[%d] %s\n", params->tid, errtype);
				break;
			case LUA_ERRRUN:
				strncpy(errtype, LUA_ERRRUN_STR, ERR_STR_SIZE);
               	errtype[ERR_STR_SIZE] = '\0';
				fprintf(stderr, "[%d] %s: %s\n", params->tid, errtype, lua_tostring(L, -1));
               	strncpy(errmsg, lua_tostring(L, -1), ERR_SIZE);
               	errtype[ERR_STR_SIZE] = '\0';
                lua_pop(L, 1);
				break;
			case LUA_ERRSYNTAX:
				strncpy(errtype, LUA_ERRSYNTAX_STR, ERR_STR_SIZE);
               	errtype[ERR_STR_SIZE] = '\0';
				fprintf(stderr, "[%d] %s: %s\n", params->tid, errtype, lua_tostring(L, -1));
               	strncpy(errmsg, lua_tostring(L, -1), ERR_SIZE);
               	errmsg[ERR_SIZE] = '\0';
                lua_pop(L, 1);
				break;
			case LUA_ERRMEM:
				strncpy(errtype, LUA_ERRMEM_STR, ERR_STR_SIZE);
               	errtype[ERR_STR_SIZE] = '\0';
				fprintf(stderr, "[%d] %s\n", params->tid, errtype);
				break;
			default:
				strncpy(errtype, ERRUNKNOWN_STR, ERR_STR_SIZE);
               	errtype[ERR_STR_SIZE] = '\0';
				fprintf(stderr, "[%d] %s\n", params->tid, errtype);
				break;
		};

        // send the data out the tubes
        if (rc == STATUS_OK) {
            if(!req.headers_sent) {
                FCGX_FPrintF(req.fcgi.out,
                    "Status: 200 OK\r\n"
                    "Content-Type: text/html\r\n\r\n"
                );
            }
        } else if (rc == STATUS_404) {
			HTTP_404(req.fcgi.out, script);
        } else {
            HTTP_500(req.fcgi.out, errtype, errmsg);
        }

        FCGX_Finish_r(&req.fcgi);

        if (i < j) {
            // we are done with the slot, so we shall flag out
            pthread_mutex_lock(&pool->mutex);
            slot->status = STATUS_OK;
            pthread_mutex_unlock(&pool->mutex);
        }

		#ifdef CHATTER
		fprintf(stderr, "[%d] done with request\n", params->tid);
		#endif

        // cooldown
        usleep((int)((rand() % 3) + 1));

    }

    return NULL;

}

int main(int arc, char** argv) {

    int i, j, sock;
    pid_t pid = getpid();
    pthread_t* id = NULL;
	params_t* params = NULL;
	pool_t* pool = NULL;
	slot_t* slot = NULL;
    struct stat fs;
    config_t* conf = NULL;

	if (arc > 1 && argv[1]) {
		conf = config_load(argv[1]);
	} else {
		conf = config_load("config.lua");
	}

    FCGX_Init();

	sock = FCGX_OpenSocket(conf->listen, 100);
    if (!sock) {
        fprintf(stderr, "\tunable to create accept socket!\n");
        fflush(stderr);
        return 1;
    }

	pool = pool_open(conf->states);

    // alloc worker data
    j = conf->workers;
	id = (pthread_t*)malloc(sizeof(pthread_t) * j);
    params = (params_t*)malloc(sizeof(params_t) * j);

    for (i = 0; i < j; i++) {
		// initialize worker params
		params[i].pid = pid;
		params[i].tid = i;
		params[i].sock = sock;
		params[i].pool = pool;
		params[i].conf = conf;
		// create worker thread
        pthread_create(&id[i], NULL, worker, (void*)&params[i]);
        usleep(10);
    }

    for (;;) {
    	// chill till the next sweep
		usleep(conf->sweep);
		// housekeeping
        pthread_mutex_lock(&pool->mutex);
		for (i = 0; i < pool->count; i++) {
			slot = &pool->slot[i];
			// check for stale moon chips
			if (!slot->status && slot->state && slot->name) {
				if ((stat(slot->name, &fs) == STATUS_OK) &&
						(fs.st_mtime > slot->load)) {
					slot_flush(slot);
					#ifdef CHATTER
					fprintf(stderr, "[%d] has gone stale\n", i);
					fflush(stderr);
					#endif
				}
			}
		}
        pthread_mutex_unlock(&pool->mutex);
        // TODO: run housekeeping hook
    }

    free(params);
	free(id);

	pool_close(pool);

    // dealloc config
    if (conf->listen) free(conf->listen);
    for (i = 0; i < HOOK_COUNT; i++) {
        if (conf->hook[i]) {
            for (j = 0; j < conf->hook[i]->count; j++) {
                if (conf->hook[i]->chunk[j]) free(conf->hook[i]->chunk[j]);
            }
            free(conf->hook[i]);
        }
    }
    free(conf->hook);
    free(conf);

	return 0;
}
