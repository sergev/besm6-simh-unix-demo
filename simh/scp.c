/* scp.c: simulator control program
 *
 * What is left of upstream's SCP after the command interpreter went: the event
 * queue, unit attach/detach, the debug output path, and sim_run(), which
 * brackets sim_instr() with console mode, signal handlers and wall-clock
 * timing.  There is no cmd_table, no sim> loop and no SET/SHOW/ATTACH; an
 * entry point of one's own calls sim_scp_init(), sim_run() and sim_scp_exit().
 */

#include "sim_defs.h"
#include "sim_sock.h"
#include <signal.h>

/* The head of the clock queue counts down in sim_interval; write the countdown
   back before touching the queue. */

#define SYNC_QUEUE_HEAD                                \
    do {                                               \
        if (sim_clock_queue != QUEUE_LIST_END)         \
            sim_clock_queue->time = sim_interval;      \
    } while (0)

/* Forward declarations for routines private to this file */
static t_stat attach_err(UNIT *uptr, t_stat stat);
static void detach_all(void);
static void int_handler(int signal);
static t_stat reset_all(void);

/* Global data */

UNIT *sim_clock_queue          = QUEUE_LIST_END;
int32 sim_interval             = 0;
int32 sim_switches             = 0;
volatile t_bool sim_is_running = FALSE;
volatile t_bool stop_cpu       = FALSE;
FILE *sim_deb                  = NULL; /* debug file */

static volatile t_bool sigterm_received = FALSE;
static int sim_exit_status              = EXIT_SUCCESS;

/* Tables and strings */

static const char *const scp_errors[1 + SCPE_MAX_ERR - SCPE_BASE] = {
    "Unit not attached",
    "I/O error",
    "Format error",
    "Unit not attachable",
    "File open error",
    "Memory exhausted",
    "Invalid argument",
    "Unknown command",
    "Read only argument",
    "Simulation stopped",
    "Goodbye",
    "Console input I/O error",
    "No settable parameters",
    "Unit already attached",
    "Signal handler setup error",
    "Console terminal setup error",
    "Command not allowed",
    "Read only operation not allowed",
    "Invalid switch",
    "Missing value",
    "Too few arguments",
    "Too many arguments",
    "Non-existent parameter",
    "Internal error",
    "Console Telnet output stall",
    "SIGTERM received",
};

/* Turn on tracing from the environment.  There is no command interpreter to
   say `set cpu debug' to any more, so BESM6_DEBUG names the trace file ("-"
   means stderr) and BESM6_TRACE lists the devices to trace, comma separated:

        BESM6_DEBUG=- BESM6_TRACE=cpu,mmu ./besm6

   With BESM6_DEBUG unset, sim_deb stays NULL and every trace site is a
   predictable branch on a null pointer, as before. */

static void sim_debug_from_env(void)
{
    const char *file = getenv("BESM6_DEBUG");
    const char *devs = getenv("BESM6_TRACE");
    DEVICE *dptr;
    uint32 i;

    if ((file == NULL) || (*file == '\0'))
        return;
    if (strcmp(file, "-") == 0)
        sim_deb = stderr;
    else if ((sim_deb = fopen(file, "w")) == NULL) {
        fprintf(stderr, "Can't open debug file '%s': %s\n", file, strerror(errno));
        return;
    }
    setvbuf(sim_deb, NULL, _IOLBF, 0);
    if ((devs == NULL) || (*devs == '\0'))
        devs = "cpu";
    for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
        const char *p = devs;
        size_t n      = strlen(dptr->name);

        while (*p) { /* is dptr->name one of the comma separated words? */
            const char *e = strchr(p, ',');
            size_t len    = e ? (size_t)(e - p) : strlen(p);

            if ((len == n) && (strncasecmp(p, dptr->name, n) == 0)) {
                dptr->dctrl = 0xffffffff;
                break;
            }
            if (!e)
                break;
            p = e + 1;
        }
    }
}

void sim_debug_close(void)
{
    if (sim_deb == NULL)
        return;
    if (sim_deb != stderr)
        fclose(sim_deb);
    sim_deb = NULL;
}

/* Startup and shutdown.  An entry point brackets its own work with these:
 * sim_scp_init returns SCPE_OK to proceed, anything else to stop; either way
 * the caller must finish with sim_scp_exit, which returns the exit status.
 */

