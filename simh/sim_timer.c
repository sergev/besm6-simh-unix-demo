/* sim_timer.c: calibrated clock for the one timer BESM-6 registers
 *
 * The machine registers a single clock unit (besm6_cpu.c clocks[0], 250 Hz),
 * so the upstream array of SIM_NTIMERS timers collapses to one.  What is left
 * is the self-regulating loop in sim_rtcn_calb(), which turns wall-clock ticks
 * into an instruction count, plus the coscheduling that besm6_tty.c's vt_clk
 * rides on.
 *
 * Gone with the rest of SCP: idling, catch-up ticks, the asynchronous timer,
 * throttling, the ROM delay factor, the internal bootstrap timer, and the
 * shadow state that existed to survive a return to the sim> prompt.  The host
 * tick rate is pinned rather than measured -- upstream spent ~200 ms of
 * nanosleep at every startup to discover it.
 */

#include "sim_defs.h"
#include <time.h>

#define NANOS_PER_MILLI 1000000
#define MILLIS_PER_SEC  1000

/* Host timer characteristics, pinned.  1 ms is what the upstream probe measured
   on every supported host; keeping it a constant also keeps the calibrated timer
   selectable for any clock rate the machine uses. */
#define CLOCK_RESOLUTION_MS 1
#define CLK_TPS             100 /* assumed rate before the first calibration */

/* The single timer's state. */
static UNIT *clock_unit;              /* the registered ticking clock unit */
static UNIT tick_unit = { .uname = (char *)"INT-CLOCK" }; /* clock assist unit */
static UNIT *cosched_queue = QUEUE_LIST_END;
static int32 cosched_interval;
static uint32 rtc_ticks;   /* ticks this second */
static uint32 rtc_hz;      /* tick rate */
static uint32 rtc_last_hz; /* prior tick rate */
static uint32 rtc_rtime;   /* real time (msec) */
static uint32 rtc_vtime;   /* virtual time (msec) */
static uint32 rtc_nxintv;  /* next interval */
static int32 rtc_based;    /* base delay */
static int32 rtc_currd;    /* current delay */

static t_stat sim_timer_tick_svc(UNIT *uptr);
static t_bool _sim_coschedule_cancel(UNIT *uptr);

/* Host services */

uint32 sim_os_msec(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint32)((t_int64)(ts.tv_sec * 1000) + (t_int64)((ts.tv_nsec + 500000) / 1000000));
}

void sim_os_ms_sleep(unsigned int milliseconds)
{
    struct timespec treq;

    treq.tv_sec  = milliseconds / MILLIS_PER_SEC;
    treq.tv_nsec = (milliseconds % MILLIS_PER_SEC) * NANOS_PER_MILLI;
    (void)nanosleep(&treq, NULL);
}

/* Wall clock, for the machine's own date/time reads */

time_t sim_get_time(void)
{
    struct timespec ts_now;

    clock_gettime(CLOCK_REALTIME, &ts_now);
    return ts_now.tv_sec;
}

/* Instruction execution rate.  A double because it is mostly used in double
   expressions; never zero, so callers can divide by it. */

double sim_timer_inst_per_sec(void)
{
    double ips = (double)rtc_currd * rtc_hz;

    if (ips == 0.0) /* rate not calibrated yet? */
        ips = (double)rtc_currd * CLK_TPS;
    if (ips == 0.0)
        ips = SIM_INITIAL_IPS;
    return ips;
}

/* Calibrated clock */

int32 sim_rtcn_init(int32 time)
{
    if (time == 0)
        time = 1;
    /*
     * If we'd previously succeeded in calibrating a tick value, then use that
     * delay as a better default to setup when we're re-initialized.
     */
    if (rtc_currd)
        time = rtc_currd;
    rtc_rtime   = sim_os_msec();
    rtc_vtime   = rtc_rtime;
    rtc_nxintv  = 1000;
    rtc_ticks   = 0;
    rtc_last_hz = rtc_hz;
    rtc_hz      = 0;
    rtc_based   = time;
    rtc_currd   = time;
    return time;
}

