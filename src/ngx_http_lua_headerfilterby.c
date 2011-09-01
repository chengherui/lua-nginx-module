/* vim:set ft=c ts=4 sw=4 et fdm=marker: */

#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"

#include "ngx_http_lua_headerfilterby.h"
#include "ngx_http_lua_hook.h"
#include "ngx_http_lua_util.h"
#include "ngx_http_lua_patch.h"
#include "ngx_http_lua_time.h"
#include "ngx_http_lua_log.h"
#include "ngx_http_lua_regex.h"
#include "ngx_http_lua_cache.h"
#include "ngx_http_lua_headers.h"

ngx_http_output_header_filter_pt ngx_http_lua_next_filter_header_filter;


/**
 * Set environment table for the given code closure.
 *
 * Before:
 *         | code closure | <- top
 *         |      ...     |
 *
 * After:
 *         | code closure | <- top
 *         |      ...     |
 * */
static void
ngx_http_lua_header_filter_by_lua_env(lua_State *L, ngx_http_request_t *r)
{
    /*  set nginx request pointer to current lua thread's globals table */
    lua_pushlightuserdata(L, r);
    lua_setglobal(L, GLOBALS_SYMBOL_REQUEST);

    /**
     * we want to create empty environment for current script
     *
     * setmetatable({}, {__index = _G})
     *
     * if a function or symbol is not defined in our env, __index will lookup
     * in the global env.
     *
     * all variables created in the script-env will be thrown away at the end
     * of the script run.
     * */
    lua_newtable(L);    /*  new empty environment aka {} */

    /*  override 'print' function */
    lua_pushcfunction(L, ngx_http_lua_print);
    lua_setfield(L, -2, "print");

    /*  {{{ initialize ngx.* namespace */
    lua_newtable(L);    /*  ngx.* */

    /* {{{ register reference maps */
    lua_newtable(L);    /* ngx.var */

    lua_newtable(L); /* metatable for ngx.var */
    lua_pushcfunction(L, ngx_http_lua_var_get);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, ngx_http_lua_var_set);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);

    lua_setfield(L, -2, "var");
    /*  }}} */

#if (NGX_PCRE)
    /* {{{ ngx.re table */

    lua_newtable(L);    /* .re */

    lua_pushcfunction(L, ngx_http_lua_ngx_re_match);
    lua_setfield(L, -2, "match");

    lua_pushcfunction(L, ngx_http_lua_ngx_re_gmatch);
    lua_setfield(L, -2, "gmatch");

    lua_pushcfunction(L, ngx_http_lua_ngx_re_sub);
    lua_setfield(L, -2, "sub");

    lua_pushcfunction(L, ngx_http_lua_ngx_re_gsub);
    lua_setfield(L, -2, "gsub");

    lua_setfield(L, -2, "re");

    /* }}} */
#endif /* NGX_PCRE */

    lua_pushcfunction(L, ngx_http_lua_ngx_escape_uri);
    lua_setfield(L, -2, "escape_uri");

    lua_pushcfunction(L, ngx_http_lua_ngx_unescape_uri);
    lua_setfield(L, -2, "unescape_uri");

    lua_pushcfunction(L, ngx_http_lua_ngx_quote_sql_str);
    lua_setfield(L, -2, "quote_sql_str");

    lua_pushcfunction(L, ngx_http_lua_ngx_decode_base64);
    lua_setfield(L, -2, "decode_base64");

    lua_pushcfunction(L, ngx_http_lua_ngx_encode_base64);
    lua_setfield(L, -2, "encode_base64");

    lua_pushcfunction(L, ngx_http_lua_ngx_md5_bin);
    lua_setfield(L, -2, "md5_bin");

    lua_pushcfunction(L, ngx_http_lua_ngx_md5);
    lua_setfield(L, -2, "md5");

    lua_pushcfunction(L, ngx_http_lua_ngx_time);
    lua_setfield(L, -2, "get_now_ts"); /* deprecated */

    lua_pushcfunction(L, ngx_http_lua_ngx_utctime);
    lua_setfield(L, -2, "utctime");

    lua_pushcfunction(L, ngx_http_lua_ngx_localtime);
    lua_setfield(L, -2, "get_now"); /* deprecated */

    lua_pushcfunction(L, ngx_http_lua_ngx_time);
    lua_setfield(L, -2, "time");

    lua_pushcfunction(L, ngx_http_lua_ngx_localtime);
    lua_setfield(L, -2, "localtime");

    lua_pushcfunction(L, ngx_http_lua_ngx_today);
    lua_setfield(L, -2, "get_today"); /* deprecated */

    lua_pushcfunction(L, ngx_http_lua_ngx_today);
    lua_setfield(L, -2, "today");

    lua_pushcfunction(L, ngx_http_lua_ngx_http_time);
    lua_setfield(L, -2, "http_time");

	lua_pushcfunction(L, ngx_http_lua_ngx_parse_http_time);
    lua_setfield(L, -2, "parse_http_time");

    lua_pushcfunction(L, ngx_http_lua_ngx_cookie_time);
    lua_setfield(L, -2, "cookie_time");

    lua_pushcfunction(L, ngx_http_lua_ngx_log);
    lua_setfield(L, -2, "log");

    ngx_http_lua_inject_log_api(L);

    lua_newtable(L);    /* .header */

    lua_createtable(L, 0, 2); /* metatable for .header */
    lua_pushcfunction(L, ngx_http_lua_ngx_header_get);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, ngx_http_lua_ngx_header_set);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);

    lua_setfield(L, -2, "header");

    /* ngx. getter and setter */
    lua_createtable(L, 0, 2); /* metatable for .ngx */
    lua_pushcfunction(L, ngx_http_lua_ngx_get);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, ngx_http_lua_ngx_set);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);

    lua_setfield(L, -2, "ngx");
    /*  }}} */

    /*  {{{ make new env inheriting main thread's globals table */
    lua_newtable(L);    /*  the metatable for the new env */
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);    /*  setmetatable({}, {__index = _G}) */
    /*  }}} */

    lua_setfenv(L, -2);    /*  set new running env for the code closure */
}


