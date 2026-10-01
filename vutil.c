/*
 * Copyright (c) Regents of The University of Michigan
 * See COPYING.
 */

#include <config.h>

#include <sysexits.h>
#include <syslog.h>
#include <unistd.h>

#include "embedded_config.h"
#include "simvacation.h"
#include "vutil.h"

ucl_object_t *vac_config = NULL;

vac_result
read_vacation_config(const char *config_file) {
    struct ucl_parser *parser;

    parser = ucl_parser_new(
            UCL_PARSER_KEY_LOWERCASE | UCL_PARSER_NO_IMPLICIT_ARRAYS);

    if (!ucl_parser_add_string(parser, SIMVACATION_CONFIG_BASE, 0)) {
        syslog(LOG_ERR, "vacation_config: base UCL parsing failed");
        return VAC_RESULT_TEMPFAIL;
    }

    vac_config = ucl_parser_get_object(parser);
    ucl_parser_free(parser);

    if (config_file == NULL) {
        config_file = "/etc/mail/simvacation.conf";
        if (access(config_file, F_OK) != 0) {
            syslog(LOG_INFO, "skipping config file loading, %s doesn't exist",
                    config_file);
            return VAC_RESULT_OK;
        }
    }

    parser = ucl_parser_new(
            UCL_PARSER_KEY_LOWERCASE | UCL_PARSER_NO_IMPLICIT_ARRAYS);
    ucl_parser_set_filevars(parser, config_file, false);
    if (!ucl_parser_add_file(parser, config_file)) {
        syslog(LOG_ERR, "vacation_config: UCL parsing failed");
        return VAC_RESULT_TEMPFAIL;
    }

    ucl_object_merge(vac_config, ucl_parser_get_object(parser), false);
    ucl_parser_free(parser);

    return VAC_RESULT_OK;
}

vac_result
pexecv(yastr *argv) {
    yastr binary;
    char *p;
    int   fd[ 2 ];

    binary = yasldup(argv[ 0 ]);
    if ((p = strrchr(argv[ 0 ], '/')) != NULL) {
        yaslrange(argv[ 0 ], p - argv[ 0 ] + 1, -1);
    }

    if (pipe(fd) < 0) {
        return VAC_RESULT_TEMPFAIL;
    }

    switch (fork()) {
    case -1:
        return VAC_RESULT_TEMPFAIL;

    case 0:
        if (close(fd[ 1 ]) < 0) {
            exit(EX_TEMPFAIL);
        }
        if (dup2(fd[ 0 ], 0) < 0) {
            exit(EX_TEMPFAIL);
        }
        if (close(fd[ 0 ]) < 0) {
            exit(EX_TEMPFAIL);
        }
        execv(binary, argv);
        exit(EX_TEMPFAIL);

    default:
        if (close(fd[ 0 ]) < 0) {
            return VAC_RESULT_TEMPFAIL;
        }
        if (dup2(fd[ 1 ], 1) < 0) {
            return VAC_RESULT_TEMPFAIL;
        }
        if (close(fd[ 1 ]) < 0) {
            return VAC_RESULT_TEMPFAIL;
        }
    }
    return VAC_RESULT_OK;
}

bool
is_substring(const char *n, const char *h, bool name) {
    bool  ret = false;
    yastr needle;
    yastr haystack;

    needle = yaslauto(n);
    yasltolower(needle);

    haystack = yaslauto(h);
    yasltolower(haystack);
    if (name) {
        yaslmapchars(haystack, "._", "  ", 2);
    }

    if (strstr(haystack, needle) != NULL) {
        ret = true;
    }

    yaslfree(needle);
    yaslfree(haystack);
    return ret;
}


