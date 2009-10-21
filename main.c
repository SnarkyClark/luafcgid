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

#define WORKER_COUNT 10
#define VM_COUNT 10

#define CHATTER

static lua_State* L;
static int loaded = 0;

const char* local_script[] = {
    "s = 'Hello World'\n"
    "function handler()\n"
    "   return s\n"
    "end\n",
    "s = 'Hello World 2'\n"
    "function handler()\n"
    "   return s\n"
    "end\n"
};

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
    char* script;
	lua_State* L;

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
			if (!(pool[i]->status) &&
				strcmp(script, pool[i]->name) == 0) {
					// lock it
					pool[i]->status = 1;
					break;
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
/*
            // load and run chunk
            rc = luaL_loadbuffer(L, hello_world, strlen(hello_world), "hello_world");
            if (rc == 0) rc = lua_pcall(L, 0, 0, 0);
            if (rc == 0) loaded = 1;
*/
			// pick which part of the pool to use
			pthread_mutex_lock(&pool_mutex);
			// is there a free spot?
			for (i = 0; i < VM_COUNT; i++) {
				if (!pool[i]->state) break;
			}
			if (i == VM_COUNT) {
				// time to kick someone out of the pool :(
				// TODO: find a better way to pick a loser
				i = (int)(rand() % VM_COUNT);
				// shut it down
				lua_close(pool[i]->state);
				free(pool[i]->name);
#ifdef CHATTER
				fprintf(stderr, "\t[%d] kicked [%d] out of the pool\n", params->tid, i);
				fflush(stderr);
#endif
			}
			// toss the Lua state into the pool
			pool[i]->name = (char*)malloc(strlen(script) + 1);
			strcpy(pool[i]->name, script);
			pool[i]->state = L;
	        pthread_mutex_unlock(&pool_mutex);
#ifdef CHATTER
			fprintf(stderr, "\t[%d] loaded '%s' into [%d]\n", params->tid, script, i);
			fflush(stderr);
#endif
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

        // avoid harmonics
        usleep((int)((rand() % 3) + 1));

    }

    lua_close(L);
    return NULL;

}

int main(int arc, char** argv) {

    int i, sock;
    pid_t pid = getpid();
    pthread_t id[WORKER_COUNT];
	params_t** params;
	vm_pool_t** pool;

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

    for (i = 0; i < WORKER_COUNT; i++) {
		// initialize worker params
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

	// dealloc VM pool
	for (i = 0; i < VM_COUNT; i++) {
		if (pool[i]->state) {
			lua_close(pool[i]->state);
			free(pool[i]->name);
		}
	}
	free(pool);

	return 0;
}