t_stat sim_scp_init(int argc, char *argv[])
{
    t_stat stat;

    sim_switches = 0;

    signal(SIGPIPE, SIG_IGN); /* writing to a closed telnet line must not kill us */
    stop_cpu        = FALSE;
    sim_interval    = 0;
    sim_clock_queue = QUEUE_LIST_END;
    sim_is_running  = FALSE;
    sim_timer_init();

    if ((stat = sim_ttinit()) != SCPE_OK) {
        fprintf(stderr, "Fatal terminal initialization error\n%s\n", sim_error_text(stat));
        sim_exit_status = EXIT_FAILURE;
        return stat;
    }
    if ((stat = reset_all()) != SCPE_OK) {
        sim_exit_status = EXIT_FAILURE;
        return stat;
    }
    signal(SIGINT, int_handler);
    sim_debug_from_env();
    return SCPE_OK;
}

int sim_scp_exit(t_stat stat)
{
    detach_all();       /* close files */
    sim_debug_close();  /* close the debug file */
    sim_ttcmd();        /* restore the console */
    if (sim_exit_status != EXIT_SUCCESS)
        return sim_exit_status; /* startup failed */
    /* SCPE_STOP and SCPE_EXIT are how a normal run ends. */
    if ((stat == SCPE_OK) || (SCPE_BARE_STATUS(stat) == SCPE_STOP) ||
        (SCPE_BARE_STATUS(stat) == SCPE_EXIT))
        return EXIT_SUCCESS;
    return EXIT_FAILURE;
}

/* Power-up reset of every device */

static t_stat reset_all(void)
{
    DEVICE *dptr;
    uint32 i;
    t_stat reason;

    for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
        if (dptr->reset == NULL)
            continue;
        reason = dptr->reset(dptr);
        if (reason != SCPE_OK) {
            fprintf(stderr,
                    "Fatal simulator initialization error\n"
                    "Device %s initial reset call returned: %s\n",
                    dptr->name, sim_error_text(reason));
            return reason;
        }
    }
    return SCPE_OK;
}

/* Attach unit to file */

t_stat attach_unit(UNIT *uptr, const char *cptr)
{
    if (!(uptr->flags & UNIT_ATTABLE)) /* not attachable? */
        return SCPE_NOATT;
    if (find_dev_from_unit(uptr) == NULL)
        return SCPE_NOATT;
    uptr->filename = (char *)calloc(CBUFSIZE, sizeof(char)); /* alloc name buf */
    if (uptr->filename == NULL)
        return SCPE_MEM;
    strlcpy(uptr->filename, cptr, CBUFSIZE); /* save name */
    if (sim_switches & SWMASK('N')) {        /* new file only? */
        uptr->fileref = fopen(cptr, "wb+");  /* open new file */
        if (uptr->fileref == NULL)           /* open fail? */
            return sim_messagef(attach_err(uptr, SCPE_OPENERR),
                                "%s: Can't open '%s': %s\n", /* yes, error */
                                sim_uname(uptr), cptr, strerror(errno));
        sim_messagef(SCPE_OK, "%s: creating new file: %s\n", sim_uname(uptr), cptr);
    } else {                                /* normal */
        uptr->fileref = fopen(cptr, "rb+"); /* open r/w */
        if (uptr->fileref == NULL) {        /* open fail? */
            if ((errno == EROFS) || (errno == EACCES) || (errno == EPERM)) { /* read only? */
                if ((uptr->flags & UNIT_ROABLE) == 0) /* allowed? */
                    return sim_messagef(attach_err(uptr, SCPE_NORO),
                                        "%s: Read Only operation not allowed\n", /* no, error */
                                        sim_uname(uptr));
                uptr->fileref = fopen(cptr, "rb"); /* open rd only */
                if (uptr->fileref == NULL)         /* open fail? */
                    return sim_messagef(attach_err(uptr, SCPE_OPENERR),
                                        "%s: Can't open '%s': %s\n", /* yes, error */
                                        sim_uname(uptr), cptr, strerror(errno));
                uptr->flags = uptr->flags | UNIT_RO; /* set rd only */
                sim_messagef(SCPE_OK, "%s: unit is read only\n", sim_uname(uptr));
            } else {                            /* doesn't exist */
                if (sim_switches & SWMASK('E')) /* must exist? */
                    return sim_messagef(attach_err(uptr, SCPE_OPENERR),
                                        "%s: Can't open '%s': %s\n", /* yes, error */
                                        sim_uname(uptr), cptr, strerror(errno));
                uptr->fileref = fopen(cptr, "wb+"); /* open new file */
                if (uptr->fileref == NULL)          /* open fail? */
                    return sim_messagef(attach_err(uptr, SCPE_OPENERR),
                                        "%s: Can't open '%s': %s\n", /* yes, error */
                                        sim_uname(uptr), cptr, strerror(errno));
                sim_messagef(SCPE_OK, "%s: creating new file\n", sim_uname(uptr));
            }
        } /* end if null */
    } /* end else */
    uptr->flags = uptr->flags | UNIT_ATT;
    return SCPE_OK;
}

