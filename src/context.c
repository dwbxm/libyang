/**
 * @file context.c
 * @author Radek Krejci <rkrejci@cesnet.cz>
 * @brief Context implementations
 *
 * Copyright (c) 2015 - 2018 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#include "common.h"

#define _BSD_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "context.h"
#include "tree_schema_internal.h"
#include "libyang.h"

#define LY_INTERNAL_MODS_COUNT 6

#define IETF_YANG_METADATA_PATH "../models/ietf-yang-metadata@2016-08-05.h"
#define YANG_PATH "../models/yang@2017-02-20.h"
#define IETF_INET_TYPES_PATH "../models/ietf-inet-types@2013-07-15.h"
#define IETF_YANG_TYPES_PATH "../models/ietf-yang-types@2013-07-15.h"
#define IETF_DATASTORES "../models/ietf-datastores@2017-08-17.h"
#define IETF_YANG_LIB_PATH "../models/ietf-yang-library@2018-01-17.h"
#define IETF_YANG_LIB_REV "2018-01-17"

#include IETF_YANG_METADATA_PATH
#include YANG_PATH
#include IETF_INET_TYPES_PATH
#include IETF_YANG_TYPES_PATH
#include IETF_DATASTORES
#include IETF_YANG_LIB_PATH

static struct internal_modules_s {
    const char *name;
    const char *revision;
    const char *data;
    uint8_t implemented;
    LYS_INFORMAT format;
} internal_modules[LY_INTERNAL_MODS_COUNT] = {
    {"ietf-yang-metadata", "2016-08-05", (const char*)ietf_yang_metadata_2016_08_05_yang, 0, LYS_IN_YANG},
    {"yang", "2017-02-20", (const char*)yang_2017_02_20_yang, 1, LYS_IN_YANG},
    {"ietf-inet-types", "2013-07-15", (const char*)ietf_inet_types_2013_07_15_yang, 0, LYS_IN_YANG},
    {"ietf-yang-types", "2013-07-15", (const char*)ietf_yang_types_2013_07_15_yang, 0, LYS_IN_YANG},
    /* ietf-datastores and ietf-yang-library must be right here at the end of the list! */
    {"ietf-datastores", "2017-08-17", (const char*)ietf_datastores_2017_08_17_yang, 0, LYS_IN_YANG},
    {"ietf-yang-library", IETF_YANG_LIB_REV, (const char*)ietf_yang_library_2018_01_17_yang, 1, LYS_IN_YANG}
};

API LY_ERR
ly_ctx_set_searchdir(struct ly_ctx *ctx, const char *search_dir)
{
    struct stat st;
    char *new_dir = NULL;
    unsigned int u;

    LY_CHECK_ARG_RET(ctx, ctx, LY_EINVAL);

    if (search_dir) {
        LY_CHECK_ERR_RET(access(search_dir, R_OK | X_OK),
                         LOGERR(ctx, LY_ESYS, "Unable to use search directory \"%s\" (%s)", search_dir, strerror(errno)),
                         LY_EINVAL);
        LY_CHECK_ERR_RET(stat(search_dir, &st),
                         LOGERR(ctx, LY_ESYS, "stat() failed for \"%s\" (%s)", search_dir, strerror(errno)),
                         LY_ESYS);
        LY_CHECK_ERR_RET(!S_ISDIR(st.st_mode),
                         LOGERR(ctx, LY_ESYS, "Given search directory \"%s\" is not a directory.", search_dir),
                         LY_EINVAL);
        new_dir = realpath(search_dir, NULL);
        LY_CHECK_ERR_RET(!new_dir,
                         LOGERR(ctx, LY_ESYS, "realpath() call failed for \"%s\" (%s).", search_dir, strerror(errno)),
                         LY_ESYS);
        /* avoid path duplication */
        for (u = 0; u < ctx->search_paths.count; ++u) {
            if (!strcmp(new_dir, ctx->search_paths.objs[u])) {
                free(new_dir);
                return LY_EEXIST;
            }
        }
        if (ly_set_add(&ctx->search_paths, new_dir, LY_SET_OPT_USEASLIST) == -1) {
            free(new_dir);
            return LY_EMEM;
        }

        /* new searchdir - possibly more latest revision available */
        ly_ctx_reset_latests(ctx);

        return LY_SUCCESS;
    } else {
        /* consider that no change is not actually an error */
        return LY_SUCCESS;
    }
}

