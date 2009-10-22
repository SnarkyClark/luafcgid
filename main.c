/*
 * threaded.c -- A simple multi-threaded FastCGI application.
 */

#ifndef lint
static const char rcsid[] = "$Id: threaded.c,v 1.9 2001/11/20 03:23:21 robs Exp $";
#endif /* not lint */

#include <fcgi_config.h>

#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <fcgiapp.h>

#ifdef _WIN32
#include <windows.h>
#define usleep(msec) Sleep(msec)
#define LISTEN_PATH ":9000"
#else
#define LISTEN_PATH "/var/tmp/luafcgid.socket"
#endif

#define WORKER_COUNT 3
#define VM_COUNT 5

#define CHATTER

const char hello_world[] =
    "s = 'Hello World'\n"
    "function handler()\n"
    "   return s\n"
    "end\n";

struct vm_pool_struct {
	int status;
	char* name;
	lua_State* state;
} typedef vm_pool_t;

struct params_struct {
	int pid;
	int tid;
	int sock;
	vm_pool_t** pool;
} typedef params_t;

static void *worker(void *a) {
    int rc, i;
    char errtype[256];
    char errmsg[1024];
	params_t* params = (params_t*)a;
	vm_pool_t** pool = params->pool;
    FCGX_Request request;
    char* script = NULL;
	lua_State* L = NULL;

#ifdef CHATTER
    fprintf(stderr, "[%d] starting\n", params->tid);
    fflush(stderr);
#endif

    FCGX_InitRequest(&request, params->sock, 0);

    for (;;) {
        static pthread_mutex_t accept_mutex = PTHREAD_MUTEX_INITIALIZER;
        static pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;

        // use accept() serialization
        pthread_mutex_lock(&accept_mutex);
        rc = FCGX_Accept_r(&request);
        pthread_mutex_unlock(&accept_mutex);

        if (rc < 0) break;

#ifdef CHATTER
        fprintf(stderr, "\t[%d] accepting connection\n", params->tid);
        fflush(stderr);
#endif

		script = FCGX_GetParam("SCRIPT_FILENAME", request.envp);

		// default error
		rc = LUA_ERRFILE;
		sprintf(errmsg, "Unable to locate script '%s'", script);

		// search for script
        pthread_mutex_lock(&pool_mutex);
		for (i = 0; i < VM_COUNT; i++) {
			if (pool[i]->state && !(pool[i]->status)) {
				if ((!script && !pool[i]->name) ||
                    ((script && pool[i]->name) &&
                    (strcmp(script, pool[i]->name) == 0))) {
					// lock it
					pool[i]->status = 1;
					break;
                }
			}
		}
		pthread_mutex_unlock(&pool_mutex);

		if (i < VM_COUNT) {
			// found a matching state
			L = pool[i]->state;
			rc = 0;
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

            // load and run chunk
            rc = luaL_loadbuffer(L, hello_world, strlen(hello_world), "hello_world");
            if (rc == 0) rc = lua_pcall(L, 0, 0, 0);

			if (rc == 0) {
			    // pick which part of the pool to use
                pthread_mutex_lock(&pool_mutex);
                // is there a free spot?
                for (i = 0; i < VM_COUNT; i++) {
                    if (!pool[i]->status && !pool[i]->state) {
                        pool[i]->status = 1;
                        break;
                    }
                }
                if (i == VM_COUNT) {
                    // time to kick someone out of the pool :(
                    // TODO: find a better way to pick a loser
                    do {
                        // search for an inactive state
                        for (i = 0; i < VM_COUNT; i++) {
                            if (!pool[i]->status) {
                                // found one, so lock it for ourselves
                                pool[i]->status = 1;
                                break;
                            }
                        }
                        if (i == VM_COUNT) {
                            // the pool is full & everyone is busy!
                            // unlock the pool for a bit so someone else can flag out
                            pthread_mutex_unlock(&pool_mutex);
                            usleep((int)((rand() % 3) + 1));
                            pthread_mutex_lock(&pool_mutex);
                        }
                    } while (i == VM_COUNT);
    #ifdef CHATTER
                    fprintf(stderr, "\t[%d] kicked [%d] out of the pool\n", params->tid, i);
                    fflush(stderr);
    #endif
                }
                // 'i' should now point to a slot that is locked and free to use
                pthread_mutex_unlock(&pool_mutex);
                // shut it down if needed
                if (pool[i]->state) lua_close(pool[i]->state);
                if (pool[i]->name) free(pool[i]->name);
                // toss the Lua state into the pool
                if (script) {
                    pool[i]->name = (char*)malloc(strlen(script) + 1);
                    strcpy(pool[i]->name, script);
                } else {
                    pool[i]->name = NULL;
                }
                pool[i]->state = L;
    #ifdef CHATTER
                fprintf(stderr, "\t[%d] loaded '%s' into [%d]\n", params->tid, script, i);
                fflush(stderr);
    #endif
            } else {
                i = VM_COUNT;
            }
		}

        if (rc == 0) {
        	// we have a valid VM state, time to roll!
            lua_getglobal(L, "handler");
            if (lua_isfunction(L, -1)) {
            	// call script handler
                rc = lua_pcall(L, 0, 1, 0);
                // check for valid return
                if (!lua_isstring(L, -1)) {
                    lua_pop(L, 1);
                    rc = LUA_ERRRUN;
                    lua_pushstring(L, "handler() must return string");
                }
            } else {
                rc = LUA_ERRRUN;
				lua_pushstring(L, "handler() function not found");
            }
        }

		switch (rc) {
			case 0:
				errmsg[0] = '\0';
				break;
			case LUA_ERRFILE:
				strcpy(errtype, "File Error");
				break;
			case LUA_ERRRUN:
				strcpy(errtype, "Runtime Error");
               	strcpy(errmsg, lua_tostring(L, -1));
                lua_pop(L, 1);
				break;
			case LUA_ERRSYNTAX:
				strcpy(errtype, "Syntax Error");
               	strcpy(errmsg, lua_tostring(L, -1));
                lua_pop(L, 1);
				break;
			case LUA_ERRMEM:
				strcpy(errtype, "Memory Error");
				break;
			default:
				strcpy(errtype, "Unknown Error");
		};

        if (rc == 0) {
            FCGX_FPrintF(request.out,
                "Content-type: text/html\r\n"
                "X-FastCGI-ID: %ld-%d\r\n"
                "\r\n<html>\n"
                "<title>Test App</title>\n"
                "<body><h2>Success</h2></body>\n"
                "<p>%s</p>\n"
                "</html>", params->pid, params->tid, lua_tostring(L, -1));
                lua_pop(L, 1);
        } else {
            FCGX_FPrintF(request.out,
                "Content-type: text/html\r\n"
                "X-FastCGI-ID: %ld-%d\r\n"
                "\r\n<html>\n"
                "<title>Application Error</title>\n"
                "<body><h2>%s</h2></body>\n"
                "<p>%s</p>\n"
                "</html>", errtype, params->pid, params->tid, errmsg);
        }

        FCGX_Finish_r(&request);

        if (i < VM_COUNT) {
            // we are done with the slot, so we shall flag out
            pthread_mutex_lock(&pool_mutex);
            pool[i]->status = 0;
            pthread_mutex_unlock(&pool_mutex);
        }

        // avoid harmonics
        usleep((int)((rand() % 3) + 1));

    }

    return NULL;

}