static t_stat attach_err(UNIT *uptr, t_stat stat)
{
    free(uptr->filename);
    uptr->filename = NULL;
    return stat;
}

/* Detach every unit at shutdown.  A device with its own detach routine gets
   called even when the unit is not attachable -- that is how besm6_tty closes
   its listeners.  Errors are ignored; we are exiting. */

static void detach_all(void)
{
    uint32 i, j;
    DEVICE *dptr;
    UNIT *uptr;

    for (i = 0; (dptr = sim_devices[i]) != NULL; i++) { /* loop thru dev */
        for (j = 0; j < dptr->numunits; j++) {          /* loop thru units */
            uptr = (dptr->units) + j;
            if ((uptr->flags & UNIT_ATT) ||      /* attached? */
                (dptr->detach &&                 /* or a device routine, */
                 !(uptr->flags & UNIT_ATTABLE))) /* !attachable? */
                (dptr->detach != NULL) ? dptr->detach(uptr) : detach_unit(uptr);
        }
    }
}

/* Detach unit from file */

t_stat detach_unit(UNIT *uptr)
{
    if (uptr == NULL)
        return SCPE_IERR;
    if (!(uptr->flags & UNIT_ATTABLE)) /* attachable? */
        return SCPE_NOATT;
    if (!(uptr->flags & UNIT_ATT)) /* not attached? */
        return SCPE_UNATT;
    if (find_dev_from_unit(uptr) == NULL)
        return SCPE_OK;
    uptr->flags = uptr->flags & ~(UNIT_ATT | ((uptr->flags & UNIT_ROABLE) ? UNIT_RO : 0));
    free(uptr->filename);
    uptr->filename = NULL;
    if (uptr->fileref) { /* Only close open file */
        if (fclose(uptr->fileref) == EOF) {
            uptr->fileref = NULL;
            return SCPE_IOERR;
        }
        uptr->fileref = NULL;
    }
    return SCPE_OK;
}

/* Get unit display name.  Named after its device, with the unit number
   appended when the device has more than one. */

const char *sim_uname(UNIT *uptr)
{
    DEVICE *d;
    char uname[CBUFSIZE];

    if (!uptr)
        return "";
    if (uptr->uname)
        return uptr->uname;
    d = find_dev_from_unit(uptr);
    if (!d)
        return "";
    if (d->numunits == 1)
        sprintf(uname, "%s", d->name);
    else
        sprintf(uname, "%s%d", d->name, (int)(uptr - d->units));
    return uptr->uname = strcpy((char *)malloc(1 + strlen(uname)), uname);
}

/* Signal handler for ^C signal - set stop simulation flag */

static void int_handler(int sig)
{
    stop_cpu = TRUE;
    if ((sig == SIGTERM)
#ifdef SIGHUP
        || (sig == SIGHUP)
#endif
    )
        sigterm_received = TRUE;
    sim_interval = 0; /* Minimize when stop_cpu gets noticed */
}

/* get_glyph - next glyph, upper-cased.  Whitespace ends a glyph, as does
   mchar when it is non-zero; trailing terminators are skipped. */

const char *get_glyph(const char *iptr, char *optr, char mchar)
{
    while ((*iptr != 0) && (sim_isspace(*iptr) == 0) && (*iptr != mchar)) {
        *optr++ = (char)sim_toupper(*iptr);
        iptr++;
    }
    *optr = 0;                                /* terminate result string */
    if (((mchar != 0) && (*iptr == mchar)) || /* skip input terminator */
        sim_isspace(*iptr))
        iptr++;
    while ((*iptr != 0) && /* skip further terminators */
           (sim_isspace(*iptr) || ((mchar != 0) && (*iptr == mchar))))
        iptr++;
    return iptr;
}

