/*
 * Copyright (c) Regents of The University of Michigan
 * See COPYING.
 */

#include <config.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sysexits.h>
#include <syslog.h>
#include <time.h>

#include "simvacation.h"
#include "vlu.h"

static bool       ldap_vlu_onvacation(VLU *);
static vac_result ldap_vlu_search_common(VLU *, const char *, const char *);
static time_t     ldap_vlu_time(struct berval *);

VLU *
ldap_vlu_init() {
    VLU          *vlu = NULL;
    const char   *uri = NULL;
    int           protocol = LDAP_VERSION3;
    struct berval credentials = {0};
    int           rc;

    if ((vlu = vlu_init()) == NULL) {
        return NULL;
    }

    if ((vlu->ldap = calloc(1, sizeof(struct vlu_ldap))) == NULL) {
        syslog(LOG_ERR, "vlu_init: calloc error: %m");
        return NULL;
    }

    if (!ucl_object_tostring_safe(
                ucl_object_lookup_path(vac_config, "ldap.search_base"),
                &(vlu->ldap->search_base))) {
        syslog(LOG_ERR, "vlu_init: ucl_object_tostring_safe failed");
        return NULL;
    }

    if (!ucl_object_tostring_safe(
                ucl_object_lookup_path(vac_config, "ldap.group_search_base"),
                &(vlu->ldap->group_search_base))) {
        syslog(LOG_ERR, "vlu_init: ucl_object_tostring_safe failed");
        return NULL;
    }

    if (!ucl_object_tostring_safe(
                ucl_object_lookup_path(vac_config, "ldap.attributes.vacation"),
                &(vlu->ldap->attr_vacation))) {
        syslog(LOG_ERR, "vlu_init: ucl_object_tostring_safe failed");
        return NULL;
    }

    if (!ucl_object_tostring_safe(ucl_object_lookup_path(vac_config,
                                          "ldap.attributes.autoreply_start"),
                &(vlu->ldap->attr_autoreply_start))) {
        syslog(LOG_ERR, "vlu_init: ucl_object_tostring_safe failed");
        return NULL;
    }

    if (!ucl_object_tostring_safe(ucl_object_lookup_path(vac_config,
                                          "ldap.attributes.autoreply_end"),
                &(vlu->ldap->attr_autoreply_end))) {
        syslog(LOG_ERR, "vlu_init: ucl_object_tostring_safe failed");
        return NULL;
    }


    vlu->ldap->default_msg = yaslauto(ucl_object_tostring(
            ucl_object_lookup_path(vac_config, "core.default_message")));

    if (!ucl_object_tostring_safe(ucl_object_lookup_path(vac_config,
                                          "ldap.attributes.vacation_message"),
                &(vlu->ldap->attr_vacation_msg))) {
        syslog(LOG_ERR, "vlu_init: ucl_object_tostring_safe failed");
        return NULL;
    }

    if (!ucl_object_tostring_safe(ucl_object_lookup_path(vac_config,
                                          "ldap.attributes.group_message"),
                &(vlu->ldap->attr_group_msg))) {
        syslog(LOG_ERR, "vlu_init: ucl_object_tostring_safe failed");
        return NULL;
    }


    if (!ucl_object_tostring_safe(
                ucl_object_lookup_path(vac_config, "ldap.attributes.name"),
                &(vlu->ldap->attr_name))) {
        syslog(LOG_ERR, "vlu_init: ucl_object_tostring_safe failed");
        return NULL;
    }

    vlu->ldap->attrs = calloc(8, sizeof(char *));
    vlu->ldap->attrs[ 0 ] = strdup("cn");
    vlu->ldap->attrs[ 1 ] = strdup(vlu->ldap->attr_vacation);
    vlu->ldap->attrs[ 2 ] = strdup(vlu->ldap->attr_vacation_msg);
    vlu->ldap->attrs[ 3 ] = strdup(vlu->ldap->attr_name);
    vlu->ldap->attrs[ 4 ] = strdup(vlu->ldap->attr_group_msg);
    vlu->ldap->attrs[ 5 ] = strdup(vlu->ldap->attr_autoreply_start);
    vlu->ldap->attrs[ 6 ] = strdup(vlu->ldap->attr_autoreply_end);

    vlu->ldap->timeout.tv_sec = 30;

    if (!ucl_object_tostring_safe(
                ucl_object_lookup_path(vac_config, "ldap.uri"), &uri)) {
        syslog(LOG_ERR, "vlu_init: ucl_object_tostring_safe failed");
        return NULL;
    }

    if (ldap_initialize(&(vlu->ldap->ld), uri) != LDAP_SUCCESS) {
        syslog(LOG_INFO, "ldap: ldap_initialize failed");
        return NULL;
    }

    if (ldap_set_option(vlu->ldap->ld, LDAP_OPT_PROTOCOL_VERSION, &protocol) !=
            LDAP_OPT_SUCCESS) {
        syslog(LOG_ALERT, "ldap: ldap_set_option failed");
        return NULL;
    }

    if ((rc = ldap_sasl_bind_s(vlu->ldap->ld, NULL, LDAP_SASL_SIMPLE,
                 &credentials, NULL, NULL, NULL)) != LDAP_SUCCESS) {
        syslog(LOG_ALERT, "vlu_connect: ldap_sasl_bind_s failed: %s",
                ldap_err2string(rc));
        return NULL;
    }

    return vlu;
}

