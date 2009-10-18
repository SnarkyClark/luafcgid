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
#define sleep(msec) Sleep(msec)
#endif

#define THREAD_COUNT 10

static lua_State* L;
static int loaded = 0;

const char hello_world[] =
    "function handler()"
    "   return 'Hello World!'"
    "end";


// static int counts[THREAD_COUNT];

static void *doit(void *a)
{
    int rc, thread_id = (int)a;
    char loc[20];
    int sock = 0;
    pid_t pid = getpid();
    FCGX_Request request;
    //char *server_name;

    sprintf(loc, "localhost:%d", 9000);
    fprintf(stderr, "starting worker %d @ %s\n", thread_id, loc);
    fflush(stderr);

    sock = FCGX_OpenSocket(loc, 100);
    if (!sock) {
        fprintf(stderr, "\tunable to create accept socket!\n");
        fflush(stderr);
        return NULL;
    }

    FCGX_InitRequest(&request, sock, 0);

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
                "</html>", pid, thread_id, lua_tostring(L, -1));
                lua_pop(L, 1);
        } else if (rc == LUA_ERRRUN) {
            FCGX_FPrintF(request.out,
                "Content-type: text/html\r\n"
                "X-FastCGI-ID: %ld-%d\r\n"
                "\r\n<html>\n"
                "<title>Application Error</title>\n"
                "<body><h2>Runtime Error</h2></body>\n"
                "<p>%s</p>\n"
                "</html>", pid, thread_id, lua_tostring(L, -1));
                lua_pop(L, 1);
        } else if (rc == LUA_ERRSYNTAX) {
            FCGX_FPrintF(request.out,
                "Content-type: text/html\r\n"
                "X-FastCGI-ID: %ld-%d\r\n"
                "\r\n<html>\n"
                "<title>Application Error</title>\n"
                "<body><h2>Syntax Error</h2></body>\n"
                "<p>%s</p>\n"
                "</html>", pid, thread_id, lua_tostring(L, -1));
                lua_pop(L, 1);
        } else if (rc == LUA_ERRMEM) {
            FCGX_FPrintF(request.out,
                "Content-type: text/html\r\n"
                "X-FastCGI-ID: %ld-%d\r\n"
                "\r\n<html>\n"
                "<title>Application Error</title>\n"
                "<body><h2>Out Of Memory</h2></body>\n"
                "</html>", pid, thread_id);
        } else {
            FCGX_FPrintF(request.out,
                "Content-type: text/html\r\n"
                "X-FastCGI-ID: %ld-%d\r\n"
                "\r\n<html>\n"
                "<title>Application Error</title>\n"
                "<body><h2>Unknown Error %d</h2></body>\n"
                "</html>", pid, thread_id, rc);
        }
        pthread_mutex_unlock(&lua_mutex);

        FCGX_Finish_r(&request);

        // avoid harmonics
        sleep((int)((rand() % 3) + 1));

    }

    lua_close(L);
    return NULL;

}

int main(void)
{
    int i;
    pthread_t id[THREAD_COUNT];

    L = lua_open();
    if (!L) {
        fprintf(stderr, "\tunable to init lua!\n");
        fflush(stderr);
        return 1;
    }
    luaL_openlibs(L);

    FCGX_Init();

    for (i = 1; i < THREAD_COUNT; i++) {
        pthread_create(&id[i], NULL, doit, (void*)i);
        sleep(10);
    }

    doit(0);

    return 0;
}

