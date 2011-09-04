#include "main.h"

/* utility functions */

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
    lua_pop(L, 1);
}

void luaL_pushrequest(lua_State* L, request_t* r) {
    lua_boxpointer(L, r);
    luaL_getmetatable(L, "LuaFCGId.Request");
    lua_setmetatable(L, -2);
}

/* request methods */
const struct luaL_Reg request_methods[] = {
    {"header", L_req_header},
    {"gets", L_req_gets},
    {"puts", L_req_puts},
    {"flush", L_req_flush},
    {"reset", L_req_reset},
    {"config", L_req_config},
    {"log", L_req_log},
    {NULL, NULL}
};

/* r:header(string, <string>) */
int L_req_header(lua_State *L) {
    request_t* r = NULL;
    const char* s1 = NULL;
    const char* s2 = NULL;
    size_t l1, l2 = 0;
    if (lua_gettop(L) >= 2) {
		r = luaL_checkrequest(L, 1);
		s1 = luaL_checklstring(L, 2, &l1);
		if (lua_gettop(L) == 3) {
			s2 = luaL_checklstring(L, 3, &l2);
			buffer_add(&r->header, s1, l1);
			buffer_add(&r->header, ": ", -1);
			buffer_add(&r->header, s2, l2);
			buffer_add(&r->header, CRLF, -1);
		} else {
			buffer_add(&r->header, s1, l1);
			buffer_add(&r->header, CRLF, -1);
		}
    }
	return 0;
}

/* r:puts(string) */
int L_req_puts(lua_State *L) {
    request_t* r = NULL;
    const char* s = NULL;
    size_t l = 0;
    if (lua_gettop(L) >= 2) {
        r = luaL_checkrequest(L, 1);
        s = luaL_checklstring(L, 2, &l);
        if (r->buffering) {
        	/* add to output buffer */
        	buffer_add(&r->body, s, l);
        } else {
			/* make sure headers are sent before any data */
			if(!r->headers_sent)
				send_header(r);
			FCGX_PutStr(s, l, r->fcgi.out);
        }
    }
    return 0;
}

/* r:flush() */
int L_req_flush(lua_State *L) {
    request_t* r = NULL;
    if (lua_gettop(L)) {
        r = luaL_checkrequest(L, 1);
       	send_body(r);
		r->body.len = 0;
    }
    return 0;
}

/* r:reset() */
int L_req_reset(lua_State *L) {
    request_t* r = NULL;
    if (lua_gettop(L)) {
        r = luaL_checkrequest(L, 1);
        r->body.len = 0;
    }
    return 0;
}

/* r:gets() returns string */
int L_req_gets(lua_State *L) {
    request_t* r = NULL;
    if (lua_gettop(L)) {
        r = luaL_checkrequest(L, 1);
        luaL_pushcgicontent(L, r);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

/* r:config(string, <string>) returns string */
int L_req_config(lua_State *L) {
    request_t* r = NULL;
    const char* n = NULL;
    const char* v = NULL;
    size_t ln, lv = 0;
    if (lua_gettop(L) >= 2) {
        r = luaL_checkrequest(L, 1);
        n = luaL_checklstring(L, 2, &ln);
        if (lua_gettop(L) == 3) {
            v = luaL_checklstring(L, 3, &lv);
            /* TODO: set config value */
        } else {
            /* TODO: get config value */
        	lua_pushnil(L);
            return 1;
        }
    }
    return 0;
}

/* r:log(string) */
int L_req_log(lua_State *L) {
    request_t* r = NULL;
    const char* s = NULL;
    size_t l = 0;
    if (lua_gettop(L) >= 2) {
        r = luaL_checkrequest(L, 1);
        s = luaL_checklstring(L, 2, &l);
        logit(s);
    }
    return 0;
}
