#ifndef SIMVACATION_H
#define SIMVACATION_H

/*
 * simvacation.h
 *
 * Copyright (c) 2007, 2013 Regents of The University of Michigan.
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation, and that the name of The University
 * of Michigan not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior
 * permission. This software is supplied as is without expressed or
 * implied warranties of any kind.
 *
 */

#include <config.h>
#include "sds/sds.h"

#define HEADER_UNKNOWN      0
#define HEADER_RECIPIENT    1
#define HEADER_APPEND       2
#define HEADER_NOREPLY      3

#ifndef CONFFILE
#define CONFFILE "/etc/mail/simvacation.conf"
#endif

#ifndef _PATH_SENDMAIL
#define _PATH_SENDMAIL  "/usr/sbin/sendmail"
#endif

#define DOMAIN          "umich.edu"

/* FIXME: Add Auto: to Subject? */
#define SUBJECTPREFIX     "Out of email contact"

/* From tzfile.h */
#define SECSPERMIN      60
#define MINSPERHOUR     60
#define HOURSPERDAY     24
#define DAYSPERWEEK     7
#define DAYSPERNYEAR    365
#define DAYSPERLYEAR    366
#define SECSPERHOUR     (SECSPERMIN * MINSPERHOUR)
#define SECSPERDAY      ((time_t) SECSPERHOUR * HOURSPERDAY)
#define MONSPERYEAR     12

struct name_list {
    struct name_list *next;
    char *name;
};

struct headers {
    sds    subject;
    sds    messageid;
    sds    references;
    sds    inreplyto;
};

#endif
