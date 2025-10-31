/* mmjsontransform.c
 * Transform dotted JSON keys into nested containers.
 *
 * Concurrency & Locking:
 * - Module stores only immutable per-instance configuration.
 * - Each worker operates on one message without shared mutable state,
 *   so no additional locking is required beyond rsyslog core guarantees.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *       -or-
 *       see COPYING.ASL20 in the source distribution
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "config.h"

#include <json.h>
#include <json_object_iterator.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "rsyslog.h"
#include "conf.h"
#include "errmsg.h"
#include "parserif.h"
#include "module-template.h"
#include "syslogd-types.h"

MODULE_TYPE_OUTPUT;
MODULE_TYPE_NOKEEP;
MODULE_CNFNAME("mmjsontransform")

DEF_OMOD_STATIC_DATA;

/**
 * @brief Per-action configuration shared across all workers.
 *
 * The descriptors identify the input JSON property to read and the
 * output slot that receives the transformed tree.  The structure is
 * immutable after instantiation, so workers can access it without
 * additional locking.
 */
typedef enum mmjsontransformMode {
    MMJSONTRANSFORM_MODE_UNFLATTEN = 0,
    MMJSONTRANSFORM_MODE_FLATTEN,
} mmjsontransformMode_t;

typedef struct jsontransformConflict {
    sbool occurred; /**< Whether a hierarchy conflict was detected. */
    char *detail; /**< Human-readable description of the conflict. */
} jsontransformConflict_t;

typedef struct _instanceData {
    msgPropDescr_t *inputProp;
    msgPropDescr_t *outputProp;
    mmjsontransformMode_t mode;
} instanceData;

/**
 * @brief Per-worker view of the action configuration.
 *
 * Workers only store a pointer to the shared instanceData so no
 * additional teardown is required when a worker exits.
 */
typedef struct wrkrInstanceData {
    instanceData *pData;
} wrkrInstanceData_t;

struct modConfData_s {
    rsconf_t *pConf;
};

static modConfData_t *loadModConf = NULL;
static modConfData_t *runModConf = NULL;

/**
 * @brief Insert a value using a dotted key path into a destination object.
 *
 * @param dest Destination JSON object that receives new containers.
 * @param path Dotted path expressed as ``segment.segment``.
 * @param value Ownership-transferred value inserted or merged at the leaf.
 * @param pConflict Optional flag that is set when incompatible hierarchies
 *        prevent inserting the value.
 */
static rsRetVal jsontransformInsertDotted(struct json_object *dest,
                                          const char *path,
                                          struct json_object *value,
                                          jsontransformConflict_t *conflict);
/**
 * @brief Recursively rewrite arbitrary JSON values, expanding dotted keys.
 */
static rsRetVal jsontransformRewriteValue(struct json_object *value,
                                          struct json_object **out,
                                          jsontransformConflict_t *conflict,
                                          const char *contextPath);
/**
 * @brief Rewrite a JSON object and merge dotted keys into nested containers.
 */
static rsRetVal jsontransformRewriteObject(struct json_object *src,
                                           struct json_object **out,
                                           jsontransformConflict_t *conflict,
                                           const char *contextPath);
/**
 * @brief Rewrite each element of a JSON array and propagate conflicts.
 */
static rsRetVal jsontransformRewriteArray(struct json_object *src,
                                          struct json_object **out,
                                          jsontransformConflict_t *conflict,
                                          const char *contextPath);
/**
 * @brief Merge ``source`` object members into ``dest`` respecting conflicts.
 */
static rsRetVal jsontransformMergeObjects(struct json_object *dest,
                                          struct json_object *source,
                                          jsontransformConflict_t *conflict,
                                          const char *contextPath);
/**
 * @brief Flatten an object by collapsing nested containers into dotted keys.
 */
static rsRetVal jsontransformFlattenObject(struct json_object *src,
                                           struct json_object **out,
                                           jsontransformConflict_t *conflict);
/**
 * @brief Flatten a JSON value, recursing into objects and arrays.
 */
static rsRetVal jsontransformFlattenValue(struct json_object *value,
                                          struct json_object **out,
                                          jsontransformConflict_t *conflict,
                                          const char *contextPath);