ngx_int_t
ngx_http_lua_header_filter_by_chunk(lua_State *L, ngx_http_request_t *r)
{
    ngx_int_t        rc;
    u_char          *err_msg;

    /*  set Lua VM panic handler */
    lua_atpanic(L, ngx_http_lua_atpanic);

    /*  initialize nginx context in Lua VM, code chunk at stack top    sp = 1 */
    ngx_http_lua_header_filter_by_lua_env(L, r);

#if (NGX_PCRE)
    /* XXX: work-around to nginx regex subsystem */
    ngx_http_lua_pcre_malloc_init(r->pool);
#endif

    /*  protected call user code */
    rc = lua_pcall(L, 0, 1, 0);

#if (NGX_PCRE)
    /* XXX: work-around to nginx regex subsystem */
    ngx_http_lua_pcre_malloc_done();
#endif

    if (rc != 0) {
        /*  error occured when running loaded code */
        err_msg = (u_char *) lua_tostring(L, -1);

        if (err_msg != NULL) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "(lua-error) %s",
                    err_msg);

            lua_settop(L, 0);    /*  clear remaining elems on stack */
        }

        return NGX_ERROR;
    }

    /*  clear Lua stack */
    lua_settop(L, 0);

    return NGX_OK;
}


ngx_int_t
ngx_http_lua_header_filter_inline(ngx_http_request_t *r)
{
    lua_State                   *L;
    ngx_int_t                    rc;
    ngx_http_lua_main_conf_t    *lmcf;
    ngx_http_lua_loc_conf_t     *llcf;
    char                        *err;

    dd("HERE");

    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lua_module);
    lmcf = ngx_http_get_module_main_conf(r, ngx_http_lua_module);

    L = lmcf->lua;

    /*  load Lua inline script (w/ cache) sp = 1 */
    rc = ngx_http_lua_cache_loadbuffer(L, llcf->header_filter_src.value.data,
            llcf->header_filter_src.value.len, llcf->header_filter_src_key,
            "header_filter_by_lua", &err, llcf->enable_code_cache);

    if (rc != NGX_OK) {
        if (err == NULL) {
            err = "unknown error";
        }

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "Failed to load Lua inlined code: %s", err);

        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    rc = ngx_http_lua_header_filter_by_chunk(L, r);

    dd("header filter by chunk returns %d", (int) rc);

    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_int_t
ngx_http_lua_header_filter_file(ngx_http_request_t *r)
{
    lua_State                       *L;
    ngx_int_t                        rc;
    u_char                          *script_path;
    ngx_http_lua_main_conf_t        *lmcf;
    ngx_http_lua_loc_conf_t         *llcf;
    char                            *err;
    ngx_str_t                        eval_src;

    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lua_module);

    /* Eval nginx variables in code path string first */
    if (ngx_http_complex_value(r, &llcf->header_filter_src, &eval_src)
            != NGX_OK) {
        return NGX_ERROR;
    }

    script_path = ngx_http_lua_rebase_path(r->pool, eval_src.data,
            eval_src.len);

    if (script_path == NULL) {
        return NGX_ERROR;
    }

    lmcf = ngx_http_get_module_main_conf(r, ngx_http_lua_module);
    L = lmcf->lua;

    /*  load Lua script file (w/ cache)        sp = 1 */
    rc = ngx_http_lua_cache_loadfile(L, script_path,
            llcf->header_filter_src_key, &err, llcf->enable_code_cache);

    if (rc != NGX_OK) {
        if (err == NULL) {
            err = "unknown error";
        }

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "Failed to load Lua inlined code: %s", err);

        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /*  make sure we have a valid code chunk */
    assert(lua_isfunction(L, -1));

    rc = ngx_http_lua_header_filter_by_chunk(L, r);

    if (rc != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_lua_filter_header_filter(ngx_http_request_t *r)
{
    ngx_http_lua_loc_conf_t     *llcf;
    ngx_http_lua_ctx_t          *ctx;
    ngx_int_t                    rc;
    ngx_http_lua_main_conf_t    *lmcf;

    dd("in lua filter header filter: %.*s", (int) r->uri.len, r->uri.data);

    lmcf = ngx_http_get_module_main_conf(r, ngx_http_lua_module);
    llcf = ngx_http_get_module_loc_conf(r, ngx_http_lua_module);

    if (llcf->header_filter_handler == NULL) {
        dd("no header filter handler found");
        return ngx_http_lua_next_filter_header_filter(r);
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_lua_module);

    dd("ctx = %p", ctx);

    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_lua_ctx_t));
        if (ctx == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        dd("setting new ctx: ctx = %p", ctx);

        ctx->cc_ref = LUA_NOREF;
        ctx->ctx_ref = LUA_NOREF;

        ngx_http_set_ctx(r, ctx, ngx_http_lua_module);
    }

    dd("calling header filter handler");
    rc = llcf->header_filter_handler(r);
    if (rc != NGX_OK) {
        dd("calling header filter handler rc %d", (int)rc);
        return NGX_ERROR;
    }

    return ngx_http_lua_next_filter_header_filter(r);
}


ngx_int_t
ngx_http_lua_header_filter_init()
{
    dd("calling header filter init");
    ngx_http_lua_next_filter_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_lua_filter_header_filter;

    return NGX_OK;
}