static time_t
ldap_vlu_time(struct berval *bv_time) {
    yastr     buf;
    time_t    retval = 0;
    struct tm tm_time;
    char     *tz;

    memset(&tm_time, 0, sizeof(struct tm));
    buf = yaslnew(bv_time->bv_val, bv_time->bv_len);

    if ((strptime(buf, "%Y%m%d%H%M%S", &tm_time) != NULL) ||
            (strptime(buf, "%Y%m%d%H%M", &tm_time) != NULL) ||
            (strptime(buf, "%Y%m%d%H", &tm_time) != NULL)) {
        /* We're going to assume that everything is UTC, as the gods intended.*/
        if ((tz = getenv("TZ")) != NULL) {
            tz = strdup(tz);
        }
        setenv("TZ", "", 1);
        tzset();
        retval = mktime(&tm_time);
        if (tz) {
            setenv("TZ", tz, 1);
            free(tz);
        } else {
            unsetenv("TZ");
        }
        tzset();
    } else {
        syslog(LOG_ERR, "vlu_ldap: strptime unable to parse %s", buf);
    }

    yaslfree(buf);
    return retval;
}


static bool
ldap_vlu_onvacation(VLU *vlu) {
    bool            retval = false;
    struct berval **bv_status;
    time_t          time;
    struct timespec ts_now;
#ifdef CLOCK_REALTIME_COARSE
    clockid_t clock = CLOCK_REALTIME_COARSE;
#else
    clockid_t clock = CLOCK_REALTIME;
#endif /* CLOCK_REALTIME_COARSE */

    bv_status = ldap_get_values_len(
            vlu->ldap->ld, vlu->ldap->result, vlu->ldap->attr_vacation);

    if (bv_status) {
        if ((bv_status[ 0 ]->bv_len == 4) &&
                (strncasecmp(bv_status[ 0 ]->bv_val, "TRUE", 4) == 0)) {
            retval = true;
        }
        ldap_value_free_len(bv_status);
    }

    if (clock_gettime(clock, &ts_now) != 0) {
        syslog(LOG_ALERT, "vlu_ldap: clock_gettime failed: %s",
                strerror(errno));
        return false;
    }

    if (!retval) {
        bv_status = ldap_get_values_len(vlu->ldap->ld, vlu->ldap->result,
                vlu->ldap->attr_autoreply_start);
        if (bv_status) {
            time = ldap_vlu_time(bv_status[ 0 ]);
            ldap_value_free_len(bv_status);
            if (time > 0 && ts_now.tv_sec > time) {
                retval = true;
            }
        }
    }

    if (retval) {
        bv_status = ldap_get_values_len(vlu->ldap->ld, vlu->ldap->result,
                vlu->ldap->attr_autoreply_end);
        if (bv_status) {
            time = ldap_vlu_time(bv_status[ 0 ]);
            ldap_value_free_len(bv_status);
            if (time > 0 && ts_now.tv_sec > time) {
                retval = false;
            }
        }
    }

    return retval;
}