/**
 * @brief Flatten array elements and propagate conflicts.
 */
static rsRetVal jsontransformFlattenArray(struct json_object *src,
                                          struct json_object **out,
                                          jsontransformConflict_t *conflict,
                                          const char *contextPath);
/**
 * @brief Append flattened members of ``src`` into ``dest`` with optional prefix.
 */
static rsRetVal jsontransformFlattenInto(struct json_object *src,
                                         struct json_object *dest,
                                         const char *prefix,
                                         jsontransformConflict_t *conflict);
/**
 * @brief Record conflict details if not already set.
 */
static rsRetVal jsontransformConflictSet(jsontransformConflict_t *conflict, const char *fmt, ...);
/**
 * @brief Release any resources captured in the conflict descriptor.
 */
static void jsontransformConflictCleanup(jsontransformConflict_t *conflict);
/**
 * @brief Allocate a dotted path string combining prefix and segment.
 */
static rsRetVal jsontransformBuildPath(char **out, const char *prefix, const char *segment);

static struct cnfparamdescr actpdescr[] = {
    {"input", eCmdHdlrString, 0},
    {"output", eCmdHdlrString, 0},
    {"mode", eCmdHdlrString, 0},
};
static struct cnfparamblk actpblk = {CNFPARAMBLK_VERSION, sizeof(actpdescr) / sizeof(struct cnfparamdescr), actpdescr};

/**
 * @brief Compare two JSON values for semantic equality.
 */
static sbool jsontransformValuesEqual(struct json_object *lhs, struct json_object *rhs);

BEGINbeginCnfLoad
    CODESTARTbeginCnfLoad;
    loadModConf = pModConf;
    pModConf->pConf = pConf;
ENDbeginCnfLoad

BEGINendCnfLoad
    CODESTARTendCnfLoad;
ENDendCnfLoad

BEGINcheckCnf
    CODESTARTcheckCnf;
ENDcheckCnf

BEGINactivateCnf
    CODESTARTactivateCnf;
    runModConf = pModConf;
ENDactivateCnf

BEGINfreeCnf
    CODESTARTfreeCnf;
ENDfreeCnf

BEGINcreateInstance
    CODESTARTcreateInstance;
    pData->inputProp = NULL;
    pData->outputProp = NULL;
    pData->mode = MMJSONTRANSFORM_MODE_UNFLATTEN;
ENDcreateInstance

BEGINfreeInstance
    CODESTARTfreeInstance;
    if (pData->inputProp != NULL) {
        msgPropDescrDestruct(pData->inputProp);
        free(pData->inputProp);
    }
    if (pData->outputProp != NULL) {
        msgPropDescrDestruct(pData->outputProp);
        free(pData->outputProp);
    }
ENDfreeInstance

BEGINcreateWrkrInstance
    CODESTARTcreateWrkrInstance;
    pWrkrData->pData = pData;
ENDcreateWrkrInstance

BEGINfreeWrkrInstance
    CODESTARTfreeWrkrInstance;
ENDfreeWrkrInstance

BEGINisCompatibleWithFeature
    CODESTARTisCompatibleWithFeature;
ENDisCompatibleWithFeature

BEGINdbgPrintInstInfo
    CODESTARTdbgPrintInstInfo;
    DBGPRINTF("mmjsontransform\n");
ENDdbgPrintInstInfo

BEGINtryResume
    CODESTARTtryResume;
ENDtryResume