/* get_sim_sw           accumulate sim_switches

   Inputs:
        cptr    =       pointer to input string
   Outputs:
        ptr     =       pointer to first non-switch glyph, NULL if a switch
                        held something that was not a letter
*/

const char *get_sim_sw(const char *cptr)
{
    char gbuf[CBUFSIZE], *sw;

    while (*cptr == '-') {               /* while switches */
        cptr = get_glyph(cptr, gbuf, 0); /* get switch glyph */
        for (sw = gbuf + 1; (sim_isspace(*sw) == 0) && (*sw != 0); sw++) {
            if (sim_isalpha(*sw) == 0)
                return NULL;
            sim_switches |= SWMASK(sim_toupper(*sw));
        }
    }
    return cptr;
}

/* Find_dev_from_unit   find device for unit

   Inputs:
        uptr    =       pointer to unit
   Outputs:
        result  =       pointer to device
*/

DEVICE *find_dev_from_unit(UNIT *uptr)
{
    DEVICE *dptr;
    uint32 i, j;

    if (uptr == NULL)
        return NULL;
    if (uptr->dptr)
        return uptr->dptr;
    for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
        for (j = 0; j < dptr->numunits; j++) {
            if (uptr == (dptr->units + j)) {
                uptr->dptr = dptr;
                return dptr;
            }
        }
    }
    return NULL;
}

/* Event queue package

        sim_activate            add entry to event queue
        sim_activate_after      add entry to event queue after a wall time delay
        sim_cancel              remove entry from event queue
        sim_process_event       process entries on event queue
        sim_is_active           see if entry is on event queue
        sim_activate_time       return time until activation

   Asynchronous events are set up by queueing a unit data structure
   to the event queue with a timeout (in simulator units, relative
   to the current time).  Each simulator 'times' these events by
   counting down interval counter sim_interval.  When this reaches
   zero the simulator calls sim_process_event to process the event
   and to see if further events need to be processed, or sim_interval
   reset to count the next one.

   The event queue is maintained in clock order; entry timeouts are
   RELATIVE to the time in the previous entry.

   sim_process_event - process event

   Inputs:
        none
   Outputs:
        reason  =       reason code returned by any event processor,
                        or 0 (SCPE_OK) if no exceptions
*/

t_stat sim_process_event(void)
{
    UNIT *uptr;
    t_stat reason, bare_reason;
    int32 sim_interval_catchup;

    if (stop_cpu) { /* stop CPU? */
        stop_cpu = 0;
        return SCPE_STOP;
    }
    SYNC_QUEUE_HEAD;
    if (sim_interval > 0) {
        return SCPE_OK;
    }
    if (sim_clock_queue == QUEUE_LIST_END) { /* queue empty? */
        sim_interval = NOQUEUE_WAIT;         /* flag queue empty */
        return SCPE_OK;
    }
    /* If sim_interval is negative, we've missed the opportunity to  */
    /* dispatch one or more events when they were scheduled to fire. */
    /* To accomodate this, we backup time to when the first event    */
    /* was supposed to fire and advance it from there until things   */
    /* have caught up.                                               */
    if (sim_interval < 0) {
        sim_interval_catchup = sim_interval;
        sim_interval         = 0;
        SYNC_QUEUE_HEAD;
    } else
        sim_interval_catchup = 0;
    do {
        uptr            = sim_clock_queue; /* get first */
        sim_clock_queue = uptr->next;      /* remove first */
        uptr->next      = NULL;            /* hygiene */
        uptr->time      = 0;
        if (sim_clock_queue != QUEUE_LIST_END) {
            if (sim_interval_catchup < 0)
                sim_interval = -sim_interval_catchup;
            sim_interval += sim_interval_catchup + sim_clock_queue->time;
        } else
            sim_interval = NOQUEUE_WAIT;
        if (uptr->usecs_remaining) {
            reason = sim_timer_activate_after(uptr, uptr->usecs_remaining);
        } else {
            if (uptr->action != NULL)
                reason = uptr->action(uptr);
            else
                reason = SCPE_OK;
        }
        if (sim_interval_catchup < -1)
            sim_interval_catchup += sim_clock_queue->time;
        else
            sim_interval_catchup = 0;
        bare_reason = SCPE_BARE_STATUS(reason);
        if ((bare_reason != SCPE_OK) && /* Provide context for unexpected errors */
            (bare_reason >= SCPE_BASE) && (bare_reason != SCPE_STOP) &&
            (bare_reason != SCPE_EXIT)) {
            if (bare_reason == SCPE_UNATT)
                sim_messagef(reason, "\nUnexpected I/O error while processing event for %s - %s\n",
                             sim_uname(uptr), sim_error_text(reason));
            else
                sim_messagef(reason,
                             "\nUnexpected internal error while processing event for %s which "
                             "returned %d - %s\n",
                             sim_uname(uptr), reason, sim_error_text(reason));
        }
    } while ((reason == SCPE_OK) && ((sim_interval + sim_interval_catchup) <= 0) &&
             (sim_clock_queue != QUEUE_LIST_END) && (!stop_cpu));

    if (sim_clock_queue == QUEUE_LIST_END) { /* queue empty? */
        sim_interval = NOQUEUE_WAIT;         /* flag queue empty */
    }
    if ((reason == SCPE_OK) && stop_cpu) {
        stop_cpu = FALSE;
        reason   = SCPE_STOP;
    }
    return reason;
}

