/*
 * luafcgid -- A simple multi-threaded Lua+FastCGI daemon.
 *
 * this code is provided under the "Simplified BSD License"
 * (c) STPeters 2009
 */

#include <fcgi_config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcgiapp.h>

#include "main.h"

#define CHATTER

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
    NULL,
};

static const struct luaL_Reg request_methods[] = {
    {"gets", req_gets},
    {"puts", req_puts},
    {NULL, NULL}
};

static pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;

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

request_t* luaL_checkrequest(lua_State* L, int i) {
    luaL_checkudata(L, i, "LuaFCGId.Request");
    request_t* r = (request_t*)lua_unboxpointer(L, i);
    return r;
}

void luaL_loadrequest(lua_State* L) {
    luaL_newmetatable(L, "LuaFCGId.Request");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_register(L, NULL, request_methods);
    lua_pop(L, 2);
}

void luaL_pushrequest(lua_State* L, request_t* r) {
    lua_boxpointer(L, r);
    luaL_getmetatable(L, "LuaFCGId.Request");
    lua_setmetatable(L, -2);
    //luaL_getmetatable(L, "FCGX.Request");
    //luaL_pushcgienv(L, r);
    //lua_setfield(L, -2, "env");
    //lua_pop(L, 1);
}

int req_puts(lua_State *L) {
    request_t* r = NULL;
    const char* s = NULL;
    size_t l = 0;
    if (lua_gettop(L) >= 2) {
        r = luaL_checkrequest(L, 1);
        luaL_checkstring(L, 2);
        s = lua_tolstring(L, 2, &l);
        if(!r->headers_sent) {
            // TODO: allow custom mime type
            FCGX_FPrintF(r->fcgi.out,
                "Status: 200 OK\r\n"
                "Content-Type: text/html\r\n\r\n"
            );
            r->headers_sent = TRUE;
        }
        FCGX_PutStr(s, l, r->fcgi.out);
    }
    return 0;
}

