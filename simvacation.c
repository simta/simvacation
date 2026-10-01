/*
 * Copyright (c) Regents of The University of Michigan
 * See COPYING.
 */

#include <config.h>

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sysexits.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "simvacation.h"
#include "vdb.h"
#include "vlu.h"
#include "vutil.h"

struct headers *readheaders(const ucl_object_t *);
static yastr    pretty_sender(const char *, const char *);
yastr           append_header(yastr, char *, int);
int             check_header(char *, const char *);
int send_message(yastr, yastr, yastr, yastr, yastr, struct headers *);

/* RFC 2822 2.1.1
 *  There are two limits that this standard places on the number of
 *  characters in a line. Each line of characters MUST be no more than
 *  998 characters, and SHOULD be no more than 78 characters, excluding
 *  the CRLF.
 */
#define MAXLINE 1000

extern int   optind, opterr;
extern char *optarg;

int
main(int argc, char **argv) {
    int   retval = EX_OK;
    bool  debug = false;
    int   ch, rc;
    yastr from = NULL;
    yastr canon_from = NULL;
    yastr rcpt;
    yastr vacmsg = NULL;
    yastr progname;
    char *p;
    char *config_file = NULL;

    struct vdb_backend *vdb = NULL;
    VDB                *vdbh = NULL;
    struct vlu_backend *vlu = NULL;
    VLU                *vluh = NULL;
    struct headers     *hdrs = NULL;

    progname = yaslauto(argv[ 0 ]);
    if ((p = strrchr(progname, '/')) != NULL) {
        yaslrange(progname, p - progname + 1, -1);
    }

    openlog(progname, LOG_PID, LOG_VACATION);
    opterr = 0;

    while ((ch = getopt(argc, argv, "c:df:")) != EOF) {
        switch ((char)ch) {
        case 'c':
            config_file = optarg;
            break;
        case 'd':
            debug = true;
            break;
        case 'f':
            from = yaslauto(optarg);
            break;

        case '?':
        default:
            retval = EX_USAGE;
        }
    }
    argc -= optind;
    argv += optind;

    if ((!*argv) || (retval != EX_OK)) {
        fprintf(stderr,
                "usage: %s [-c conf_file] [-d] [-f from_address] to_address\n",
                progname);
        retval = EX_USAGE;
        goto done;
    }

    if (debug) {
        openlog(progname, LOG_NOWAIT | LOG_PERROR | LOG_PID, LOG_VACATION);
    }

    rcpt = yaslauto(*argv);

    if (read_vacation_config(config_file) != VAC_RESULT_OK) {
        retval = EX_TEMPFAIL;
        goto done;
    }

    if ((vdb = vdb_backend(ucl_object_tostring(
                 ucl_object_lookup_path(vac_config, "core.vdb")))) == NULL) {
        retval = EX_TEMPFAIL;
        goto done;
    }

    if ((vlu = vlu_backend(ucl_object_tostring(
                 ucl_object_lookup_path(vac_config, "core.vlu")))) == NULL) {
        retval = EX_TEMPFAIL;
        goto done;
    }

    if ((vluh = vlu->init()) == NULL) {
        retval = EX_TEMPFAIL;
        goto done;
    }

    rc = vlu->search(vluh, rcpt);
    if (rc == VAC_RESULT_PERMFAIL) {
        rc = vlu->group_search(vluh, rcpt);
    }
    if (rc != VAC_RESULT_OK) {
        if (rc != VAC_RESULT_PERMFAIL) {
            retval = EX_TEMPFAIL;
        }
        goto done;
    }

    if ((vdbh = vdb->init(rcpt)) == NULL) {
        goto done;
    }

    if ((hdrs = readheaders(vlu->aliases(vluh, rcpt))) == NULL) {
        goto done;
    }

    if (!hdrs->rcpt_match) {
        syslog(LOG_INFO, "message does not appear to be to %s", rcpt);
        goto done;
    }

    if ((canon_from = check_from(from)) == NULL) {
        syslog(LOG_INFO, "skipping message, bad sender %s", from);
        goto done;
    }

    if (vdb->recent(vdbh, canon_from, vlu->interval(vluh, rcpt)) ==
            VDB_STATUS_RECENT) {
        syslog(LOG_DEBUG, "suppressed message for %s to %s", rcpt, from);
        goto done;
    }

    /* All the checks have passed, send the message. */
    vdb->store_reply(vdbh, canon_from);

    if ((vacmsg = vlu->message(vluh, rcpt)) == NULL) {
        vacmsg = yaslauto(ucl_object_tostring(
                ucl_object_lookup_path(vac_config, "core.default_message")));
    }

    retval = send_message(pretty_sender(rcpt, vlu->display_name(vluh, rcpt)),
            from, canon_from, vacmsg, vlu->subject_prefix(vluh, rcpt), hdrs);

    if (retval == EX_OK) {
        syslog(LOG_DEBUG, "sent message for %s to %s", rcpt, from);
    }

done:
    if (vdb) {
        vdb->close(vdbh);
    }
    if (vlu) {
        vlu->close(vluh);
    }

    exit(retval);
}