/* sim_activate - activate (queue) event

   Inputs:
        uptr    =       pointer to unit
        event_time =    relative timeout
   Outputs:
        reason  =       result (SCPE_OK if ok)
*/

t_stat sim_activate(UNIT *uptr, int32 event_time)
{
    if (uptr->is_timer_unit)
        return sim_timer_activate(uptr, event_time);
    return _sim_activate(uptr, event_time);
}

t_stat _sim_activate(UNIT *uptr, int32 event_time)
{
    UNIT *cptr, *prvptr;
    int32 accum;

    if (sim_is_active(uptr)) /* already active? */
        return SCPE_OK;
    SYNC_QUEUE_HEAD;

    prvptr = NULL;
    accum  = 0;
    for (cptr = sim_clock_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
        if (event_time < (accum + cptr->time))
            break;
        accum  = accum + cptr->time;
        prvptr = cptr;
    }
    if (prvptr == NULL) { /* insert at head */
        cptr = uptr->next = sim_clock_queue;
        sim_clock_queue   = uptr;
    } else {
        cptr = uptr->next = prvptr->next; /* insert at prvptr */
        prvptr->next      = uptr;
    }
    uptr->time = event_time - accum;
    if (cptr != QUEUE_LIST_END)
        cptr->time = cptr->time - uptr->time;
    sim_interval = sim_clock_queue->time;
    return SCPE_OK;
}

/* sim_activate_after - activate (queue) event

   Inputs:
        uptr    =       pointer to unit
        usec_delay =    relative timeout (in microseconds)
   Outputs:
        reason  =       result (SCPE_OK if ok)
*/

t_stat sim_activate_after(UNIT *uptr, double usec_delay)
{
    if (sim_is_active(uptr)) /* already active? */
        return SCPE_OK;
    return sim_timer_activate_after(uptr, usec_delay);
}

/* sim_cancel - cancel (dequeue) event

   Inputs:
        uptr    =       pointer to unit
   Outputs:
        reason  =       result (SCPE_OK if ok)

*/

t_stat sim_cancel(UNIT *uptr)
{
    UNIT *cptr, *nptr;

    if ((uptr->cancel) && uptr->cancel(uptr))
        return SCPE_OK;
    if (uptr->is_timer_unit)
        sim_timer_cancel(uptr);
    if (sim_clock_queue == QUEUE_LIST_END)
        return SCPE_OK;
    if (!sim_is_active(uptr))
        return SCPE_OK;
    SYNC_QUEUE_HEAD;
    nptr = QUEUE_LIST_END;

    if (sim_clock_queue == uptr) {
        nptr = sim_clock_queue = uptr->next;
        uptr->next             = NULL; /* hygiene */
    } else {
        for (cptr = sim_clock_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
            if (cptr->next == uptr) {
                nptr = cptr->next = uptr->next;
                uptr->next        = NULL; /* hygiene */
                break;                    /* end queue scan */
            }
        }
    }
    if (nptr != QUEUE_LIST_END)
        nptr->time += (uptr->next) ? 0 : uptr->time;
    if (!uptr->next)
        uptr->time = 0;
    uptr->usecs_remaining = 0;
    if (sim_clock_queue != QUEUE_LIST_END)
        sim_interval = sim_clock_queue->time;
    else
        sim_interval = NOQUEUE_WAIT;
    if (uptr->next) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Cancel failed for %s\n", sim_uname(uptr));
        SIM_SCP_ABORT(buf);
    }
    return SCPE_OK;
}