static sbool jsontransformValuesEqual(struct json_object *lhs, struct json_object *rhs) {
    if (lhs == rhs) {
        return 1;
    }
    if (lhs == NULL || rhs == NULL) {
        return 0;
    }

    enum json_type lhsType = json_object_get_type(lhs);
    enum json_type rhsType = json_object_get_type(rhs);
    if (lhsType != rhsType) {
        return 0;
    }

    switch (lhsType) {
        case json_type_object: {
            if (json_object_object_length(lhs) != json_object_object_length(rhs)) {
                return 0;
            }
            struct json_object_iterator it = json_object_iter_begin(lhs);
            struct json_object_iterator itEnd = json_object_iter_end(lhs);
            for (; !json_object_iter_equal(&it, &itEnd); json_object_iter_next(&it)) {
                const char *name = json_object_iter_peek_name(&it);
                struct json_object *lhsVal = json_object_iter_peek_value(&it);
                struct json_object *rhsVal;
                if (!json_object_object_get_ex(rhs, name, &rhsVal)) {
                    return 0;
                }
                if (!jsontransformValuesEqual(lhsVal, rhsVal)) {
                    return 0;
                }
            }
            return 1;
        }
        case json_type_array: {
            int len = json_object_array_length(lhs);
            if (len != json_object_array_length(rhs)) {
                return 0;
            }
            for (int i = 0; i < len; ++i) {
                if (!jsontransformValuesEqual(json_object_array_get_idx(lhs, i), json_object_array_get_idx(rhs, i))) {
                    return 0;
                }
            }
            return 1;
        }
        case json_type_string:
            return strcmp(json_object_get_string(lhs), json_object_get_string(rhs)) == 0;
        case json_type_int:
            return json_object_get_int64(lhs) == json_object_get_int64(rhs);
#ifdef json_type_uint64
        case json_type_uint64:
            return json_object_get_uint64(lhs) == json_object_get_uint64(rhs);
#endif
        case json_type_double:
            return json_object_get_double(lhs) == json_object_get_double(rhs);
        case json_type_boolean:
            return json_object_get_boolean(lhs) == json_object_get_boolean(rhs);
        case json_type_null:
            return 1;
        default:
            return strcmp(json_object_get_string(lhs), json_object_get_string(rhs)) == 0;
    }
}

BEGINnewActInst
    struct cnfparamvals *pvals;
    int i;
    CODESTARTnewActInst;
    if ((pvals = nvlstGetParams(lst, &actpblk, NULL)) == NULL) {
        ABORT_FINALIZE(RS_RET_MISSING_CNFPARAMS);
    }

    CODE_STD_STRING_REQUESTnewActInst(1);
    CHKiRet(OMSRsetEntry(*ppOMSR, 0, NULL, OMSR_TPL_AS_MSG));
    CHKiRet(createInstance(&pData));

    for (i = 0; i < actpblk.nParams; ++i) {
        if (!pvals[i].bUsed) continue;
        if (!strcmp(actpblk.descr[i].name, "input")) {
            char *name = es_str2cstr(pvals[i].val.d.estr, NULL);
            if (name == NULL) {
                ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
            }
            if (name[0] != '$') {
                parser_errmsg("mmjsontransform: input name '%s' must start with '$'", name);
                free(name);
                ABORT_FINALIZE(RS_RET_INVALID_VAR);
            }
            if (pData->inputProp == NULL) {
                CHKmalloc(pData->inputProp = malloc(sizeof(msgPropDescr_t)));
            }
            CHKiRet(msgPropDescrFill(pData->inputProp, (uchar *)name, strlen(name)));
            free(name);
        } else if (!strcmp(actpblk.descr[i].name, "output")) {
            char *name = es_str2cstr(pvals[i].val.d.estr, NULL);
            if (name == NULL) {
                ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
            }
            if (name[0] == '$') {
                memmove(name, name + 1, strlen(name));
            }
            if (name[0] != '!' && name[0] != '.' && name[0] != '/') {
                parser_errmsg("mmjsontransform: output name '%s' must start with '$!', '$.' or '$/'", name);
                free(name);
                ABORT_FINALIZE(RS_RET_INVALID_VAR);
            }
            if (pData->outputProp == NULL) {
                CHKmalloc(pData->outputProp = malloc(sizeof(msgPropDescr_t)));
            }
            CHKiRet(msgPropDescrFill(pData->outputProp, (uchar *)name, strlen(name)));
            free(name);
        } else if (!strcmp(actpblk.descr[i].name, "mode")) {
            char *mode = es_str2cstr(pvals[i].val.d.estr, NULL);
            if (mode == NULL) {
                ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
            }
            if (strcasecmp(mode, "unflatten") == 0 || mode[0] == '\0') {
                pData->mode = MMJSONTRANSFORM_MODE_UNFLATTEN;
            } else if (strcasecmp(mode, "flatten") == 0) {
                pData->mode = MMJSONTRANSFORM_MODE_FLATTEN;
            } else {
                parser_errmsg("mmjsontransform: mode '%s' is invalid; use 'unflatten' or 'flatten'", mode);
                free(mode);
                ABORT_FINALIZE(RS_RET_INVALID_PARAMS);
            }
            free(mode);
        } else {
            dbgprintf("mmjsontransform: unexpected parameter '%s'\n", actpblk.descr[i].name);
        }
    }

    if (pData->inputProp == NULL || pData->outputProp == NULL) {
        parser_errmsg("mmjsontransform: both 'input' and 'output' parameters are required");
        ABORT_FINALIZE(RS_RET_MISSING_CNFPARAMS);
    }

    CODE_STD_FINALIZERnewActInst;
    cnfparamvalsDestruct(pvals, &actpblk);