API const char * const *
ly_ctx_get_searchdirs(const struct ly_ctx *ctx)
{
    void **new;

    LY_CHECK_ARG_RET(ctx, ctx, NULL);

    if (ctx->search_paths.count == ctx->search_paths.size) {
        /* not enough space for terminating NULL byte */
        new = realloc(((struct ly_ctx *)ctx)->search_paths.objs, (ctx->search_paths.size + 8) * sizeof *ctx->search_paths.objs);
        LY_CHECK_ERR_RET(!new, LOGMEM(NULL), NULL);
        ((struct ly_ctx *)ctx)->search_paths.size += 8;
        ((struct ly_ctx *)ctx)->search_paths.objs = new;
    }
    /* set terminating NULL byte to the strings list */
    ctx->search_paths.objs[ctx->search_paths.count] = NULL;

    return (const char * const *)ctx->search_paths.objs;
}

API LY_ERR
ly_ctx_unset_searchdirs(struct ly_ctx *ctx, const char *value)
{
    unsigned int index;

    LY_CHECK_ARG_RET(ctx, ctx, LY_EINVAL);

    if (!ctx->search_paths.count) {
        return LY_SUCCESS;
    }

    if (value) {
        /* remove specific search directory */
        for (index = 0; index < ctx->search_paths.count; ++index) {
            if (!strcmp(value, ctx->search_paths.objs[index])) {
                break;
            }
        }
        if (index == ctx->search_paths.count) {
            LOGARG(ctx, value);
            return LY_EINVAL;
        } else {
            return ly_set_rm_index(&ctx->search_paths, index, free);
        }
    } else {
        /* remove them all */
        ly_set_erase(&ctx->search_paths, free);
        memset(&ctx->search_paths, 0, sizeof ctx->search_paths);
    }

    return LY_SUCCESS;
}

API LY_ERR
ly_ctx_new(const char *search_dir, int options, struct ly_ctx **new_ctx)
{
    struct ly_ctx *ctx = NULL;
    struct lys_module *module;
    char *search_dir_list;
    char *sep, *dir;
    int i;
    LY_ERR rc = LY_SUCCESS;

    ctx = calloc(1, sizeof *ctx);
    LY_CHECK_ERR_RET(!ctx, LOGMEM(NULL), LY_EMEM);

    /* dictionary */
    lydict_init(&ctx->dict);

#if 0 /* TODO when plugins implemented */
    /* plugins */
    ly_load_plugins();
#endif

    /* initialize thread-specific key */
    while ((pthread_key_create(&ctx->errlist_key, ly_err_free)) == EAGAIN);

    /* models list */
    ctx->flags = options;
    if (search_dir) {
        search_dir_list = strdup(search_dir);
        LY_CHECK_ERR_GOTO(!search_dir_list, LOGMEM(NULL); rc = LY_EMEM, error);

        for (dir = search_dir_list; (sep = strchr(dir, ':')) != NULL && rc == LY_SUCCESS; dir = sep + 1) {
            *sep = 0;
            rc = ly_ctx_set_searchdir(ctx, dir);
            if (rc == LY_EEXIST) {
                /* ignore duplication */
                rc = LY_SUCCESS;
            }
        }
        if (*dir && rc == LY_SUCCESS) {
            rc = ly_ctx_set_searchdir(ctx, dir);
            if (rc == LY_EEXIST) {
                /* ignore duplication */
                rc = LY_SUCCESS;
            }
        }
        free(search_dir_list);

        /* If ly_ctx_set_searchdir() failed, the error is already logged. Just exit */
        if (rc != LY_SUCCESS) {
            goto error;
        }
    }
    ctx->module_set_id = 1;

    /* load internal modules */
    for (i = 0; i < ((options & LY_CTX_NOYANGLIBRARY) ? (LY_INTERNAL_MODS_COUNT - 2) : LY_INTERNAL_MODS_COUNT); i++) {
        module = (struct lys_module *)lys_parse_mem(ctx, internal_modules[i].data, internal_modules[i].format);
        LY_CHECK_GOTO(!module, error);
        module->parsed->implemented = internal_modules[i].implemented;
    }

    *new_ctx = ctx;
    return rc;

error:
    ly_ctx_destroy(ctx, NULL);
    return rc;
}
API int
ly_ctx_get_options(const struct ly_ctx *ctx)
{
    LY_CHECK_ARG_RET(ctx, ctx, 0);
    return ctx->flags;
}

