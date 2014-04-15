#ifndef BACKEND_BDB_H
#define BACKEND_BDB_H

/*
 * backend_bdb.h
 *
 * Copyright (c) 2014 Regents of The University of Michigan.
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

#define DB_DBM_HSEARCH    1
#include <db.h>

struct vdb {
    DB  *dbp;
    char *path;
};

#endif /* BACKEND_BDB_H */
