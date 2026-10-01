#ifndef VLU_H
#define VLU_H

#include "simvacation.h"

#ifdef HAVE_LDAP
#include <lber.h>
#include <ldap.h>
#endif /* HAVE_LDAP */

#ifdef HAVE_LDAP
struct vlu_ldap {
    LDAP          *ld;
    LDAPMessage   *result;
    struct timeval timeout;
    yastr          default_msg;
    yastr          subject_prefix;
    time_t         interval;
    const char    *attr_vacation;
    const char    *attr_vacation_msg;
    const char    *attr_group_msg;
    const char    *attr_name;
    const char    *attr_msg;
    const char    *attr_autoreply_start;
    const char    *attr_autoreply_end;
    const char    *search_base;
    const char    *group_search_base;
    char         **attrs;
};
#endif /* HAVE_LDAP */

typedef union vlu {
    int null;
#ifdef HAVE_LDAP
    struct vlu_ldap *ldap;
#endif /* HAVE_LDAP */
} VLU;

struct vlu_backend {
    VLU *(*init)();
    vac_result (*search)(VLU *, const yastr);
    vac_result (*group_search)(VLU *, const yastr);
    yastr (*message)(VLU *, const yastr);
    yastr (*subject_prefix)(VLU *, const yastr);
    time_t (*interval)(VLU *, const yastr);
    ucl_object_t *(*aliases)(VLU *, const yastr);
    yastr (*name)(VLU *, const yastr);
    yastr (*display_name)(VLU *, const yastr);
    void (*close)(VLU *);
};

struct vlu_backend *vlu_backend(const char *);
VLU                *vlu_init();
vac_result          vlu_search(VLU *, const yastr);
vac_result          vlu_group_search(VLU *, const yastr);
yastr               vlu_message(VLU *, const yastr);
yastr               vlu_subject_prefix(VLU *, const yastr);
time_t              vlu_interval(VLU *, const yastr);
ucl_object_t       *vlu_aliases(VLU *, const yastr);
yastr               vlu_name(VLU *, const yastr);
yastr               vlu_display_name(VLU *, const yastr);
void                vlu_close(VLU *);

#ifdef HAVE_LDAP
VLU          *ldap_vlu_init();
vac_result    ldap_vlu_search(VLU *, const yastr);
vac_result    ldap_vlu_group_search(VLU *, const yastr);
yastr         ldap_vlu_message(VLU *, const yastr);
yastr         ldap_vlu_subject_prefix(VLU *, const yastr);
time_t        ldap_vlu_interval(VLU *, const yastr);
ucl_object_t *ldap_vlu_aliases(VLU *, const yastr);
yastr         ldap_vlu_name(VLU *, const yastr);
yastr         ldap_vlu_display_name(VLU *, const yastr);
void          ldap_vlu_close(VLU *);
#endif /* HAVE_LDAP */

#endif /* VLU_H */