static vac_result
ldap_vlu_search_common(VLU *vlu, const char *filter, const char *search_base) {
    int          rc, matches;
    int          retval = VAC_RESULT_OK;
    LDAPMessage *result;

    rc = ldap_search_ext_s(vlu->ldap->ld, search_base, LDAP_SCOPE_SUBTREE,
            filter, vlu->ldap->attrs, 0, NULL, NULL, &vlu->ldap->timeout, 0,
            &result);

    switch (rc) {
    case LDAP_SUCCESS:
        break;
    case LDAP_SERVER_DOWN:
    case LDAP_TIMEOUT:
    case LDAP_BUSY:
    case LDAP_UNAVAILABLE:
    case LDAP_OTHER:
    case LDAP_LOCAL_ERROR:
        retval = VAC_RESULT_TEMPFAIL;
        break;
    default:
        retval = VAC_RESULT_PERMFAIL;
    }

    if (retval != VAC_RESULT_OK) {
        syslog(LOG_ALERT, "vlu_search_common: ldap_search failed: %s",
                ldap_err2string(rc));
        return retval;
    }

    matches = ldap_count_entries(vlu->ldap->ld, result);

    if (matches > 1) {
        syslog(LOG_ALERT, "vlu_search_common: multiple matches for %s in %s",
                filter, search_base);
        return VAC_RESULT_PERMFAIL;
    } else if (matches == 0) {
        syslog(LOG_ALERT, "vlu_search_common: no match for %s in %s", filter,
                search_base);
        return VAC_RESULT_PERMFAIL;
    }

    vlu->ldap->result = ldap_first_entry(vlu->ldap->ld, result);

    return retval;
}


vac_result
ldap_vlu_search(VLU *vlu, const yastr rcpt) {
    char filter[ 64 ];
    int  retval;

    sprintf(filter, "uid=%s", rcpt);
    retval = ldap_vlu_search_common(vlu, filter, vlu->ldap->search_base);

    if (retval == VAC_RESULT_OK) {
        if (ldap_vlu_onvacation(vlu)) {
            vlu->ldap->attr_msg = vlu->ldap->attr_vacation_msg;
            vlu->ldap->subject_prefix = yaslauto(ucl_object_tostring(
                    ucl_object_lookup_path(vac_config, "core.subject_prefix")));
            vlu->ldap->interval = (time_t)ucl_object_todouble(
                    ucl_object_lookup_path(vac_config, "core.interval"));
            syslog(LOG_DEBUG, "vlu_search: user %s on vacation", rcpt);
        } else {
            syslog(LOG_INFO, "vlu_search: user %s is not on vacation", rcpt);
            retval = VAC_RESULT_PERMFAIL;
        }
    }

    return retval;
}