struct headers *
readheaders(const ucl_object_t *names) {
    struct headers     *h;
    char               *p;
    int                 state, stripfield = 0;
    char                buf[ MAXLINE ];
    yastr              *current_hdr = NULL;
    yastr               rcpt_hdrs = yaslempty();
    ucl_object_iter_t   i;
    const ucl_object_t *obj;

    h = calloc(1, sizeof(struct headers));
    h->rcpt_match = false;

    state = HEADER_UNKNOWN;
    while (fgets(buf, sizeof(buf), stdin) && *buf != '\n') {
        if (check_header(buf, "Message-ID:") == 0) {
            state = HEADER_APPEND;
            stripfield = 1;
            current_hdr = &h->messageid;
        } else if (check_header(buf, "References:") == 0) {
            state = HEADER_APPEND;
            stripfield = 1;
            current_hdr = &h->references;
        } else if (check_header(buf, "In-Reply-To:") == 0) {
            state = HEADER_APPEND;
            stripfield = 1;
            current_hdr = &h->inreplyto;
        }
        /* RFC 3834 2
         *  Automatic responses SHOULD NOT be issued in response to any
         *  message which contains an Auto-Submitted header field (see below),
         *  where that field has any value other than "no"
         */
        else if (check_header(buf, "Auto-Submitted:") == 0) {
            state = HEADER_NOREPLY;
            p = buf + 15;
            while (*++p && isspace(*p))
                ;
            if (!*p) {
                break;
            }
            if (strncasecmp(p, "no", 2) != 0) {
                syslog(LOG_DEBUG, "readheaders: suppressing message: %s", buf);
                return NULL;
            }
        }
        /* RFC 3834 2
         *  A responder MAY refuse to send a response to a subject message
         *  which contains any header or content which makes it appear to the
         *  responder that a response would not be appropriate.
         */
        else if (check_header(buf, "List-") == 0) {
            /* RFC 3834 2
             *  For similar reasons, a responder MAY ignore any subject message
             *  with a List-* field [RFC2369].
             */
            state = HEADER_NOREPLY;
            syslog(LOG_DEBUG, "readheaders: suppressing message: %s", buf);
            return NULL;
        } else if (check_header(buf, "Precedence") == 0) {
            /* RFC 3834 2
             *  For instance, if the subject message contained a
             *  Precedence header field [RFC2076] with a value of
             *  "list" the responder might guess that the traffic had
             *  arrived from a mailing list, and would not respond if
             *  the response were only intended for personal messages.
             *  [...]
             *  (Because Precedence is not a standard header field, and
             *  its use and interpretation vary widely in the wild,
             *  no particular responder behavior in the presence of
             *  Precedence is recommended by this specification.
             */
            state = HEADER_NOREPLY;
            if ((buf[ 10 ] == ':' || buf[ 10 ] == ' ' || buf[ 10 ] == '\t') &&
                    (p = index(buf, ':'))) {
                while (*++p && isspace(*p))
                    ;
                if (!*p) {
                    break;
                }
                if (strncasecmp(p, "junk", 4) == 0 ||
                        strncasecmp(p, "bulk", 4) == 0 ||
                        strncasecmp(p, "list", 4) == 0) {
                    syslog(LOG_DEBUG,
                            "readheaders: suppressing message: precedence %s",
                            p);
                    return NULL;
                }
            }
        } else if (check_header(buf, "X-Auto-Response-Suppress:") == 0) {
            /* MS-OXCMAIL 2.1.3.2.20
             *
             * X-Auto-Response-Suppress value   | Meaning
             * ----------------------------------------------------------------
             * None                             |
             * ----------------------------------------------------------------
             * All                              |
             * ----------------------------------------------------------------
             * DR                               | Suppress delivery reports
             *                                  | from transport.
             * ----------------------------------------------------------------
             * NDR                              | Suppress non-delivery reports
             *                                  | from transport.
             * ----------------------------------------------------------------
             * RN                               | Suppress read notifications
             *                                  | from receiving client.
             * ----------------------------------------------------------------
             * NRN                              | Suppress non-read
             *                                  | notifications from receiving
             *                                  | client.
             * ----------------------------------------------------------------
             * OOF                              | Suppress Out of Office (OOF)
             *                                  | notifications.
             * ----------------------------------------------------------------
             * AutoReply                        | Suppress auto-reply messages
             *                                  | other than OOF notifications.
             * ----------------------------------------------------------------
             *
             * The order of these values in the header is not important.
             */
            state = HEADER_NOREPLY;
            if (is_substring("OOF", buf, false) ||
                    is_substring("All", buf, false)) {
                syslog(LOG_DEBUG, "readheaders: suppressing message: %s", buf);
                return NULL;
            }
        }
        /* RFC 3834 2
         *  Personal and Group responses whose purpose is to notify the sender
         *  of a message of a temporary absence of the recipient (e.g.,
         *  "vacation" and "out of the office" notices) SHOULD NOT be issued
         *  unless a valid address for the recipient is explicitly included in
         *  a recipient (e.g., To, Cc, Bcc, Resent-To, Resent-Cc, or Resent-
         *  Bcc) field of the subject message.
         */
        else if (check_header(buf, "Cc:") == 0) {
            state = HEADER_RECIPIENT;
        } else if (check_header(buf, "To:") == 0) {
            state = HEADER_RECIPIENT;
        } else if (check_header(buf, "Subject:") == 0) {
            state = HEADER_APPEND;
            stripfield = 1;
            current_hdr = &h->subject;
        } else if (!isspace(*buf)) {
            /* Not a continuation of the previous header line, reset flag
             *
             * RFC 2822 2.2.3
             *  Unfolding is accomplished by simply removing any CRLF
             *  that is immediately followed by WSP.
             */
            state = HEADER_UNKNOWN;
        }

        switch (state) {
        case HEADER_RECIPIENT:
            rcpt_hdrs = yaslcat(rcpt_hdrs, buf);
            rcpt_hdrs = yaslcatlen(rcpt_hdrs, " ", 1);
            break;

        case HEADER_APPEND:
            *current_hdr = append_header(*current_hdr, buf, stripfield);
            stripfield = 0;

            break;
        }
    }

    i = ucl_object_iterate_new(names);
    while (!h->rcpt_match &&
            (obj = ucl_object_iterate_safe(i, false)) != NULL) {
        h->rcpt_match = is_substring(ucl_object_tostring(obj), rcpt_hdrs, true);
    }
    ucl_object_iterate_free(i);

    return h;
}