ENDnewActInst

BEGINdoAction_NoStrings
    smsg_t **ppMsg = (smsg_t **)pMsgData;
    smsg_t *pMsg = ppMsg[0];
    instanceData *pData = pWrkrData->pData;
    struct json_object *input = NULL;
    struct json_object *rewritten = NULL;
    jsontransformConflict_t conflict = {0, NULL};
    rsRetVal localRet;
    CODESTARTdoAction;

    localRet = msgGetJSONPropJSON(pMsg, pData->inputProp, &input);
    if (localRet == RS_RET_NOT_FOUND) {
        FINALIZE;
    } else if (localRet != RS_RET_OK) {
        ABORT_FINALIZE(localRet);
    }

    if (!json_object_is_type(input, json_type_object)) {
        LogError(0, RS_RET_INVLD_PROP, "mmjsontransform: input '%s' does not reference a JSON object",
                 pData->inputProp->name);
        ABORT_FINALIZE(RS_RET_INVLD_PROP);
    }

    localRet = msgCheckVarExists(pMsg, pData->outputProp);
    if (localRet == RS_RET_OK) {
        LogError(0, RS_RET_INVLD_SETOP, "mmjsontransform: output '%s' already exists", pData->outputProp->name);
        ABORT_FINALIZE(RS_RET_INVLD_SETOP);
    } else if (localRet != RS_RET_NOT_FOUND) {
        ABORT_FINALIZE(localRet);
    }

    if (pData->mode == MMJSONTRANSFORM_MODE_UNFLATTEN) {
        CHKiRet(jsontransformRewriteObject(input, &rewritten, &conflict, NULL));
    } else {
        CHKiRet(jsontransformFlattenObject(input, &rewritten, &conflict));
    }
    if (conflict.occurred) {
        if (conflict.detail != NULL) {
            LogError(0, RS_RET_INVLD_SETOP, "mmjsontransform: %s", conflict.detail);
        } else {
            LogError(0, RS_RET_INVLD_SETOP, "mmjsontransform: JSON hierarchy conflict while processing '%s'",
                     pData->inputProp->name);
        }
        ABORT_FINALIZE(RS_RET_INVLD_SETOP);
    }

    CHKiRet(msgAddJSON(pMsg, pData->outputProp->name, rewritten, 0, 0));
    rewritten = NULL;

finalize_it:
    if (input != NULL) json_object_put(input);
    if (rewritten != NULL) json_object_put(rewritten);
    jsontransformConflictCleanup(&conflict);
ENDdoAction

NO_LEGACY_CONF_parseSelectorAct

    BEGINmodExit CODESTARTmodExit;
ENDmodExit

BEGINqueryEtryPt
    CODESTARTqueryEtryPt;
    CODEqueryEtryPt_STD_OMOD_QUERIES;
    CODEqueryEtryPt_STD_OMOD8_QUERIES;
    CODEqueryEtryPt_STD_CONF2_OMOD_QUERIES;
    CODEqueryEtryPt_STD_CONF2_QUERIES;
ENDqueryEtryPt

BEGINmodInit()
    CODESTARTmodInit;
    *ipIFVersProvided = CURR_MOD_IF_VERSION;
    CODEmodInit_QueryRegCFSLineHdlr