yastr
canon_from(const yastr from) {
    yastr buf;
    yastr a;
    char *p;
    char *sep;

    a = yasldup(from);

    /* Canonicalize SRS addresses. We don't need to verify hashes and timestamps
     * because this doesn't increase our risk of replying to a forged address.
     */
    if (*a == '"') {
        p = a + 1;
    } else {
        p = a;
    }
    if ((strncasecmp(p, "SRS", 3) == 0) && (yasllen(a) > 13) &&
            ((p[ 3 ] == '0') || (p[ 3 ] == '1')) &&
            ((p[ 4 ] == '=') || (p[ 4 ] == '-') || (p[ 4 ] == '+'))) {

        if (*a == '"') {
            yaslrange(a, 1, -1);
            buf = yaslauto("\"");
        } else {
            buf = yaslempty();
        }

        if (a[ 3 ] == '1') {
            yaslrange(a, 5, -1);
            if (((p = strstr(a, "==")) != NULL) ||
                    ((p = strstr(a, "=-")) != NULL) ||
                    ((p = strstr(a, "=+")) != NULL)) {
                yaslrange(a, (p - a) + 2, -1);
            }
        } else {
            yaslrange(a, 5, -1);
        }

        /* Skip the hash and timestamp */
        if (yasllen(a) && ((p = strchr(a, '=')) != NULL) &&
                ((p = strchr(p + 1, '=')) != NULL)) {
            yaslrange(a, (p - a) + 1, -1);
        }

        /* a is now domain.tld=localpart@forwarder.tld
         * or the address is borked */
        if (yasllen(a) && ((sep = strchr(a, '=')) != NULL) &&
                ((p = strrchr(sep, '@')) != NULL)) {
            buf = yaslcatlen(buf, sep + 1, p - sep - 1);
            buf = yaslcat(buf, "@");
            buf = yaslcatlen(buf, a, sep - a);
        }

        if (yasllen(buf) > 1) {
            syslog(LOG_NOTICE, "check_from: corrected for SRS: %s", buf);
            a = buf;
        } else {
            a = yasldup(from);
        }
    }

    /* Canonicalize BATV addresses */
    if ((strncasecmp(a, "prvs=", 5) == 0) &&
            ((p = strchr(a + 5, '=')) != NULL)) {
        yaslrange(a, p - a + 1, -1);
        syslog(LOG_NOTICE, "check_from: corrected for BATV: %s", a);
    }

    /* Canonicalize bastardized BATV addresses */
    if ((strncasecmp(a, "btv1==", 6) == 0) &&
            ((p = strstr(a + 6, "==")) != NULL)) {
        yaslrange(a, p - a + 2, -1);
        syslog(LOG_NOTICE, "check_from: corrected for Barracuda: %s", a);
    }

    return a;
}

/*
 * RFC 3834 2
 *  Responders MUST NOT generate any response for which the
 *  destination of that response would be a null address (e.g., an
 *  address for which SMTP MAIL FROM or Return-Path is <>), since the
 *  response would not be delivered to a useful destination.
 *  Responders MAY refuse to generate responses for addresses commonly
 *  used as return addresses by responders - e.g., those with local-
 *  parts matching "owner-*", "*-request", "MAILER-DAEMON", etc.
 */
yastr
check_from(const yastr from) {
    static struct ignore {
        char *name;
        int   len;
    } ignore[] = {
            {"-errors", 7},
            {"-request", 8},
            {"postmaster", 10},
            {"mailer-daemon", 13},
            {"mailer", 6},
            {"-relay", 6},
            {"<>", 2},
            {NULL, 0},
    };
    struct ignore *cur;
    size_t         len;
    yastr          a;
    yastr          cfrom;
    char          *p;

    cfrom = canon_from(from);
    a = yasldup(cfrom);

    /* Chop off the domain */
    if ((p = strrchr(a, '@')) != NULL) {
        yaslrange(a, 0, p - a - 1);
    }

    yasltrim(a, "\"");
    yasltolower(a);
    len = yasllen(a);
    p = a + len;

    for (cur = ignore; cur->name; cur++) {
        /* This is a bit convoluted because we're matching the end of
         * the string.
         */
        if (len >= cur->len && memcmp(cur->name, p - cur->len, cur->len) == 0) {
            return NULL;
        }
    }

    return cfrom;
}
