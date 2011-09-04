#ifndef REQUEST_H_INCLUDED
#define REQUEST_H_INCLUDED

#include "buffer.h"

struct request_struct {
    FCGX_Request fcgi;
    /* output buffering */
    BOOL headers_sent;
    buffer_t header;
    buffer_t body;
    /* local config */
    BOOL buffering;
    char httpstatus[128];
    char contenttype[128];
    /* READ ONLY vars! */
    int wid;
    const config_t* conf;
} typedef request_t;

request_t* luaL_checkrequest(lua_State* L, int i);
void luaL_loadrequest(lua_State* L);
void luaL_pushrequest(lua_State* L, request_t* r);

extern const struct luaL_Reg request_methods[];

/* r:header(string, <string>) */
int L_req_header(lua_State *L);
/* r:puts(string) */
int L_req_puts(lua_State *L);
/* r:gets() returns string */
int L_req_gets(lua_State *L);
/* r:flush() */
int L_req_flush(lua_State *L);
/* r:reset() */
int L_req_reset(lua_State *L);
/* r:config(string, <string>) returns string */
int L_req_config(lua_State *L);
/* r:log(string) */
int L_req_log(lua_State *L);

#endif // REQUEST_H_INCLUDED