ENDmodInit

/** \copydoc jsontransformInsertDotted */
static rsRetVal jsontransformInsertDotted(struct json_object *dest,
                                          const char *path,
                                          struct json_object *value,
                                          jsontransformConflict_t *conflict) {
    const char *segStart = path;
    const char *ptr = path;
    struct json_object *current = dest;
    char *segment = NULL;
    char *leaf = NULL;
    DEFiRet;

    while (*ptr != '\0') {
        if (*ptr == '.') {
            if (ptr == segStart) {
                CHKiRet(jsontransformConflictSet(conflict, "dotted path '%s' contains an empty segment", path));
                goto finalize_it;
            }
            const size_t segLen = (size_t)(ptr - segStart);
            segment = strndup(segStart, segLen);
            if (segment == NULL) {
                ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
            }
            struct json_object *child;
            if (json_object_object_get_ex(current, segment, &child)) {
                if (!json_object_is_type(child, json_type_object)) {
                    CHKiRet(jsontransformConflictSet(
                        conflict, "dotted path '%s' collides with existing non-object value at '%s'", path, segment));
                    goto finalize_it;
                }
                free(segment);
                segment = NULL;
            } else {
                child = json_object_new_object();
                if (child == NULL) {
                    free(segment);
                    segment = NULL;
                    ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
                }
                json_object_object_add(current, segment, child);
                free(segment);
                segment = NULL;
            }
            current = child;
            ++ptr;
            segStart = ptr;
            continue;
        }
        ++ptr;
    }

    if (*segStart == '\0') {
        CHKiRet(jsontransformConflictSet(conflict, "dotted path '%s' ends with an empty segment", path));
        goto finalize_it;
    }

    leaf = strdup(segStart);
    if (leaf == NULL) {
        ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
    }
    struct json_object *existing;
    if (json_object_object_get_ex(current, leaf, &existing)) {
        if (!json_object_is_type(existing, json_type_object) || !json_object_is_type(value, json_type_object)) {
            CHKiRet(
                jsontransformConflictSet(conflict, "dotted path '%s' collides with existing non-object value", path));
            goto finalize_it;
        }
        CHKiRet(jsontransformMergeObjects(existing, value, conflict, path));
        json_object_put(value);
        goto finalize_it;
    }
    json_object_object_add(current, leaf, value);
    free(leaf);
    leaf = NULL;

finalize_it:
    if (segment != NULL) free(segment);
    if (leaf != NULL) free(leaf);
    RETiRet;
}

/** \copydoc jsontransformRewriteArray */
static rsRetVal jsontransformRewriteArray(struct json_object *src,
                                          struct json_object **out,
                                          jsontransformConflict_t *conflict,
                                          const char *contextPath) {
    struct json_object *dest = NULL;
    struct json_object *elem = NULL;
    char *elementContext = NULL;
    int i;
    DEFiRet;

    dest = json_object_new_array();
    if (dest == NULL) {
        ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
    }

    for (i = 0; i < json_object_array_length(src); ++i) {
        if (contextPath != NULL) {
            if (asprintf(&elementContext, "%s[%d]", contextPath, i) == -1) {
                elementContext = NULL;
                ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
            }
        } else {
            if (asprintf(&elementContext, "[%d]", i) == -1) {
                elementContext = NULL;
                ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
            }
        }
        CHKiRet(jsontransformRewriteValue(json_object_array_get_idx(src, i), &elem, conflict, elementContext));
        if (conflict != NULL && conflict->occurred) {
            goto conflict;
        }
        if (json_object_array_add(dest, elem) != 0) {
            json_object_put(elem);
            elem = NULL;
            ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
        }
        elem = NULL;
        free(elementContext);
        elementContext = NULL;
    }

    *out = dest;
    dest = NULL;
    goto finalize_it;

conflict:
    if (elementContext != NULL) {
        free(elementContext);
        elementContext = NULL;
    }
    if (elem != NULL) {
        json_object_put(elem);
        elem = NULL;
    }

finalize_it:
    if (dest != NULL) json_object_put(dest);
    RETiRet;
}