int32 sim_rtcn_calb(uint32 ticksper)
{
    uint32 new_rtime, delta_rtime;
    int32 delta_vtime;

    if (rtc_hz != ticksper) { /* changing tick rate? */
        if ((rtc_last_hz != 0) && (rtc_last_hz != ticksper) && (ticksper != 0))
            rtc_currd = (int32)(sim_timer_inst_per_sec() / ticksper);
        rtc_last_hz = rtc_hz;
        rtc_hz      = ticksper;
    }
    if (ticksper == 0) /* running? */
        return 10000;
    rtc_ticks += 1;           /* count ticks */
    if (rtc_ticks < ticksper) /* 1 sec yet? */
        return rtc_currd;
    rtc_ticks = 0; /* reset ticks */
    new_rtime = sim_os_msec(); /* wall time */
    if (new_rtime < rtc_rtime) {
        /* Time running backwards: sim_os_msec() wrapped as a uint32, which
           happens roughly every 49 days.  Rebase and skip this calibration. */
        rtc_vtime = rtc_rtime = new_rtime;
        rtc_nxintv            = 1000;
        rtc_based             = rtc_currd;
        return rtc_currd;
    }
    delta_rtime = new_rtime - rtc_rtime; /* elapsed wtime */
    rtc_rtime   = new_rtime;             /* adv wall time */
    rtc_vtime += 1000;                   /* adv sim time */
    if (delta_rtime > 30000) {
        /* Gap too big: the process was suspended, or the host slept.  Ignore
           what happened and proceed from here. */
        rtc_vtime  = rtc_rtime;  /* sync virtual and real time */
        rtc_nxintv = 1000;       /* reset next interval */
        rtc_based  = rtc_currd;
        return rtc_currd; /* can't calibrate */
    }
    /* This self regulating algorithm depends directly on the assumption */
    /* that this routine is called back after processing the number of */
    /* instructions which was returned the last time it was called. */
    if (delta_rtime == 0)                 /* gap too small? */
        rtc_based = rtc_based * ticksper; /* slew wide */
    else
        rtc_based = (int32)(((double)rtc_based * (double)rtc_nxintv) /
                            ((double)delta_rtime)); /* new base rate */
    delta_vtime = rtc_vtime - rtc_rtime;            /* gap */
    if (delta_vtime > SIM_TMAX)                     /* limit gap */
        delta_vtime = SIM_TMAX;
    else {
        if (delta_vtime < -SIM_TMAX)
            delta_vtime = -SIM_TMAX;
    }
    rtc_nxintv = 1000 + delta_vtime;                                        /* next wtime */
    rtc_currd  = (int32)(((double)rtc_based * (double)rtc_nxintv) / 1000.0); /* next delay */
    if (rtc_based <= 0) /* never negative or zero! */
        rtc_based = 1;
    if (rtc_currd <= 0) /* never negative or zero! */
        rtc_currd = 1;
    return rtc_currd;
}

/* Clock assist: run the clock event, then release anything coscheduled on it */

static t_stat sim_timer_tick_svc(UNIT *uptr)
{
    t_stat stat;

    /*
     * Some devices may depend on executing during the same instruction or
     * immediately after the clock tick event.  To satisfy this, we directly
     * run the clock event here and if it completes successfully, schedule any
     * currently pending coschedule units to run now.  Ticks should never
     * return a non-success status, while co-schedule activities might, so
     * they are queued to run from sim_process_event
     */
    if (clock_unit->action == NULL)
        return SCPE_IERR;
    stat = clock_unit->action(clock_unit);
    --cosched_interval; /* Countdown ticks */

    if (cosched_queue != QUEUE_LIST_END)
        cosched_queue->time = cosched_interval;
    if ((stat == SCPE_OK) && (cosched_interval <= 0) && (cosched_queue != QUEUE_LIST_END)) {
        UNIT *sptr = cosched_queue;
        UNIT *cptr = QUEUE_LIST_END;

        /* Gather any queued events which are scheduled for right now */
        do {
            cptr          = cosched_queue;
            cosched_queue = cptr->next;
            if (cosched_queue != QUEUE_LIST_END) {
                cosched_queue->time += cosched_interval;
                cosched_interval = cosched_queue->time;
            } else
                cosched_interval = 0;
        } while ((cosched_interval <= 0) && (cosched_queue != QUEUE_LIST_END));
        if (cptr != QUEUE_LIST_END)
            cptr->next = QUEUE_LIST_END;
        /* Now dispatch that list (in order). */
        while (sptr != QUEUE_LIST_END) {
            cptr         = sptr;
            sptr         = sptr->next;
            cptr->next   = NULL;
            cptr->cancel = NULL;
            cptr->time   = 0;
            if (cptr->usecs_remaining > 0.0)
                stat = sim_timer_activate_after(cptr, cptr->usecs_remaining);
            else {
                cptr->usecs_remaining = 0.0;
                stat                  = _sim_activate(cptr, 0);
            }
            if (stat != SCPE_OK)
                break;
        }
    } else {
        if (cosched_queue == QUEUE_LIST_END)
            cosched_interval = 0;
    }
    return stat;
}

