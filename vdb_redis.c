/*
 * Copyright (c) Regents of The University of Michigan
 * See COPYING.
 */

#include <config.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sysexits.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "rabin.h"
#include "simvacation.h"
#include "vdb.h"

static yastr redis_vdb_key(const yastr, const yastr);

VDB *
redis_vdb_init(const yastr rcpt) {
    VDB        *vdb = NULL;
    VDB        *res = NULL;
    const char *host = "127.0.0.1";
    int64_t     port = 6379;

    if (!ucl_object_tostring_safe(
                ucl_object_lookup_path(vac_config, "redis.host"), &host)) {
        syslog(LOG_ERR, "vdb_init: ucl_object_tostring_safe failed");
        goto error;
    }
    if (!ucl_object_toint_safe(
                ucl_object_lookup_path(vac_config, "redis.port"), &port)) {
        syslog(LOG_ERR, "vdb_init: ucl_object_toint_safe failed");
        goto error;
    }

    if ((vdb = calloc(1, sizeof(VDB))) == NULL) {
        goto error;
    }

    if ((vdb->redis = urcl_connect(host, port)) == NULL) {
        syslog(LOG_ALERT, "redis vdb_init urcl_connect: failed");
        goto error;
    }

    vdb->rcpt = yasldup(rcpt);

    res = vdb;

error:
    if (vdb && !res) {
        redis_vdb_close(vdb);
    }

    return res;
}

void
redis_vdb_close(VDB *vdb) {
    if (vdb) {
        if (vdb->redis) {
            urcl_free(vdb->redis);
        }
        free(vdb);
    }
}

vdb_status
redis_vdb_recent(VDB *vdb, const yastr from, time_t interval) {
    int         retval = VDB_STATUS_OK;
    time_t      last, now;
    yastr       key;
    redisReply *res = NULL;

    if ((key = redis_vdb_key(vdb->rcpt, from)) == NULL) {
        return retval;
    }

    if (((res = urcl_command(vdb->redis, key, "GET %s", key)) == NULL) ||
            (res->type != REDIS_REPLY_STRING)) {
        goto cleanup;
    }

    last = (time_t)strtoll(res->str, NULL, 10);

    if ((now = time(NULL)) < 0) {
        syslog(LOG_ALERT, "redis vdb_recent time: %m");
    } else if (now < (last + interval)) {
        retval = VDB_STATUS_RECENT;
    } else {
        /* This shouldn't happen, so let's clean it up */
        urcl_free_result(urcl_command(vdb->redis, key, "DEL %s", key));
    }

cleanup:
    urcl_free_result(res);
    yaslfree(key);
    return retval;
}

vac_result
redis_vdb_store_reply(VDB *vdb, const yastr from) {
    time_t now;
    yastr  key;
    char   value[ 20 ];

    if ((now = time(NULL)) < 0) {
        syslog(LOG_ALERT, "redis vdb_store_reply time: %m");
        return VAC_RESULT_TEMPFAIL;
    }

    key = redis_vdb_key(vdb->rcpt, from);
    snprintf(value, 20, "%lld", (long long)now);

    urcl_free_result(urcl_command(vdb->redis, key, "SET %s %s", key, value));
    /* expire entries after 7 days. */
    urcl_free_result(urcl_command(vdb->redis, key, "EXPIRE %s 604800", key));

    return VAC_RESULT_OK;
}

static yastr
redis_vdb_key(const yastr rcpt, const yastr from) {
    yastr key = yaslauto("simvacation:user:");
    key = yaslcatprintf(
            key, "%s:%lx", rcpt, (long)rabin_fingerprint(from, yasllen(from)));
    syslog(LOG_DEBUG, "redis_vdb_key: %s", key);

    return key;
}