/* sim_is_active - test for entry in queue

   Inputs:
        uptr    =       pointer to unit
   Outputs:
        result =        TRUE if unit is busy, FALSE inactive
*/

t_bool sim_is_active(UNIT *uptr)
{
    return (((uptr->next) || (uptr->is_timer_unit ? sim_timer_is_active(uptr) : FALSE)) ? TRUE
                                                                                       : FALSE);
}

/* sim_activate_time - return activation time

   Inputs:
        uptr    =       pointer to unit
   Outputs:
        result =        absolute activation time + 1, 0 if inactive
*/

int32 _sim_activate_time(UNIT *uptr)
{
    UNIT *cptr;
    int32 accum = 0;

    for (cptr = sim_clock_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
        if (cptr == sim_clock_queue) {
            if (sim_interval > 0)
                accum = accum + sim_interval;
        } else
            accum = accum + cptr->time;
        if (cptr == uptr)
            return accum + 1 +
                   (int32)((uptr->usecs_remaining * sim_timer_inst_per_sec()) / 1000000.0);
    }
    return 0;
}

int32 sim_activate_time(UNIT *uptr)
{
    int32 accum;

    accum = _sim_timer_activate_time(uptr);
    if (accum >= 0)
        return accum;
    return _sim_activate_time(uptr);
}

/* Message Text */

const char *sim_error_text(t_stat stat)
{
    static char msgbuf[64];

    stat &= ~(SCPE_KFLAG | SCPE_NOMESSAGE); /* remove any flags */
    if (stat == SCPE_OK)
        return "No Error";
    if ((stat >= SCPE_BASE) && (stat <= SCPE_MAX_ERR))
        return scp_errors[stat - SCPE_BASE];
    sprintf(msgbuf, "Error %d", stat);
    return msgbuf;
}

/* This routine should ONLY be called from SCP modules */
void _sim_scp_abort(const char *msg, const char *file, int linenum)
{
    uint32 i, j;
    DEVICE *dptr;
    UNIT *uptr;

    sim_printf("%s - aborting from %s:%d\n", msg, file, linenum);
    for (i = 0; (dptr = sim_devices[i]) != NULL; i++) { /* flush attached files */
        for (j = 0; j < dptr->numunits; j++) {
            uptr = dptr->units + j;
            if ((uptr->flags & UNIT_ATT) && uptr->fileref && !(uptr->flags & UNIT_RO))
                fflush(uptr->fileref);
        }
    }
    sim_debug_close();
    abort();
}

/* Format a message and write it to stdout, and to sim_deb if it is open.
   With the console in raw mode a bare \n does not return the carriage, so
   while the machine is running every \n is expanded to \r\n. */

static void sim_emit(const char *buf)
{
    if (sim_is_running) {
        const char *c, *remnant = buf;

        while ((c = strchr(remnant, '\n'))) {
            if ((c != buf) && (*(c - 1) != '\r'))
                fprintf(stdout, "%.*s\r\n", (int)(c - remnant), remnant);
            else
                fprintf(stdout, "%.*s\n", (int)(c - remnant), remnant);
            remnant = c + 1;
        }
        fprintf(stdout, "%s", remnant);
    } else
        fprintf(stdout, "%s", buf);
}

/* Print message to stdout and sim_deb (if enabled) */
void sim_printf(const char *fmt, ...)
{
    char buf[CBUFSIZE];
    va_list arglist;

    va_start(arglist, fmt);
    vsnprintf(buf, sizeof(buf), fmt, arglist);
    va_end(arglist);

    sim_emit(buf);
    if (sim_deb && (sim_deb != stdout))
        fprintf(sim_deb, "%s", buf);
}