int req_gets(lua_State *L) {
    request_t* r = NULL;
    if (lua_gettop(L)) {
        r = luaL_checkrequest(L, 1);
        luaL_pushcgicontent(L, r);
    } else {
        lua_pushnil(L);
    }
    return 1;
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

config_t* config_load(const char* fn) {
	int rc;
	struct stat fs;
	char* fbuf = NULL;
	lua_State* L = NULL;

    config_t* cf = (config_t*)malloc(sizeof(config_t));
    memset(cf, 0, sizeof(config_t));

	// defaults
	cf->listen = (char*)malloc(strlen(LISTEN_PATH) + 1);
	strcpy(cf->listen, LISTEN_PATH);
    cf->workers = 3;
    cf->states = 5;
    cf->sweep = 1000;
    cf->retries = 1;
    cf->maxpost = 1024 * 1024;

    if (fn) fbuf = script_load(fn, &fs);
	if (fbuf) {
        // make a new state
        L = lua_open();
        if (!L) return NULL;
        luaL_openlibs(L);
		// load and run buffer
		rc = luaL_loadbuffer(L, fbuf, fs.st_size, fn);
		if (rc == STATUS_OK) rc = lua_pcall(L, 0, 0, 0);
		// cleanup
		free(fbuf);
		if (rc == STATUS_OK) {
			// transfer globals to config struct
			luaL_getglobal_str(L, "listen", &cf->listen);
			luaL_getglobal_int(L, "workers", &cf->workers);
			luaL_getglobal_int(L, "states", &cf->states);
			luaL_getglobal_int(L, "sweep", &cf->sweep);
			// TODO
		}
		lua_close(L);
	}
	return cf;
}

void pool_load(vm_pool_t *p, lua_State* L, char* name) {
	// toss the Lua state into the pool slot
	p->state = L;
	// slap a label on it
	if (name) {
		p->name = (char*)malloc(strlen(name) + 1);
		strcpy(p->name, name);
	}
	// timestamp for aging
	p->load = time(NULL);
}

void pool_flush(vm_pool_t* p) {
	// shut it down
	if(p->state) {
		lua_close(p->state);
        // TODO: run state shutdown hook
	}
	// sweep it up
	if(p->name) free(p->name);
	memset(p, 0, sizeof(vm_pool_t));
}

static void *worker(void *a) {
    int rc, i, j, k;
    char errtype[256];
    char errmsg[1024];
	params_t* params = (params_t*)a;
	vm_pool_t* pool = params->pool;
    request_t req;
    char* script = NULL;
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
        static pthread_mutex_t accept_mutex = PTHREAD_MUTEX_INITIALIZER;

        // use accept() serialization
        pthread_mutex_lock(&accept_mutex);
        rc = FCGX_Accept_r(&req.fcgi);
        pthread_mutex_unlock(&accept_mutex);

        if (rc < 0) break;

		#ifdef CHATTER
		//fprintf(stderr, "\t[%d] accepting connection\n", params->tid);
		//fflush(stderr);
		#endif

		script = FCGX_GetParam("SCRIPT_FILENAME", req.fcgi.envp);

		#ifdef SEP
		// normalize path seperator
		if (script) {
			j = strlen(script);
			for (i = 0; i < j; i++) {
				if (script[i] == '/') script[i] = SEP;
			}
		}
		#endif

		// default error
		rc = STATUS_404;
		errmsg[0] = '\0';

		// search for script
        j = req.conf->states;
        k = req.conf->retries + 1;
		do {
			pthread_mutex_lock(&pool_mutex);
			for (i = 0; i < j; i++) {
				if (pool[i].state && !pool[i].status) {
					if ((!script && !pool[i].name) ||
							((script && pool[i].name) &&
							(strcmp(script, pool[i].name) == 0))) {
						// lock it
						pool[i].status = STATUS_BUSY;
						break;
					}
				}
			}
			pthread_mutex_unlock(&pool_mutex);
		} while ((i == j) && (--k > 0));

		if (i < j) {
			// found a matching state
			L = pool[i].state;
			rc = STATUS_OK;
		} else {
			// make a new state
			L = lua_open();
			if (!L) {
				#ifdef CHATTER
				fprintf(stderr, "\tunable to init lua!\n");
				fflush(stderr);
				#endif
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
                pthread_mutex_lock(&pool_mutex);
                // is there a free spot?
                for (i = 0; i < j; i++) {
                    if (!pool[i].status && !pool[i].state) {
                        pool[i].status = STATUS_BUSY;
                        break;
                    }
                }
                if (i == j) {
                    // time to kick someone out of the pool :(
                    // TODO: find a better way to pick a loser
                    do {
                        // search for an inactive state
                        for (i = 0; i < j; i++) {
                            if (!pool[i].status) {
                                // found one, so lock it for ourselves
                                pool[i].status = STATUS_BUSY;
                                break;
                            }
                        }
                        if (i == j) {
                            // the pool is full & everyone is busy!
                            // unlock the pool for a bit so someone else can flag out
                            pthread_mutex_unlock(&pool_mutex);
                            usleep((int)((rand() % 3) + 1));
                            pthread_mutex_lock(&pool_mutex);
                        }
                    } while (i == j);
					#ifdef CHATTER
                    fprintf(stderr, "\t[%d] kicked [%d] out of the pool\n", params->tid, i);
                    fflush(stderr);
					#endif
                }
                // 'i' should now point to a slot that is locked and free to use
                pthread_mutex_unlock(&pool_mutex);
                // scrub it clean and load it up
                pool_flush(&pool[i]);
                pool_load(&pool[i], L, script);
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
				strcpy(errtype, LUA_ERRFILE_STR);
				break;
			case LUA_ERRRUN:
				strcpy(errtype, LUA_ERRRUN_STR);
               	strcpy(errmsg, lua_tostring(L, -1));
                lua_pop(L, 1);
				break;
			case LUA_ERRSYNTAX:
				strcpy(errtype, LUA_ERRSYNTAX_STR);
               	strcpy(errmsg, lua_tostring(L, -1));
                lua_pop(L, 1);
				break;
			case LUA_ERRMEM:
				strcpy(errtype, LUA_ERRMEM_STR);
				break;
			default:
				strcpy(errtype, ERRUNKNOWN_STR);
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
            pthread_mutex_lock(&pool_mutex);
            pool[i].status = STATUS_OK;
            pthread_mutex_unlock(&pool_mutex);
        }

        // avoid harmonics
        usleep((int)((rand() % 3) + 1));

    }

    return NULL;

}

int main(int arc, char** argv) {

    int i, j, sock;
    pid_t pid = getpid();
    pthread_t* id = NULL;
	params_t* params = NULL;
	vm_pool_t* pool = NULL;
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

	// alloc VM pool
	j = conf->states;
	pool = (vm_pool_t*)malloc(sizeof(vm_pool_t) * j);
	memset(pool, 0, sizeof(vm_pool_t) * j);

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
        pthread_mutex_lock(&pool_mutex);
		for (i = 0; i < conf->states; i++) {
			// check for stale moon chips
			if (pool[i].state && pool[i].name && !pool[i].status) {
				if ((stat(pool[i].name, &fs) == STATUS_OK) &&
						(fs.st_mtime > pool[i].load)) {
					pool_flush(&pool[i]);
					#ifdef CHATTER
					fprintf(stderr, "[%d] has gone stale\n", i);
					fflush(stderr);
					#endif
				}
			}
		}
        pthread_mutex_unlock(&pool_mutex);
        // TODO: run housekeeping hook
    }

    free(params);
	free(id);

	// dealloc VM pool
	j = conf->states;
    pthread_mutex_lock(&pool_mutex);
	for (i = 0; i < j; i++) pool_flush(&pool[i]);
	free(pool);
    pthread_mutex_unlock(&pool_mutex);

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

