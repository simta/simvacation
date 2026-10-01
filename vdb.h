#ifndef BACKEND_VDB_H
#define BACKEND_VDB_H

#include "simvacation.h"

#ifdef HAVE_LMDB
#include <lmdb.h>
#endif

#ifdef HAVE_URCL
#include <urcl.h>
#endif /* HAVE_URCL */


typedef enum {
    VDB_STATUS_OK,
    VDB_STATUS_RECENT,
} vdb_status;

typedef struct vdb {
    union {
        int null;
#ifdef HAVE_URCL
        urclHandle *redis;
#endif /* HAVE_URCL */
#ifdef HAVE_LMDB
        MDB_env *lmdb;
#endif /* HAVE_LMDB */
    };
    yastr rcpt;
} VDB;

struct vdb_backend {
    VDB *(*init)(const yastr);
    void (*close)(VDB *);
    vdb_status (*recent)(VDB *, const yastr, time_t);
    vac_result (*store_reply)(VDB *, const yastr);
    ucl_object_t *(*get_names)(VDB *);
    void (*clean)(VDB *, const yastr);
    void (*gc)(VDB *);
};

struct vdb_backend *vdb_backend(const char *);
VDB                *vdb_init(const yastr);
void                vdb_close(VDB *);
vdb_status          vdb_recent(VDB *, const yastr, time_t);
vac_result          vdb_store_reply(VDB *, const yastr);
ucl_object_t       *vdb_get_names(VDB *);
void                vdb_clean(VDB *, const yastr);
void                vdb_gc(VDB *);

#ifdef HAVE_LMDB
VDB       *lmdb_vdb_init(const yastr);
void       lmdb_vdb_close(VDB *);
vdb_status lmdb_vdb_recent(VDB *, const yastr, time_t);
vac_result lmdb_vdb_store_reply(VDB *, const yastr);
void       lmdb_vdb_gc(VDB *);
#endif /* HAVE_LMDB */

#ifdef HAVE_URCL
VDB       *redis_vdb_init(const yastr);
void       redis_vdb_close(VDB *);
vdb_status redis_vdb_recent(VDB *, const yastr, time_t);
vac_result redis_vdb_store_reply(VDB *, const yastr);
#endif /* HAVE_URCL */

#endif /* BACKEND_VDB_H */