/** \copydoc jsontransformRewriteObject */
static rsRetVal jsontransformRewriteObject(struct json_object *src,
                                           struct json_object **out,
                                           jsontransformConflict_t *conflict,
                                           const char *contextPath) {
    struct json_object *dest = NULL;
    struct json_object *child = NULL;
    char *valueContext = NULL;
    DEFiRet;

    dest = json_object_new_object();
    if (dest == NULL) {
        ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
    }

    struct json_object_iterator it = json_object_iter_begin(src);
    struct json_object_iterator itEnd = json_object_iter_end(src);

    for (; !json_object_iter_equal(&it, &itEnd); json_object_iter_next(&it)) {
        const char *key = json_object_iter_peek_name(&it);
        struct json_object *value = json_object_iter_peek_value(&it);

        if (strchr(key, '.') != NULL) {
            valueContext = strdup(key);
            if (valueContext == NULL) {
                ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
            }
        } else {
            CHKiRet(jsontransformBuildPath(&valueContext, contextPath, key));
        }

        CHKiRet(jsontransformRewriteValue(value, &child, conflict, valueContext));
        if (conflict != NULL && conflict->occurred) {
            goto conflict;
        }
        if (strchr(key, '.') != NULL) {
            CHKiRet(jsontransformInsertDotted(dest, key, child, conflict));
            if (conflict != NULL && conflict->occurred) goto conflict;
            child = NULL;
            free(valueContext);
            valueContext = NULL;
        } else {
            struct json_object *existing;
            if (json_object_object_get_ex(dest, key, &existing)) {
                if (json_object_is_type(existing, json_type_object) && json_object_is_type(child, json_type_object)) {
                    CHKiRet(jsontransformMergeObjects(existing, child, conflict, valueContext));
                    if (conflict != NULL && conflict->occurred) goto conflict;
                    json_object_put(child);
                    child = NULL;
                    free(valueContext);
                    valueContext = NULL;
                    continue;
                }
                CHKiRet(jsontransformConflictSet(conflict, "object key '%s' collides with an incompatible value",
                                                 valueContext != NULL ? valueContext : key));
                goto conflict;
            }
            char *dupKey = strdup(key);
            if (dupKey == NULL) {
                ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
            }
            json_object_object_add(dest, dupKey, child);
            free(dupKey);
            child = NULL;
            free(valueContext);
            valueContext = NULL;
        }
    }

    *out = dest;
    dest = NULL;
    goto finalize_it;

conflict:
    if (child != NULL) {
        json_object_put(child);
        child = NULL;
    }
    if (valueContext != NULL) {
        free(valueContext);
        valueContext = NULL;
    }

finalize_it:
    if (dest != NULL) json_object_put(dest);
    RETiRet;
}

/** \copydoc jsontransformMergeObjects */
static rsRetVal jsontransformMergeObjects(struct json_object *dest,
                                          struct json_object *source,
                                          jsontransformConflict_t *conflict,
                                          const char *contextPath) {
    struct json_object_iterator it = json_object_iter_begin(source);
    struct json_object_iterator itEnd = json_object_iter_end(source);
    char *childPath = NULL;
    DEFiRet;

    for (; !json_object_iter_equal(&it, &itEnd); json_object_iter_next(&it)) {
        const char *name = json_object_iter_peek_name(&it);
        struct json_object *value = json_object_iter_peek_value(&it);
        struct json_object *existing;

        CHKiRet(jsontransformBuildPath(&childPath, contextPath, name));
        if (json_object_object_get_ex(dest, name, &existing)) {
            if (json_object_is_type(existing, json_type_object) && json_object_is_type(value, json_type_object)) {
                CHKiRet(jsontransformMergeObjects(existing, value, conflict, childPath));
                if (conflict != NULL && conflict->occurred) goto finalize_it;
                free(childPath);
                childPath = NULL;
                continue;
            }
            if (jsontransformValuesEqual(existing, value)) {
                free(childPath);
                childPath = NULL;
                continue;
            }
            CHKiRet(jsontransformConflictSet(conflict, "object key '%s' collides with an incompatible value",
                                             childPath != NULL ? childPath : name));
            goto finalize_it;
        }
        char *dupKey = strdup(name);
        if (dupKey == NULL) {
            ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
        }
        json_object_object_add(dest, dupKey, json_object_get(value));
        free(dupKey);
        free(childPath);
        childPath = NULL;
    }

finalize_it:
    if (childPath != NULL) {
        free(childPath);
        childPath = NULL;
    }
    RETiRet;
}

