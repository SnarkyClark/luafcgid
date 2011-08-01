#include "main.h"
#include "parser.h"

// utility functions

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
    //luaL_getmetatable(L, "FCGX.Request");
    //luaL_pushcgienv(L, r);
    //lua_setfield(L, -2, "env");
    //lua_pop(L, 1);
}

// request methods

const struct luaL_Reg request_methods[] = {
    {"gets", req_gets},
    {"puts", req_puts},
    {"parse", req_parse},
    {NULL, NULL}
};

int req_header(lua_State *L) {
    request_t* r = NULL;
    char* s = NULL;
    const char* s1 = NULL;
    const char* s2 = NULL;
    size_t l = 0;
    size_t l1= 0;
    size_t l2 = 0;
	const char newline = "\r\n";
    if (lua_gettop(L) >= 2) {
		r = luaL_checkrequest(L, 1);
        if lua_istable(L, 2) {
			/* parse out the headers table */
			lua_pushnil(L);  /* first key */
			while (lua_next(L, 2) != 0) {
				/* uses 'key' (at index -2) and 'value' (at index -1) */
				if lua_isstring(L, -2) {
					s1 = lua_tolstring(L, -2, &l1);
					s2 = lua_checklstring(L, -1, &l2);
					s = (char*)malloc(sizeof(char) * (l1 + l2 + 4));
					l = sprintf(s, "%s: %s\r\n", s1, s2);
					FCGX_PutStr(s, l, r->fcgi.out);
					free(s);
				}
				/* removes 'value'; keeps 'key' for next iteration */
				lua_pop(L, 1);
			}			
		} else {
			/* output as sent */
			s1 = lua_checklstring(L, 2, &l1);
			FCGX_PutStr(s1, l1, r->fcgi.out);
		}
		/* add extra newline as per HTTP specs */
		FCGX_PutStr(newline, 2, r->fcgi.out);
	}
	r->headers_sent = TRUE;
	return 0;
}

int req_puts(lua_State *L) {
    request_t* r = NULL;
    const char* s = NULL;
    size_t l = 0;
    if (lua_gettop(L) >= 2) {
        r = luaL_checkrequest(L, 1);
        s = lua_checklstring(L, 2, &l);
        /* make sure headers are sent before any data */
        if(!r->headers_sent) {
            /* default headers */
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

int req_parse(lua_State *L) {
    request_t* r = NULL;
    const char* s = NULL;
    size_t l = 0;
    if (lua_gettop(L) >= 2) {
        r = luaL_checkrequest(L, 1);
        s = lua_checklstring(L, 2, &l);
        lua_newtable(L);
        parser_decode(L, s);
    } else {
        lua_pushnil(L);
    }
    return 1;
}
