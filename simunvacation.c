/*
 * Copyright (c) Regents of The University of Michigan
 * See COPYING.
 */

#include <config.h>

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/time.h>
#include <syslog.h>
#include <unistd.h>

#include "simvacation.h"
#include "vdb.h"
#include "vlu.h"
#include "vutil.h"

void usage(void);

int
main(int argc, char **argv) {
    int          debug = 0;
    extern int   optind, opterr;
    extern char *optarg;
    char         ch;

    struct vlu_backend *vlu;
    VLU                *vluh = NULL;
    struct vdb_backend *vdb;
    VDB                *vdbh = NULL;

    char *config_file = NULL;

    yastr               uniqname;
    ucl_object_iter_t   i;
    const ucl_object_t *uniqnames;
    const ucl_object_t *obj;

    while ((ch = getopt(argc, argv, "c:d")) != EOF) {
        switch ((char)ch) {

        case 'c':
            config_file = optarg;
            break;

        case 'd':
            debug = 1;
            break;

        case '?':
        default:
            usage();
        }
    }

    if (debug) {
        openlog("simunvacation", LOG_NOWAIT | LOG_PERROR | LOG_PID,
                LOG_VACATION);
    } else {
        openlog("simunvacation", LOG_PID, LOG_VACATION);
    }

    if (read_vacation_config(config_file) != VAC_RESULT_OK) {
        exit(1);
    }

    if ((vdb = vdb_backend(ucl_object_tostring(
                 ucl_object_lookup_path(vac_config, "core.vdb")))) == NULL) {
        exit(1);
    }

    if ((vdbh = vdb->init("simunvacation")) == NULL) {
        exit(1);
    }

    uniqnames = vdb->get_names(vdbh);

    if ((vlu = vlu_backend(ucl_object_tostring(
                 ucl_object_lookup_path(vac_config, "core.vlu")))) == NULL) {
        vdb->close(vdbh);
        exit(1);
    }

    if ((vluh = vlu->init()) == NULL) {
        vdb->close(vdbh);
        exit(1);
    }

    i = ucl_object_iterate_new(uniqnames);
    while ((obj = ucl_object_iterate_safe(i, false)) != NULL) {
        uniqname = yaslauto(ucl_object_tostring(obj));
        switch (vlu->search(vluh, uniqname)) {
        case VAC_RESULT_PERMFAIL:
            syslog(LOG_INFO, "cleaning up %s", uniqname);
            vdb->clean(vdbh, uniqname);
            break;
        case VAC_RESULT_TEMPFAIL:
            syslog(LOG_ERR, "lookup error processing %s", uniqname);
            break;
        default:
            syslog(LOG_DEBUG, "leaving %s alone", uniqname);
            break;
        }
        yaslfree(uniqname);
    }

    /* Vacuum the database. */
    vdb->gc(vdbh);

    vdb->close(vdbh);
    vlu->close(vluh);
    exit(0);
}

void
usage(void) {
    fprintf(stderr, "usage: simunvacation [-c config_file] [-d]\n");
    exit(1);
}