/** \copydoc jsontransformRewriteValue */
static rsRetVal jsontransformRewriteValue(struct json_object *value,
                                          struct json_object **out,
                                          jsontransformConflict_t *conflict,
                                          const char *contextPath) {
    struct json_object *result = NULL;
    enum json_type type;
    DEFiRet;

    type = json_object_get_type(value);
    if (type == json_type_object) {
        CHKiRet(jsontransformRewriteObject(value, &result, conflict, contextPath));
        if (conflict != NULL && conflict->occurred) goto finalize_it;
    } else if (type == json_type_array) {
        CHKiRet(jsontransformRewriteArray(value, &result, conflict, contextPath));
        if (conflict != NULL && conflict->occurred) goto finalize_it;
    } else {
        result = json_object_get(value);
    }

    *out = result;
    result = NULL;

finalize_it:
    if (result != NULL) json_object_put(result);
    RETiRet;
}

/** \copydoc jsontransformFlattenObject */
static rsRetVal jsontransformFlattenObject(struct json_object *src,
                                           struct json_object **out,
                                           jsontransformConflict_t *conflict) {
    struct json_object *dest = NULL;
    DEFiRet;

    dest = json_object_new_object();
    if (dest == NULL) {
        ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
    }

    CHKiRet(jsontransformFlattenInto(src, dest, NULL, conflict));
    if (conflict != NULL && conflict->occurred) goto finalize_it;

    *out = dest;
    dest = NULL;

finalize_it:
    if (dest != NULL) json_object_put(dest);
    RETiRet;
}

/** \copydoc jsontransformFlattenInto */
static rsRetVal jsontransformFlattenInto(struct json_object *src,
                                         struct json_object *dest,
                                         const char *prefix,
                                         jsontransformConflict_t *conflict) {
    struct json_object_iterator it = json_object_iter_begin(src);
    struct json_object_iterator itEnd = json_object_iter_end(src);
    struct json_object *flattened = NULL;
    char *childPath = NULL;
    DEFiRet;

    for (; !json_object_iter_equal(&it, &itEnd); json_object_iter_next(&it)) {
        const char *name = json_object_iter_peek_name(&it);
        struct json_object *value = json_object_iter_peek_value(&it);

        CHKiRet(jsontransformBuildPath(&childPath, prefix, name));
        if (json_object_is_type(value, json_type_object)) {
            CHKiRet(jsontransformFlattenInto(value, dest, childPath, conflict));
            if (conflict != NULL && conflict->occurred) goto finalize_it;
            free(childPath);
            childPath = NULL;
            continue;
        }

        CHKiRet(jsontransformFlattenValue(value, &flattened, conflict, childPath));
        if (conflict != NULL && conflict->occurred) goto finalize_it;

        struct json_object *existing;
        if (json_object_object_get_ex(dest, childPath, &existing)) {
            if (jsontransformValuesEqual(existing, flattened)) {
                json_object_put(flattened);
                flattened = NULL;
                free(childPath);
                childPath = NULL;
                continue;
            }
            CHKiRet(
                jsontransformConflictSet(conflict, "flattened key '%s' collides with an existing value", childPath));
            goto finalize_it;
        }

        json_object_object_add(dest, childPath, flattened);
        free(childPath);
        childPath = NULL;
        flattened = NULL;
    }

finalize_it:
    if (flattened != NULL) {
        json_object_put(flattened);
        flattened = NULL;
    }
    if (childPath != NULL) {
        free(childPath);
        childPath = NULL;
    }
    RETiRet;
}