API LY_ERR
ly_ctx_set_option(struct ly_ctx *ctx, int option)
{
    LY_CHECK_ARG_RET(ctx, ctx, LY_EINVAL);
    LY_CHECK_ERR_RET(option & LY_CTX_NOYANGLIBRARY, LOGARG(ctx, option), LY_EINVAL);

    /* set the option(s) */
    ctx->flags |= option;

    return LY_SUCCESS;
}

API LY_ERR
ly_ctx_unset_option(struct ly_ctx *ctx, int option)
{
    LY_CHECK_ARG_RET(ctx, ctx, LY_EINVAL);
    LY_CHECK_ERR_RET(option & LY_CTX_NOYANGLIBRARY, LOGARG(ctx, option), LY_EINVAL);

    /* unset the option(s) */
    ctx->flags &= ~option;

    return LY_SUCCESS;
}

API uint16_t
ly_ctx_get_module_set_id(const struct ly_ctx *ctx)
{
    LY_CHECK_ARG_RET(ctx, ctx, 0);
    return ctx->module_set_id;
}

API void
ly_ctx_set_module_imp_clb(struct ly_ctx *ctx, ly_module_imp_clb clb, void *user_data)
{
    LY_CHECK_ARG_RET(ctx, ctx,);

    ctx->imp_clb = clb;
    ctx->imp_clb_data = user_data;
}

API ly_module_imp_clb
ly_ctx_get_module_imp_clb(const struct ly_ctx *ctx, void **user_data)
{
    LY_CHECK_ARG_RET(ctx, ctx, NULL);

    if (user_data) {
        *user_data = ctx->imp_clb_data;
    }
    return ctx->imp_clb;
}

/**
 * @brief Iterate over the modules in the given context. Returned modules must match the given key at the offset of
 * lysp_module and lysc_module structures (they are supposed to be placed at the same offset in both structures).
 *
 * @param[in] ctx Context where to iterate.
 * @param[in] key Key value to search for.
 * @param[in] key_offset Key's offset in struct lysp_module and struct lysc_module to get value from the context's
 * modules to match with the key.
 * @param[in,out] Iterator to pass between the function calls. On the first call, the variable is supposed to be
 * initiated to 0. After each call returning a module, the value is greater by 1 than the index of the returned
 * module in the context.
 * @return Module matching the given key, NULL if no such module found.
 */
static const struct lys_module *
ly_ctx_get_module_by_iter(const struct ly_ctx *ctx, const char *key, size_t key_offset, unsigned int *index)
{
    const struct lys_module *mod;
    const char *value;

    for (; *index < ctx->list.count; ++(*index)) {
        mod = ctx->list.objs[*index];
        if (mod->compiled) {
            value = *(const char**)(((int8_t*)(mod->compiled)) + key_offset);
        } else {
            value = *(const char**)(((int8_t*)(mod->parsed)) + key_offset);
        }
        if (!strcmp(key, value)) {
            /* increment index for the next run */
            ++(*index);
            return mod;
        }
    }
    /* done */
    return NULL;
}