/* Scheduling */

t_stat sim_timer_activate(UNIT *uptr, int32 interval)
{
    double usecs = ((interval * 1000000.0) / sim_timer_inst_per_sec());

    /* Any clock with a very short delay (not a tick duration) will be put
       directly on the event queue */
    if (usecs <= (1000.0 * CLOCK_RESOLUTION_MS)) {
        if (!sim_is_active(uptr))
            uptr->usecs_remaining = 0.0;
        return _sim_activate(uptr, interval);
    } else
        return sim_timer_activate_after(uptr, usecs);
}

t_stat sim_timer_activate_after(UNIT *uptr, double usec_delay)
{
    int inst_delay;
    double inst_delay_d, inst_per_usec;
    t_stat stat;

    /* If this is the clock unit, schedule the related assist unit instead */
    if (clock_unit == uptr)
        uptr = &tick_unit;
    if (sim_is_active(uptr)) /* already active? */
        return SCPE_OK;
    if (usec_delay < 0.0) {
        char buf[128];

        snprintf(buf, sizeof(buf), "sim_timer_activate_after(%s, %.3f usecs) - negative delay",
                 sim_uname(uptr), usec_delay);
        SIM_SCP_ABORT(buf);
    }
    uptr->usecs_remaining = 0.0;
    /*
     * Handle long delays by aligning with the calibrated timer's calibration
     * activities.  Delays which would expire prior to the next calibration
     * are specifically scheduled directly based on the the current instruction
     * execution rate.  Longer delays are coscheduled to fire on the first tick
     * after the next calibration and at that time are either scheduled directly
     * or re-coscheduled for the next calibration time, repeating until the total
     * desired time has elapsed.
     */
    inst_per_usec = sim_timer_inst_per_sec() / 1000000.0;
    inst_delay_d  = floor(inst_per_usec * usec_delay);
    inst_delay    = (int32)inst_delay_d;
    if ((inst_delay == 0) && (usec_delay != 0))
        inst_delay_d = inst_delay = 1; /* Minimum non-zero delay is 1 instruction */
    if (rtc_hz) {                      /* calibrated timer running? */
        int32 inst_til_tick    = sim_activate_time(&tick_unit) - 1;
        int32 ticks_til_calib  = rtc_hz - rtc_ticks;
        double usecs_per_tick  = floor(1000000.0 / rtc_hz);
        int32 inst_til_calib   = inst_til_tick + ((ticks_til_calib - 1) * rtc_currd);
        uint32 usecs_til_calib = (uint32)ceil(inst_til_calib / inst_per_usec);

        if ((uptr != &tick_unit) &&                  /* Not scheduling the assist unit */
            (inst_til_tick > 0)) {                   /* and tick not pending? */
            if (inst_delay_d > (double)inst_til_calib) { /* very long wait (certainly after the
                                                            next calibration)? */
                stat                  = sim_clock_coschedule(uptr, 0);
                uptr->usecs_remaining = (stat == SCPE_OK) ? usec_delay - usecs_til_calib : 0.0;
                return stat;
            }
            if ((usec_delay > (2 * usecs_per_tick)) && (ticks_til_calib > 1)) { /* long wait? */
                double usecs_til_tick = floor(inst_til_tick / inst_per_usec);

                uptr->usecs_remaining = 0.0;
                stat                  = sim_clock_coschedule(uptr, 0);
                if (stat == SCPE_OK)
                    uptr->usecs_remaining = usec_delay - usecs_til_tick;
                return stat;
            }
        }
    }
    /*
     * Bound delay to avoid overflow.  Long delays are usually canceled before
     * they expire, however bounding the delay will cause sim_activate_time to
     * return inconsistent results when truncation has happened.
     */
    if (inst_delay_d > (double)0x7fffffff) {
        usec_delay   = (inst_delay_d - (double)0x7fffffff) / inst_per_usec;
        inst_delay_d = (double)0x7fffffff;
    } else
        usec_delay = 0.0;
    inst_delay            = (int32)inst_delay_d;
    usec_delay            = uptr->usecs_remaining;
    uptr->usecs_remaining = 0.0;
    stat                  = _sim_activate(uptr, inst_delay); /* queue it now */
    uptr->usecs_remaining = usec_delay;
    return stat;
}