int
check_header(char *line, const char *field) {
    return strncasecmp(field, line, strlen(field));
}

yastr
append_header(yastr str, char *value, int stripfield) {
    if (stripfield) {
        value = index(value, ':');
        while (value && *++value && isspace(*value))
            ;
    }

    if (str == NULL) {
        str = yaslempty();
    }

    yastr s = yaslcat(str, value);
    yasltrim(s, "\n");
    return s;
}

static yastr
pretty_sender(const char *sender, const char *sender_name) {
    yastr       retval = yaslempty();
    const char *domain = ucl_object_tostring(
            ucl_object_lookup_path(vac_config, "core.domain"));

    if (sender_name && (strcasecmp(sender_name, sender) != 0)) {
        retval = yaslcatprintf(
                retval, "\"%s\" <%s@%s>", sender_name, sender, domain);
    } else {
        retval = yaslcatprintf(retval, "%s@%s", sender, domain);
    }
    return retval;
}


int
send_message(yastr sender, yastr rcpt, yastr canon_rcpt, yastr vmsg,
        yastr subject, struct headers *h) {
    char   hostname[ 255 ];
    int    splitlen;
    yastr *split;

    split = yaslsplitargs(ucl_object_tostring(ucl_object_lookup_path(
                                  vac_config, "core.sendmail")),
            &splitlen);

    split = realloc(split, (splitlen + 1) * sizeof(yastr));
    split[ splitlen ] = NULL;

    /* Replace placeholder with recipient */
    for (int i = 0; i < splitlen; i++) {
        if ((yasllen(split[ i ]) == 2) && (memcmp(split[ i ], "$R", 2) == 0)) {
            split[ i ] = rcpt;
        }
    }

    if ((pexecv(split)) != VAC_RESULT_OK) {
        syslog(LOG_ERR, "mail: pexecv of %s failed", split[ 0 ]);
        return EX_TEMPFAIL;
    }
    /*
     * Our stdout should now be hooked up to sendmail's stdin.
     * Generate headers and print the vacation message.
     */

    gethostname(hostname, 255);
    printf("Message-ID: <%llx_%x@%s>\n", (unsigned long long)time(NULL),
            getpid(), hostname);

    /* RFC 3834 3.1.1
     *  For responses sent by Personal Responders, the From field SHOULD
     *  contain the name of the recipient of the subject message (i.e.,
     *  the user on whose behalf the response is being sent) and an
     *  address chosen by the recipient of the subject message to be
     *  recognizable to correspondents.  Often this will be the same
     *  address that was used to send the subject message to that
     *  recipient.
     */
    printf("From: %s\n", sender);

    /* RFC 3834 3.1.3
     *  The To header field SHOULD indicate the recipient of the response.
     *  In general there SHOULD only be one recipient of any automatic
     *  response.  This minimizes the potential for sorcerer's apprentice
     *  mode and denial-of-service attacks.
     */
    printf("To: %s\n", canon_rcpt);

    /* RFC 3834 3.1.5
     *  The Subject field SHOULD contain a brief indication that the message
     *  is an automatic response, followed by contents of the Subject field
     *  (or a portion thereof) from the subject message.  The prefix "Auto:"
     *  MAY be used as such an indication.  If used, this prefix SHOULD be
     *  followed by an ASCII SPACE character (0x20).
     */
    printf("Subject: %s", subject);
    if (yasllen(h->subject) > 0) {
        if (check_header(h->subject, "Re:") != 0) {
            printf(" (Re: %s)", h->subject);
        } else {
            printf(" (%s)", h->subject);
        }
    }
    printf("\n");

    /* RFC 3834 3.1.6
     *  The In-Reply-To and References fields SHOULD be provided in the
     *  header of a response message if there was a Message-ID field in the
     *  subject message, according to the rules in [RFC2822] section
     *  3.6.4.
     */
    if (yasllen(h->messageid) > 0) {
        /* RFC 2822 3.6.4
         *  The "In-Reply-To:" field will contain the contents of the
         *  "Message-ID:" field of the message to which this one is a reply
         *  (the "parent message"). [...] If there is no "Message-ID:" field
         *  in any of the parent messages, then the new message will have no
         *  "In-Reply-To:" field.
         */
        printf("In-Reply-To: %s\n", h->messageid);
    }

    /* RFC 2822 3.6.4
     *  The "References:" field will contain the contents of the
     *  parent's "References:" field (if any) followed by the contents
     *  of the parent's "Message-ID:" field (if any). If the parent
     *  message does not contain a "References:" field but does have an
     *  "In-Reply-To:" field containing a single message identifier, then
     *  the "References:" field will contain the contents of the parent's
     *  "In-Reply-To:" field followed by the contents of the parent's
     *  "Message-ID:" field (if any).
     */
    if (yasllen(h->references) > 0) {
        printf("References: %s %s\n", h->references, h->messageid);
    } else if (yasllen(h->inreplyto) > 0) {
        printf("References: %s %s\n", h->inreplyto, h->messageid);
    } else if (yasllen(h->messageid) > 0) {
        printf("References: %s\n", h->messageid);
    }

    /* RFC 3834 3.1.7
     *  The Auto-Submitted field, with a value of "auto-replied", SHOULD be
     *  included in the message header of any automatic response.
     */
    printf("Auto-Submitted: auto-replied\n");

    printf("MIME-Version: 1.0\n");
    printf("Content-Type: text/plain; charset=UTF-8\n");

    /* End of headers */
    printf("\n");

    printf("%s\n", vmsg);

    return EX_OK;
}