vac_result
ldap_vlu_group_search(VLU *vlu, const yastr rcpt) {
    yastr filter;
    int   retval;

    /* Replace space equivalent characters with spaces. */
    filter = yaslauto("cn=");
    filter = yaslcatyasl(filter, rcpt);
    yaslmapchars(filter, "._", "  ", 2);
    retval = ldap_vlu_search_common(vlu, filter, vlu->ldap->group_search_base);

    if (retval == VAC_RESULT_OK) {
        if (ldap_vlu_onvacation(vlu)) {
            vlu->ldap->attr_msg = vlu->ldap->attr_group_msg;
            vlu->ldap->default_msg =
                    yaslauto(ucl_object_tostring(ucl_object_lookup_path(
                            vac_config, "core.default_group_message")));
            vlu->ldap->subject_prefix =
                    yaslauto(ucl_object_tostring(ucl_object_lookup_path(
                            vac_config, "core.group_subject_prefix")));
            vlu->ldap->interval = (time_t)ucl_object_todouble(
                    ucl_object_lookup_path(vac_config, "core.group_interval"));
            syslog(LOG_DEBUG,
                    "vlu_group_search: group %s has autoreplies enabled", rcpt);
        } else {
            syslog(LOG_INFO, "vlu_group_search: group %s does not autoreply",
                    rcpt);
            retval = VAC_RESULT_PERMFAIL;
        }
    }

    return retval;
}


ucl_object_t *
ldap_vlu_aliases(VLU *vlu, const yastr rcpt) {
    struct berval **cnames;
    int             i;
    ucl_object_t   *result;

    cnames = ldap_get_values_len(vlu->ldap->ld, vlu->ldap->result, "cn");

    result = ucl_object_fromstring(rcpt);

    for (i = 0; cnames && cnames[ i ] != NULL; i++) {
        result = ucl_elt_append(
                result, ucl_object_fromlstring(
                                cnames[ i ]->bv_val, cnames[ i ]->bv_len));
    }

    return result;
}

yastr
ldap_vlu_message(VLU *vlu, const yastr rcpt) {
    struct berval **rawmsg;
    int             i;
    yastr           vacmsg;

    if ((rawmsg = ldap_get_values_len(vlu->ldap->ld, vlu->ldap->result,
                 vlu->ldap->attr_msg)) == NULL) {
        return vlu->ldap->default_msg;
    }

    vacmsg = yaslempty();

    for (i = 0; rawmsg[ i ] != NULL; i++) {
        vacmsg = yaslcatlen(vacmsg, rawmsg[ i ]->bv_val, rawmsg[ i ]->bv_len);
    }

    yaslmapchars(vacmsg, "$", "\n", 1);

    return vacmsg;
}

yastr
ldap_vlu_subject_prefix(VLU *vlu, const yastr rcpt) {
    return vlu->ldap->subject_prefix;
}

time_t
ldap_vlu_interval(VLU *vlu, const yastr rcpt) {
    return vlu->ldap->interval;
}

yastr
ldap_vlu_name(VLU *vlu, const yastr rcpt) {
    char  *dn;
    LDAPDN ldn = NULL;
    yastr  retval = NULL;

    dn = ldap_get_dn(vlu->ldap->ld, vlu->ldap->result);
    if (ldap_str2dn(dn, &ldn, LDAP_DN_FORMAT_LDAPV3) != LDAP_SUCCESS) {
        syslog(LOG_ERR,
                "Liberror: ldap_vlu_name ldap_str2dn: failed to parse %s", dn);
        retval = rcpt;
    } else {
        retval = yaslnew(
                (*ldn[ 0 ])->la_value.bv_val, (*ldn[ 0 ])->la_value.bv_len);
    }
    ldap_dnfree(ldn);
    ldap_memfree(dn);
    return (retval);
}

yastr
ldap_vlu_display_name(VLU *vlu, const yastr rcpt) {
    struct berval **name;
    LDAPDN          dn = NULL;

    if ((name = ldap_get_values_len(vlu->ldap->ld, vlu->ldap->result,
                 vlu->ldap->attr_name)) == NULL) {
        ldap_str2dn(ldap_get_dn(vlu->ldap->ld, vlu->ldap->result), &dn,
                LDAP_DN_FORMAT_LDAPV3);
        return (yaslnew(
                dn[ 0 ][ 0 ]->la_value.bv_val, dn[ 0 ][ 0 ]->la_value.bv_len));
    }
    return yaslnew(name[ 0 ]->bv_val, name[ 0 ]->bv_len);
}