/**
 * @brief Unifying function for ly_ctx_get_module() and ly_ctx_get_module_ns()
 * @param[in] ctx Context where to search.
 * @param[in] key Name or Namespace as a search key.
 * @param[in] key_offset Key's offset in struct lysp_module to get value from the context's modules to match with the key.
 * @param[in] revision Revision date to match. If NULL, the matching module must have no revision. To search for the latest
 * revision module, use ly_ctx_get_module_latest_by().
 * @return Matching module if any.
 */
static const struct lys_module *
ly_ctx_get_module_by(const struct ly_ctx *ctx, const char *key, size_t key_offset, const char *revision)
{
    const struct lys_module *mod;
    unsigned int index = 0;

    while ((mod = ly_ctx_get_module_by_iter(ctx, key, key_offset, &index))) {
        if (!revision) {
            if ((mod->compiled && !mod->compiled->revs) || (!mod->compiled && !mod->parsed->revs)) {
                /* found requested module without revision */
                return mod;
            }
        } else {
            if ((mod->compiled && mod->compiled->revs && !strcmp(mod->compiled->revs[0].date, revision)) ||
                    (!mod->compiled && mod->parsed->revs && !strcmp(mod->parsed->revs[0].date, revision))) {
                /* found requested module of the specific revision */
                return mod;
            }
        }
    }

    return NULL;
}

API const struct lys_module *
ly_ctx_get_module_ns(const struct ly_ctx *ctx, const char *ns, const char *revision)
{
    LY_CHECK_ARG_RET(ctx, ctx, ns, NULL);
    return ly_ctx_get_module_by(ctx, ns, offsetof(struct lysp_module, ns), revision);
}

API const struct lys_module *
ly_ctx_get_module(const struct ly_ctx *ctx, const char *name, const char *revision)
{
    LY_CHECK_ARG_RET(ctx, ctx, name, NULL);
    return ly_ctx_get_module_by(ctx, name, offsetof(struct lysp_module, name), revision);
}

/**
 * @brief Unifying function for ly_ctx_get_module_latest() and ly_ctx_get_module_latest_ns()
 * @param[in] ctx Context where to search.
 * @param[in] key Name or Namespace as a search key.
 * @param[in] key_offset Key's offset in struct lysp_module to get value from the context's modules to match with the key.
 * @return Matching module if any.
 */
static const struct lys_module *
ly_ctx_get_module_latest_by(const struct ly_ctx *ctx, const char *key, size_t key_offset)
{
    const struct lys_module *mod;
    unsigned int index = 0;

    while ((mod = ly_ctx_get_module_by_iter(ctx, key, key_offset, &index))) {
        if ((mod->compiled && mod->compiled->latest_revision) || (!mod->compiled && mod->parsed->latest_revision)) {
            return mod;
        }
    }

    return NULL;
}

API const struct lys_module *
ly_ctx_get_module_latest(const struct ly_ctx *ctx, const char *name)
{
    LY_CHECK_ARG_RET(ctx, ctx, name, NULL);
    return ly_ctx_get_module_latest_by(ctx, name, offsetof(struct lysp_module, name));
}

const struct lys_module *
ly_ctx_get_module_latest_ns(const struct ly_ctx *ctx, const char *ns)
{
    LY_CHECK_ARG_RET(ctx, ctx, ns, NULL);
    return ly_ctx_get_module_latest_by(ctx, ns, offsetof(struct lysp_module, ns));
}

/**
 * @brief Unifying function for ly_ctx_get_module_implemented() and ly_ctx_get_module_implemented_ns()
 * @param[in] ctx Context where to search.
 * @param[in] key Name or Namespace as a search key.
 * @param[in] key_offset Key's offset in struct lysp_module to get value from the context's modules to match with the key.
 * @return Matching module if any.
 */
