/*************************************************************************************************
 * Scripting language extension of Tokyo Promenade
 *                                                      Copyright (C) 2008-2010 Mikio Hirabayashi
 * This file is part of Tokyo Promenade.
 * This program is free software: you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, either version
 * 3 of the License, or any later version.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *************************************************************************************************/


#include "scrext.h"



/*************************************************************************************************
 * by default
 *************************************************************************************************/


#if defined(TTNOEXT)


/* Initialize the scripting language extension. */
void *scrextnew(void){
  return NULL;
}


/* Destroy the scripting language extension. */
void scrextdel(void *scr){
  assert(scr);
}


/* Load a script file of the scripting language extension. */
bool scrextload(void *scr, const char *path){
  assert(scr && path);
  return false;
}


/* Check a function is implemented by the scripting language extension. */
bool scrextcheckfunc(void *scr, const char *name){
  assert(scr && name);
  return false;
}


/* Call a function of the scripting language extension. */
char *scrextcallfunc(void *scr, const char *name, const char *param){
  assert(scr && name && param);
  return NULL;
}


/* Get the error message of the last operation of scripting language extension. */
const char *screxterrmsg(void *scr){
  assert(scr);
  return NULL;
}


/* Set a table variable of scripting language extension. */
void scrextsetmapvar(void *scr, const char *name, const TCMAP *params){
  assert(scr && name && params);
}


#endif



/*************************************************************************************************
 * for Lua
 *************************************************************************************************/


#if defined(TTLUAEXT)


#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

typedef struct {                         // type of structure of the script extension
  lua_State *lua;                        // Lua environment
  TCXSTR *emsg;                          // error message
} SCREXT;


/* private function prototypes */
static void scrextseterrmsg(SCREXT *scr);
static int scrextstrstr(lua_State *lua);
static int scrextregex(lua_State *lua);


/* Initialize the scripting language extension. */
void *scrextnew(void){
  lua_State *lua = luaL_newstate();
  if(!lua) return NULL;
  luaL_openlibs(lua);
  lua_pushcfunction(lua, scrextstrstr);
  lua_setglobal(lua, "_strstr");
  lua_pushcfunction(lua, scrextregex);
  lua_setglobal(lua, "_regex");
  SCREXT *scr = tcmalloc(sizeof(*scr));
  scr->lua = lua;
  scr->emsg = tcxstrnew();
  return scr;
}


/* Destroy the scripting language extension. */
void scrextdel(void *scr){
  assert(scr);
  SCREXT *s = scr;
  tcxstrdel(s->emsg);
  lua_close(s->lua);
  tcfree(s);
}


/* Load a script file of the scripting language extension. */
bool scrextload(void *scr, const char *path){
  assert(scr && path);
  SCREXT *s = scr;
  lua_State *lua = s->lua;
  tcxstrclear(s->emsg);
  char *ibuf;
  int isiz;
  if(*path == '@'){
    ibuf = tcstrdup(path + 1);
    isiz = strlen(ibuf);
  } else if(*path != '\0'){
    ibuf = tcreadfile(path, 0, &isiz);
  } else {
    ibuf = tcmemdup("", 0);
    isiz = 0;
  }
  bool err = false;
  if(ibuf){
    lua_settop(lua, 0);
    if(luaL_loadstring(lua, ibuf) == 0){
      if(lua_pcall(lua, 0, 0, 0) != 0){
        scrextseterrmsg(s);
        err = true;
      }
    } else {
      scrextseterrmsg(s);
      err = true;
    }
    tcfree(ibuf);
  } else {
    tcxstrprintf(s->emsg, "%s: could not open", path);
    err = true;
  }
  return !err;
}


/* Check a function is implemented by the scripting language extension. */
bool scrextcheckfunc(void *scr, const char *name){
  assert(scr && name);
  SCREXT *s = scr;
  lua_State *lua = s->lua;
  lua_settop(lua, 0);
  lua_getglobal(lua, name);
  return lua_gettop(lua) == 1 && lua_isfunction(lua, 1);
}


/* Get the error message of the last operation of scripting language extension. */
const char *screxterrmsg(void *scr){
  assert(scr);
  SCREXT *s = scr;
  const char *emsg = tcxstrptr(s->emsg);
  return *emsg != '\0' ? emsg : NULL;
}