/* Clock coscheduling */

t_stat sim_register_clock_unit(UNIT *uptr)
{
    if (clock_unit == NULL)
        cosched_queue = QUEUE_LIST_END;
    clock_unit          = uptr;
    uptr->is_timer_unit = TRUE;
    return SCPE_OK;
}

/* ticks - 0 means on the next tick, 1 means the second tick, etc. */

t_stat sim_clock_coschedule(UNIT *uptr, int32 interval)
{
    int32 tick_size = rtc_currd ? rtc_currd : 10000;
    int32 ticks     = (interval + (tick_size / 2)) / tick_size;
    UNIT *cptr, *prvptr;
    int32 accum;

    if (ticks < 0)
        return SCPE_ARG;
    if (sim_is_active(uptr))
        return SCPE_OK;
    if ((clock_unit == NULL) || (rtc_hz == 0))
        return sim_activate(uptr, ticks * tick_size);

    if (cosched_queue != QUEUE_LIST_END)
        cosched_queue->time = cosched_interval;
    prvptr = NULL;
    accum  = 0;
    for (cptr = cosched_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
        if (ticks < (accum + cptr->time))
            break;
        accum += cptr->time;
        prvptr = cptr;
    }
    if (prvptr == NULL) {
        cptr = uptr->next = cosched_queue;
        cosched_queue     = uptr;
    } else {
        cptr = uptr->next = prvptr->next;
        prvptr->next      = uptr;
    }
    uptr->time = ticks - accum;
    if (cptr != QUEUE_LIST_END)
        cptr->time = cptr->time - uptr->time;
    uptr->cancel     = &_sim_coschedule_cancel; /* bind cleanup method */
    cosched_interval = cosched_queue->time;
    return SCPE_OK;
}

/* Cancel a unit on the coschedule queue */

static t_bool _sim_coschedule_cancel(UNIT *uptr)
{
    UNIT *nptr = QUEUE_LIST_END;

    if (!uptr->next || (clock_unit == NULL)) /* On a queue? */
        return FALSE;
    if (uptr == cosched_queue) {
        nptr = cosched_queue = uptr->next;
        uptr->next           = NULL;
        if (cosched_queue != QUEUE_LIST_END)
            cosched_interval = cosched_queue->time;
    } else {
        UNIT *cptr;

        for (cptr = cosched_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
            if (cptr->next == uptr) {
                nptr = cptr->next = uptr->next;
                uptr->next        = NULL;
                break;
            }
        }
    }
    if (uptr->next != NULL) /* not found? */
        return FALSE;
    uptr->cancel          = NULL;
    uptr->usecs_remaining = 0.0;
    if (nptr != QUEUE_LIST_END)
        nptr->time += uptr->time;
    uptr->time = 0;
    return TRUE;
}

t_bool sim_timer_is_active(UNIT *uptr)
{
    if (!uptr->is_timer_unit || (clock_unit != uptr))
        return FALSE;
    return sim_is_active(&tick_unit);
}

t_stat sim_timer_cancel(UNIT *uptr)
{
    if (!uptr->is_timer_unit || (clock_unit != uptr))
        return SCPE_IERR;
    return sim_cancel(&tick_unit);
}

int32 _sim_timer_activate_time(UNIT *uptr)
{
    UNIT *cptr;
    int32 accum = 0;

    if (uptr->cancel == &_sim_coschedule_cancel) {
        for (cptr = cosched_queue; cptr != QUEUE_LIST_END; cptr = cptr->next) {
            if (cptr == cosched_queue) {
                if (cosched_interval > 0)
                    accum += cosched_interval;
            } else
                accum += cptr->time;
            if (cptr == uptr)
                return (rtc_currd * accum) + sim_activate_time(&tick_unit);
        }
    }
    if ((uptr == &tick_unit) && uptr->next)
        return _sim_activate_time(&tick_unit);
    return -1; /* Not found. */
}

/* Startup */

void sim_timer_init(void)
{
    cosched_queue    = QUEUE_LIST_END;
    tick_unit.action = &sim_timer_tick_svc;
}