static const struct lys_module *
ly_ctx_get_module_implemented_by(const struct ly_ctx *ctx, const char *key, size_t key_offset)
{
    const struct lys_module *mod;
    unsigned int index = 0;

    while ((mod = ly_ctx_get_module_by_iter(ctx, key, key_offset, &index))) {
        if ((mod->compiled && mod->compiled->implemented) || (!mod->compiled && mod->parsed->implemented)) {
            return mod;
        }
    }

    return NULL;
}

API const struct lys_module *
ly_ctx_get_module_implemented(const struct ly_ctx *ctx, const char *name)
{
    LY_CHECK_ARG_RET(ctx, ctx, name, NULL);
    return ly_ctx_get_module_implemented_by(ctx, name, offsetof(struct lysp_module, name));
}

API const struct lys_module *
ly_ctx_get_module_implemented_ns(const struct ly_ctx *ctx, const char *ns)
{
    LY_CHECK_ARG_RET(ctx, ctx, ns, NULL);
    return ly_ctx_get_module_implemented_by(ctx, ns, offsetof(struct lysp_module, ns));
}

struct lysp_module *
ly_ctx_get_submodule(const struct ly_ctx *ctx, const char *module, const char *submodule, const char *revision)
{
    const struct lys_module *mod;
    struct lysp_include *inc;
    unsigned int v, u;

    assert(submodule);

    for (v = 0; v < ctx->list.count; ++v) {
        mod = ctx->list.objs[v];
        if (!mod->parsed) {
            continue;
        }
        if (module && strcmp(module, mod->parsed->name)) {
            continue;
        }

        LY_ARRAY_FOR(mod->parsed->includes, u) {
            if (mod->parsed->includes[u].submodule && !strcmp(submodule, mod->parsed->includes[u].submodule->name)) {
                inc = &mod->parsed->includes[u];
                if (!revision) {
                    if (inc->submodule->latest_revision) {
                        return inc->submodule;
                    }
                } else if (inc->submodule->revs && !strcmp(revision, inc->submodule->revs[0].date)) {
                    return inc->submodule;
                }
            }
        }
    }

    return NULL;
}

API void
ly_ctx_reset_latests(struct ly_ctx *ctx)
{
    unsigned int u,v ;
    struct lys_module *mod;

    for (u = 0; u < ctx->list.count; ++u) {
        mod = ctx->list.objs[u];
        if (mod->compiled && mod->compiled->latest_revision == 2) {
            mod->compiled->latest_revision = 1;
        }
        if (mod->parsed) {
            if (mod->parsed->latest_revision == 2) {
                mod->parsed->latest_revision = 1;
            }
            if (mod->parsed->includes) {
                for (v = 0; v < LY_ARRAY_SIZE(mod->parsed->includes); ++v) {
                    if (mod->parsed->includes[v].submodule->latest_revision == 2) {
                        mod->parsed->includes[v].submodule->latest_revision = 1;
                    }
                }
            }
        }
    }
}

API void
ly_ctx_destroy(struct ly_ctx *ctx, void (*private_destructor)(const struct lysc_node *node, void *priv))
{
    if (!ctx) {
        return;
    }

    /* models list */
    for (; ctx->list.count; ctx->list.count--) {
        /* remove the module */
        lys_module_free(ctx->list.objs[ctx->list.count - 1], private_destructor);
    }
    free(ctx->list.objs);

    /* search paths list */
    ly_set_erase(&ctx->search_paths, free);

    /* clean the error list */
    ly_err_clean(ctx, 0);
    pthread_key_delete(ctx->errlist_key);

    /* dictionary */
    lydict_clean(&ctx->dict);

#if 0 /* TODO when plugins implemented */
    /* plugins - will be removed only if this is the last context */
    ly_clean_plugins();
#endif

    free(ctx);
}