/* Call a function of the scripting language extension. */
char *scrextcallfunc(void *scr, const char *name, const char *param){
  assert(scr && name && param);
  SCREXT *s = scr;
  lua_State *lua = s->lua;
  tcxstrclear(s->emsg);
  lua_settop(lua, 0);
  lua_getglobal(lua, name);
  if(lua_gettop(lua) != 1 || !lua_isfunction(lua, 1)){
    tcxstrprintf(s->emsg, "%s: no such function", name);
    return NULL;
  }
  lua_pushstring(lua, param);
  if(lua_pcall(lua, 1, 1, 0) != 0){
    scrextseterrmsg(s);
    return NULL;
  }
  if(lua_gettop(lua) < 1){
    tcxstrprintf(s->emsg, "%s: no return value", name);
    return NULL;
  }
  const char *res = NULL;
  switch(lua_type(lua, 1)){
    case LUA_TNUMBER:
    case LUA_TSTRING:
      res = lua_tostring(lua, 1);
      break;
    case LUA_TBOOLEAN:
      res = lua_toboolean(lua, 1) ? "[true]" : "[false]";
      break;
    case LUA_TTABLE:
      res = "[table]";
      break;
    case LUA_TNIL:
      res = "";
      break;
  }
  if(!res) res = "[unknown]";
  return tcstrdup(res);
}


/* Set a table variable of scripting language extension. */
void scrextsetmapvar(void *scr, const char *name, const TCMAP *params){
  assert(scr && name && params);
  SCREXT *s = scr;
  lua_State *lua = s->lua;
  lua_settop(lua, 0);
  TCLIST *keys = tcmapkeys(params);
  int knum = tclistnum(keys);
  lua_createtable(lua, 0, knum);
  for(int i = 0; i < knum; i++){
    const char *key = tclistval2(keys, i);
    const char *val = tcmapget2(params, key);
    if(val){
      lua_pushstring(lua, key);
      lua_pushstring(lua, val);
      lua_settable(lua, 1);
    }
  }
  tclistdel(keys);
  lua_setglobal(lua, name);
}


/* Set the error message the last operation.
   `scr' specifies the scripting object. */
static void scrextseterrmsg(SCREXT *scr){
  assert(scr);
  lua_State *lua = scr->lua;
  int argc = lua_gettop(lua);
  tcxstrcat2(scr->emsg, argc > 0 ? lua_tostring(lua, argc) : "unknown");
}


/* Built-in function of strstr.
   `lua' specifies the lua processor object
   The return value is the number of the return values. */
static int scrextstrstr(lua_State *lua){
  int argc = lua_gettop(lua);
  if(argc < 2){
    lua_pushstring(lua, "strstr: invalid arguments");
    lua_error(lua);
  }
  const char *str = lua_tostring(lua, 1);
  const char *pat = lua_tostring(lua, 2);
  if(!str || !pat){
    lua_pushstring(lua, "strstr: invalid arguments");
    lua_error(lua);
  }
  const char *alt = argc > 2 ? lua_tostring(lua, 3) : NULL;
  if(alt){
    TCXSTR *xstr = tcxstrnew();
    int plen = strlen(pat);
    int alen = strlen(alt);
    if(plen > 0){
      char *pv;
      while((pv = strstr(str, pat)) != NULL){
        tcxstrcat(xstr, str, pv - str);
        tcxstrcat(xstr, alt, alen);
        str = pv + plen;
      }
    }
    tcxstrcat2(xstr, str);
    lua_settop(lua, 0);
    lua_pushstring(lua, tcxstrptr(xstr));
    tcxstrdel(xstr);
  } else {
    char *pv = strstr(str, pat);
    if(pv){
      int idx = pv - str + 1;
      lua_settop(lua, 0);
      lua_pushinteger(lua, idx);
    } else {
      lua_settop(lua, 0);
      lua_pushinteger(lua, 0);
    }
  }
  return 1;
}


/* Built-in function of regex.
   `lua' specifies the lua processor object
   The return value is the number of the return values. */
static int scrextregex(lua_State *lua){
  int argc = lua_gettop(lua);
  if(argc < 2){
    lua_pushstring(lua, "regex: invalid arguments");
    lua_error(lua);
  }
  const char *str = lua_tostring(lua, 1);
  const char *regex = lua_tostring(lua, 2);
  if(!str || !regex){
    lua_pushstring(lua, "regex: invalid arguments");
    lua_error(lua);
  }
  const char *alt = argc > 2 ? lua_tostring(lua, 3) : NULL;
  if(alt){
    char *res = tcregexreplace(str, regex, alt);
    lua_settop(lua, 0);
    lua_pushstring(lua, res);
    tcfree(res);
  } else {
    if(tcregexmatch(str, regex)){
      lua_settop(lua, 0);
      lua_pushboolean(lua, true);
    } else {
      lua_settop(lua, 0);
      lua_pushboolean(lua, false);
    }
  }
  return 1;
}


#endif



// END OF FILE