int main(int arc, char** argv) {

    int i, sock;
    pid_t pid = getpid();
    pthread_t id[WORKER_COUNT];
	params_t** params = NULL;
	vm_pool_t** pool = NULL;

	// alloc

	// alloc VM pool
	pool = (vm_pool_t**)malloc(sizeof(vm_pool_t*) * VM_COUNT);
	for (i = 0; i < VM_COUNT; i++) {
		pool[i] = (vm_pool_t*)malloc(sizeof(vm_pool_t));
		memset(pool[i], 0, sizeof(vm_pool_t));
	}

    FCGX_Init();

	sock = FCGX_OpenSocket(LISTEN_PATH, 100);
    if (!sock) {
        fprintf(stderr, "\tunable to create accept socket!\n");
        fflush(stderr);
        return 1;
    }

    params = (params_t**)malloc(sizeof(params_t*) * WORKER_COUNT);
    for (i = 0; i < WORKER_COUNT; i++) {
		// initialize worker params
		params[i] = (params_t*)malloc(sizeof(params_t));
		params[i]->pid = pid;
		params[i]->tid = i;
		params[i]->sock = sock;
		params[i]->pool = pool;
		// create worker thread
        pthread_create(&id[i], NULL, worker, (void*)params[i]);
        usleep(10);
    }

    for (;;) {
		// TODO: housekeeping
    }

	for (i = 0; i < WORKER_COUNT; i++) {
            free(params[i]);
    }
    free(params);

	// dealloc VM pool
	for (i = 0; i < VM_COUNT; i++) {
		if (pool[i]->state) lua_close(pool[i]->state);
		if (pool[i]->name) free(pool[i]->name);
	}
	free(pool);

	return 0;
}