/* Print command result message to stdout and sim_deb (if enabled) */
t_stat sim_messagef(t_stat stat, const char *fmt, ...)
{
    char buf[CBUFSIZE];
    va_list arglist;
    t_bool inhibit_message = (stat & SCPE_NOMESSAGE) != 0;
    t_bool newline_prefix  = (*fmt == '\n');
    int prefix_len;

    if ((stat == SCPE_OK) && (sim_switches & SWMASK('Q')))
        return stat;
    if (newline_prefix)
        ++fmt;
    prefix_len = snprintf(buf, sizeof(buf), "%s%%SIM-%s: ",
                          newline_prefix ? (sim_is_running ? "\r\n" : "\n") : "",
                          (stat == SCPE_OK) ? "INFO" : "ERROR");
    va_start(arglist, fmt);
    vsnprintf(buf + prefix_len, sizeof(buf) - prefix_len, fmt, arglist);
    va_end(arglist);

    if (!inhibit_message)
        sim_emit(buf);
    /* Always display messages in debug output */
    if (sim_deb && ((sim_deb != stdout) || inhibit_message))
        fprintf(sim_deb, "%s", buf);

    return stat | ((stat != SCPE_OK) ? SCPE_NOMESSAGE : 0);
}

/* Character classification, called explicitly rather than through macros that
   shadow <ctype.h>.  These are not redundant wrappers: the BESM-6 feeds them
   bytes with the high bit set, and passing those to the libc isxxx() as a
   negative int is undefined behaviour.  sim_toupper is ASCII-only by design,
   independent of locale. */

int sim_isspace(int c)
{
    return ((c < 0) || (c >= 128)) ? 0 : isspace(c);
}

int sim_isalpha(int c)
{
    return ((c < 0) || (c >= 128)) ? 0 : isalpha(c);
}

int sim_toupper(int c)
{
    return ((c >= 'a') && (c <= 'z')) ? ((c - 'a') + 'A') : c;
}

/* The base name of a path, without directories and without the extension.
   The result is malloc'd and must be freed by the caller. */

char *sim_basename(const char *filepath)
{
    const char *name = strrchr(filepath, '/');
    const char *dot;
    size_t len;
    char *result;

    name   = name ? name + 1 : filepath;
    dot    = strrchr(name, '.');
    len    = dot ? (size_t)(dot - name) : strlen(name);
    result = (char *)malloc(len + 1);
    if (result == NULL)
        return NULL;
    memcpy(result, name, len);
    result[len] = '\0';
    return result;
}

/* Run the simulator until it stops.  What GO used to do, minus the command
 * parsing: bracket sim_instr() with console mode, signal handlers and wall
 * clock timing. */

t_stat sim_run(void)
{
    t_stat r;

    if ((r = sim_ttrun()) != SCPE_OK) { /* set console mode */
        r = sim_messagef(SCPE_TTYERR, "sim_ttrun() returned: %s - errno: %d - %s\n",
                         sim_error_text(r), errno, strerror(errno));
        sim_ttcmd();
        return r;
    }
#ifdef SIGHUP
    if (signal(SIGHUP, int_handler) == SIG_ERR) {
        r = sim_messagef(SCPE_SIGERR, "Can't establish SIGHUP: errno: %d - %s", errno,
                         strerror(errno));
        sim_ttcmd();
        return r;
    }
#endif
    if (signal(SIGTERM, int_handler) == SIG_ERR) {
        r = sim_messagef(SCPE_SIGERR, "Can't establish SIGTERM: errno: %d - %s", errno,
                         strerror(errno));
        sim_ttcmd();
        return r;
    }
    stop_cpu       = FALSE;
    sim_is_running = TRUE;
    fflush(stdout);

    r = sim_instr();

    if ((SCPE_BARE_STATUS(r) == SCPE_STOP) && sigterm_received)
        r = SCPE_SIGTERM;
    if (SCPE_BARE_STATUS(r) == SCPE_STOP) /* WRU exit: wait a bit for SIGINT */
        sim_os_ms_sleep(250);
    sim_is_running = FALSE;
    sim_ttcmd(); /* restore console */
#ifdef SIGHUP
    signal(SIGHUP, sigterm_received ? SIG_IGN : SIG_DFL);
#endif
    signal(SIGTERM, sigterm_received ? SIG_IGN : SIG_DFL);
    return r;
}
