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
#endif

#define THREAD_COUNT 10

static lua_State* L;
static int loaded = 0;

const char hello_world[] =
    "s = 'Hello World'\n"
    "function handler()\n"
    "   return s\n"
    "end\n";

struct thread_params {
	int pid;
	int tid;
	int sock;
};

// static int counts[THREAD_COUNT];

static void *doit(void *a)
{
    int rc;
    struct thread_params* params = (struct thread_params*)a;
    FCGX_Request request;
    //char *server_name;

    fprintf(stderr, "starting worker %d-%d\n", params->pid, params->tid);
    fflush(stderr);

    FCGX_InitRequest(&request, params->sock, 0);

    for (;;) {
        static pthread_mutex_t accept_mutex = PTHREAD_MUTEX_INITIALIZER;
        static pthread_mutex_t lua_mutex = PTHREAD_MUTEX_INITIALIZER;

        /* Some platforms require accept() serialization, some don't.. */
        pthread_mutex_lock(&accept_mutex);
        rc = FCGX_Accept_r(&request);
        pthread_mutex_unlock(&accept_mutex);

        if (rc < 0) break;

        // server_name = FCGX_GetParam("SERVER_NAME", request.envp);

        //fprintf(stderr, "[%d] accepting connection\n", thread_id);
        //fflush(stderr);

        pthread_mutex_lock(&lua_mutex);
        if (!loaded) { // have we loaded the script yet?
            // load and run chunk
            fprintf(stderr, "loading script\n");
            fflush(stderr);
            rc = luaL_loadbuffer(L, hello_world, strlen(hello_world), "hello_world");
            if (rc == 0) rc = lua_pcall(L, 0, 0, 0);
            if (rc == 0) loaded = 1;
        } else {
            rc = 0;
        }

        if (rc == 0) {
            // push functions and arguments
            lua_getglobal(L, "handler");  // function to be called
            if (lua_isfunction(L, -1)) {
                rc = lua_pcall(L, 0, 1, 0); // call script handler
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
        } else if (rc == LUA_ERRRUN) {
            FCGX_FPrintF(request.out,
                "Content-type: text/html\r\n"
                "X-FastCGI-ID: %ld-%d\r\n"
                "\r\n<html>\n"
                "<title>Application Error</title>\n"
                "<body><h2>Runtime Error</h2></body>\n"
                "<p>%s</p>\n"
                "</html>", params->pid, params->tid, lua_tostring(L, -1));
                lua_pop(L, 1);
        } else if (rc == LUA_ERRSYNTAX) {
            FCGX_FPrintF(request.out,
                "Content-type: text/html\r\n"
                "X-FastCGI-ID: %ld-%d\r\n"
                "\r\n<html>\n"
                "<title>Application Error</title>\n"
                "<body><h2>Syntax Error</h2></body>\n"
                "<p>%s</p>\n"
                "</html>", params->pid, params->tid, lua_tostring(L, -1));
                lua_pop(L, 1);
        } else if (rc == LUA_ERRMEM) {
            FCGX_FPrintF(request.out,
                "Content-type: text/html\r\n"
                "X-FastCGI-ID: %ld-%d\r\n"
                "\r\n<html>\n"
                "<title>Application Error</title>\n"
                "<body><h2>Out Of Memory</h2></body>\n"
                "</html>", params->pid, params->tid);
        } else {
            FCGX_FPrintF(request.out,
                "Content-type: text/html\r\n"
                "X-FastCGI-ID: %ld-%d\r\n"
                "\r\n<html>\n"
                "<title>Application Error</title>\n"
                "<body><h2>Unknown Error %d</h2></body>\n"
                "</html>", params->pid, params->tid, rc);
        }
        pthread_mutex_unlock(&lua_mutex);

        FCGX_Finish_r(&request);

        // avoid harmonics
        usleep((int)((rand() % 3) + 1));

    }

    lua_close(L);
    return NULL;

}

int main(void)
{
    int i, sock;
    pid_t pid = getpid();
    char loc[20];
    pthread_t id[THREAD_COUNT];
	struct thread_params params[THREAD_COUNT];

    L = lua_open();
    if (!L) {
        fprintf(stderr, "\tunable to init lua!\n");
        fflush(stderr);
        return 1;
    }
    luaL_openlibs(L);

    FCGX_Init();

	sprintf(loc, ":%d", 9000);
	sock = FCGX_OpenSocket(loc, 100);
    if (!sock) {
        fprintf(stderr, "\tunable to create accept socket!\n");
        fflush(stderr);
        return 1;
    }

	for (i = 0; i < THREAD_COUNT; i ++) {
		params[i].pid = pid;
		params[i].tid = i;
		params[i].sock = sock;
	}

    for (i = 1; i < THREAD_COUNT; i++) {
        pthread_create(&id[i], NULL, doit, (void*)&params[i]);
        usleep(10);
    }

    doit((void*)&params[0]);

    return 0;
}

