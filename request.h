#ifndef REQUEST_H_INCLUDED
#define REQUEST_H_INCLUDED

struct request_struct {
    FCGX_Request fcgi;
    BOOL headers_sent;
	// READ ONLY config!
    const config_t* conf;
} typedef request_t;

request_t* luaL_checkrequest(lua_State* L, int i);
void luaL_loadrequest(lua_State* L);
void luaL_pushrequest(lua_State* L, request_t* r);

extern const struct luaL_Reg request_methods[];

int req_puts(lua_State *L);
int req_gets(lua_State *L);

#endif // REQUEST_H_INCLUDED
