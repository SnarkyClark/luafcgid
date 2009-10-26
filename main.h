#ifndef MAIN_H_INCLUDED
#define MAIN_H_INCLUDED

#define LUAFCGID_VERSION 1

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#define lua_boxpointer(L,u) (*(void **)(lua_newuserdata(L, sizeof(void *))) = (u))
#define lua_unboxpointer(L,i) (*(void **)(lua_touserdata(L, i)))

#ifdef _WIN32
#include <windows.h>
#define usleep(msec) Sleep(msec)
#define SEP '\\'
#endif

#ifndef BOOL
#define BOOL int
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

#define LISTEN_PATH ":9000"

#define STATUS_OK 0
#define STATUS_BUSY 1
#define STATUS_404 LUA_ERRFILE + 1

#define HTTP_200(stream, data) \
    FCGX_FPrintF(stream, \
		"Status: 200 OK\r\n" \
		"Content-Type: text/html\r\n\r\n" \
		"%s", data)
#define HTTP_404(stream, filename) \
    FCGX_FPrintF(stream, \
		"Status: 404 Not Found\r\n" \
		"Content-Type: text/html\r\n\r\n" \
		"<html>\r\n<head>\r\n" \
		"<title>The page was not found</title>\r\n" \
		"<style type='text/css'>\r\n" \
		"\tbody {font-family: Tahoma,Verdana,Arial,sans-serif;}\r\n" \
		"\tp.details {color: gray; font-size: 85%%}\r\n" \
		"\tpre {color:maroon; font-size:85%%; margin-top:6pt}\r\n" \
		"</style>\r\n</head>\r\n<body bgcolor=\"white\" text=\"black\">\r\n" \
		"<table width=\"100%%\" height=\"100%%\"><tr><td align=\"center\" valign=\"middle\">\r\n" \
		"<p>The page you are looking for cannot be found</p><br/>" \
		"<p class=\"details\">- <b>Filename</b> -<br/>\r\n<pre>\"%s\"</pre></p>\r\n" \
		"</td></tr></table>\r\n</body>\r\n</html>", filename)
#define HTTP_500(stream, errtype, errmsg) \
    FCGX_FPrintF(stream, \
		"Status: 500 Internal Server Error\r\n" \
		"Content-Type: text/html\r\n\r\n" \
		"<html>\r\n<head>\r\n" \
		"<title>The page is temporarily unavailable</title>\r\n" \
		"<style type='text/css'>\r\n" \
		"\tbody {font-family: Tahoma,Verdana,Arial,sans-serif;}\r\n" \
		"\tp.details {color: gray; font-size: 85%%}\r\n" \
		"\tpre {color:maroon; font-size:85%%; margin-top:6pt}\r\n" \
		"</style>\r\n</head>\r\n<body bgcolor=\"white\" text=\"black\">\r\n" \
		"<table width=\"100%%\" height=\"100%%\"><tr><td align=\"center\" valign=\"middle\">\r\n" \
		"<p>The page you are looking for is temporarily unavailable.<br/>Please try again later.</p><br/>" \
		"<p class=\"details\">- <b>%s</b> -<br/>\r\n<table><tr><td><pre>\r\n%s\r\n" \
		"</pre></td></tr></table></p>\r\n" \
		"</td></tr></table>\r\n</body>\r\n</html>", errtype, errmsg)

#define LUA_ERRFILE_STR "File Error"
#define LUA_ERRRUN_STR "Runtime Error"
#define LUA_ERRSYNTAX_STR "Syntax Error"
#define LUA_ERRMEM_STR "Memory Error"
#define ERRUNKNOWN_STR "Unknown Error"

#define HOOK_HOUSEKEEPING 0
#define HOOK_STARTUP 1
#define HOOK_SHUTDOWN 2
#define HOOK_COUNT 3

struct vm_pool_struct {
	int status;
	char* name;
	time_t load;
	lua_State* state;
} typedef vm_pool_t;

struct hook_struct {
	int count;
	char** chunk;
} typedef hook_t;

struct config_struct {
	char* listen;
	int workers;
	int states;
	int sweep;
	int retries;
	int maxpost;
	hook_t* hook[HOOK_COUNT];
} typedef config_t;

struct params_struct {
	int pid;
	int tid;
	int sock;
	const config_t* conf;
	vm_pool_t* pool;
} typedef params_t;

struct request_struct {
    FCGX_Request fcgi;
    const config_t* conf;
    BOOL headers_sent;
} typedef request_t;

BOOL luaL_getglobal_int(lua_State* L, const char* name, int* v);
BOOL luaL_getglobal_str(lua_State* L, const char* name, char** v);

char* script_load(const char* fn, struct stat* fs);
config_t* config_load(const char* fn);
void pool_load(vm_pool_t *p, lua_State* L, char* name);
void pool_flush(vm_pool_t* p);

int req_gets(lua_State *L);
int req_puts(lua_State *L);

#endif // MAIN_H_INCLUDED