/** \copydoc jsontransformFlattenValue */
static rsRetVal jsontransformFlattenValue(struct json_object *value,
                                          struct json_object **out,
                                          jsontransformConflict_t *conflict,
                                          const char *contextPath) {
    struct json_object *result = NULL;
    enum json_type type;
    DEFiRet;

    type = json_object_get_type(value);
    if (type == json_type_object) {
        result = json_object_new_object();
        if (result == NULL) {
            ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
        }
        CHKiRet(jsontransformFlattenInto(value, result, NULL, conflict));
        if (conflict != NULL && conflict->occurred) goto finalize_it;
    } else if (type == json_type_array) {
        CHKiRet(jsontransformFlattenArray(value, &result, conflict, contextPath));
        if (conflict != NULL && conflict->occurred) goto finalize_it;
    } else {
        result = json_object_get(value);
    }

    *out = result;
    result = NULL;

finalize_it:
    if (result != NULL) json_object_put(result);
    RETiRet;
}

/** \copydoc jsontransformFlattenArray */
static rsRetVal jsontransformFlattenArray(struct json_object *src,
                                          struct json_object **out,
                                          jsontransformConflict_t *conflict,
                                          const char *contextPath) {
    struct json_object *dest = NULL;
    struct json_object *elem = NULL;
    char *elementContext = NULL;
    int i;
    DEFiRet;

    dest = json_object_new_array();
    if (dest == NULL) {
        ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
    }

    for (i = 0; i < json_object_array_length(src); ++i) {
        if (contextPath != NULL) {
            if (asprintf(&elementContext, "%s[%d]", contextPath, i) == -1) {
                elementContext = NULL;
                ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
            }
        } else {
            if (asprintf(&elementContext, "[%d]", i) == -1) {
                elementContext = NULL;
                ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
            }
        }
        CHKiRet(jsontransformFlattenValue(json_object_array_get_idx(src, i), &elem, conflict, elementContext));
        if (conflict != NULL && conflict->occurred) {
            goto conflict;
        }
        if (json_object_array_add(dest, elem) != 0) {
            json_object_put(elem);
            elem = NULL;
            ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
        }
        elem = NULL;
        free(elementContext);
        elementContext = NULL;
    }

    *out = dest;
    dest = NULL;
    goto finalize_it;

conflict:
    if (elementContext != NULL) {
        free(elementContext);
        elementContext = NULL;
    }
    if (elem != NULL) {
        json_object_put(elem);
        elem = NULL;
    }

finalize_it:
    if (dest != NULL) json_object_put(dest);
    RETiRet;
}

/** \copydoc jsontransformConflictSet */
static rsRetVal jsontransformConflictSet(jsontransformConflict_t *conflict, const char *fmt, ...) {
    va_list ap;
    char *detail = NULL;

    if (conflict == NULL) {
        return RS_RET_OK;
    }
    if (conflict->occurred) {
        return RS_RET_OK;
    }

    conflict->occurred = 1;
    if (fmt == NULL) {
        conflict->detail = NULL;
        return RS_RET_OK;
    }

    va_start(ap, fmt);
    if (vasprintf(&detail, fmt, ap) == -1) {
        va_end(ap);
        conflict->detail = NULL;
        return RS_RET_OUT_OF_MEMORY;
    }
    va_end(ap);

    conflict->detail = detail;
    return RS_RET_OK;
}

/** \copydoc jsontransformConflictCleanup */
static void jsontransformConflictCleanup(jsontransformConflict_t *conflict) {
    if (conflict == NULL) {
        return;
    }
    if (conflict->detail != NULL) {
        free(conflict->detail);
        conflict->detail = NULL;
    }
    conflict->occurred = 0;
}

/** \copydoc jsontransformBuildPath */
static rsRetVal jsontransformBuildPath(char **out, const char *prefix, const char *segment) {
    if (out == NULL) {
        return RS_RET_OK;
    }
    if (*out != NULL) {
        free(*out);
        *out = NULL;
    }
    if (segment == NULL) {
        return RS_RET_OK;
    }
    if (prefix == NULL || prefix[0] == '\0') {
        *out = strdup(segment);
        if (*out == NULL) {
            return RS_RET_OUT_OF_MEMORY;
        }
    } else {
        if (asprintf(out, "%s.%s", prefix, segment) == -1) {
            *out = NULL;
            return RS_RET_OUT_OF_MEMORY;
        }
    }
    return RS_RET_OK;
}

/* vi:set ai: */
