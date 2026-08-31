/* sim_defs.h: simulator definitions
*/

#ifndef SIM_DEFS_H_
#define SIM_DEFS_H_ 0

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

/* Length specific integer declarations */

typedef int32_t int32;
typedef uint8_t uint8;
typedef uint32_t uint32;

typedef int t_stat; /* status */
typedef int t_bool; /* boolean */

typedef signed long long t_int64;
typedef unsigned long long t_uint64;

/* The BESM-6 48-bit word requires a 64-bit t_value; there is no 32-bit build. */
typedef t_uint64 t_value; /* value */

/* System independent definitions */

#if !defined(PATH_MAX)      /* usually in limits */
#define PATH_MAX 512
#endif
#if (PATH_MAX >= 128)
#define CBUFSIZE (128 + PATH_MAX) /* string buf size */
#else
#define CBUFSIZE 256
#endif

/* Simulator status codes

   0                    ok
   1 - (SCPE_BASE - 1)  simulator specific
   SCPE_BASE - n        general
*/

#define SCPE_OK      0                /* normal return */
#define SCPE_BASE    64               /* base for messages */
#define SCPE_UNATT   (SCPE_BASE + 0)  /* no file */
#define SCPE_IOERR   (SCPE_BASE + 1)  /* I/O error */
#define SCPE_FMT     (SCPE_BASE + 2)  /* loader format */
#define SCPE_NOATT   (SCPE_BASE + 3)  /* not attachable */
#define SCPE_OPENERR (SCPE_BASE + 4)  /* open error */
#define SCPE_MEM     (SCPE_BASE + 5)  /* alloc error */
#define SCPE_ARG     (SCPE_BASE + 6)  /* argument error */
#define SCPE_UNK     (SCPE_BASE + 7)  /* unknown command */
#define SCPE_RO      (SCPE_BASE + 8)  /* read only */
#define SCPE_STOP    (SCPE_BASE + 9)  /* sim stopped */
#define SCPE_EXIT    (SCPE_BASE + 10) /* sim exit */
#define SCPE_TTIERR  (SCPE_BASE + 11) /* console tti err */
#define SCPE_NOPARAM (SCPE_BASE + 12) /* no parameters */
#define SCPE_ALATT   (SCPE_BASE + 13) /* already attached */
#define SCPE_SIGERR  (SCPE_BASE + 14) /* signal err */
#define SCPE_TTYERR  (SCPE_BASE + 15) /* tty setup err */
#define SCPE_NOFNC   (SCPE_BASE + 16) /* func not imp */
#define SCPE_NORO    (SCPE_BASE + 17) /* rd only not ok */
#define SCPE_INVSW   (SCPE_BASE + 18) /* invalid switch */
#define SCPE_MISVAL  (SCPE_BASE + 19) /* missing value */
#define SCPE_2FARG   (SCPE_BASE + 20) /* too few arguments */
#define SCPE_2MARG   (SCPE_BASE + 21) /* too many arguments */
#define SCPE_NXPAR   (SCPE_BASE + 22) /* nx parameter */
#define SCPE_IERR    (SCPE_BASE + 23) /* internal error */
#define SCPE_STALL   (SCPE_BASE + 24) /* Telnet conn stall */
#define SCPE_SIGTERM (SCPE_BASE + 25) /* SIGTERM has been received */

#define SCPE_MAX_ERR           (SCPE_BASE + 25) /* Maximum SCPE Error Value */
#define SCPE_KFLAG             0x10000000       /* tti data flag */
#define SCPE_NOMESSAGE         0x40000000       /* message display suppression flag */
#define SCPE_BARE_STATUS(stat) ((stat) & ~(SCPE_NOMESSAGE | SCPE_KFLAG))

/* Default timing parameters */

#define NOQUEUE_WAIT    1000000 /* min check time */

/* Convert switch letter to bit mask */

#define SWMASK(x) (1u << (((int)(x)) - ((int)'A')))

/* End of Linked List/Queue value                           */
/* Chosen for 2 reasons:                                    */
/*     1 - to not be NULL, this allowing the NULL value to  */
/*         indicate inclusion on a list                     */
/* and                                                      */
/*     2 - to not be a valid/possible pointer (alignment)   */
#define QUEUE_LIST_END ((UNIT *)1)

/* Typedefs for principal structures */

typedef struct DEVICE DEVICE;
typedef struct UNIT UNIT;

/* Device data structure */

struct DEVICE {
    const char *name;                            /* name */
    UNIT *units;                                 /* units */
    uint32 numunits;                             /* #units */
    t_stat (*reset)(DEVICE *dp);                 /* reset routine */
    t_stat (*detach)(UNIT *up);                  /* detach routine */
    uint32 dctrl;                                /* debug control */
};

/* Unit data structure

   Parts of the unit structure are device specific, that is, they are
   not referenced by the simulator control package and can be freely
   used by device simulators.  Fields starting with 'buf', and flags
   starting with 'UF', are device specific.  The definitions given here
   are for a typical sequential device.
*/

struct UNIT {
    UNIT *next;                 /* next active */
    t_stat (*action)(UNIT *up); /* action routine */
    char *filename;             /* open file name */
    FILE *fileref;              /* file reference */
    int32 time;                 /* time out */
    uint32 flags;               /* flags */
    t_bool is_timer_unit;       /* registered as the calibrated timer */
    t_bool (*cancel)(UNIT *);
    double usecs_remaining; /* time balance for long delays */
    char *uname;            /* Unit name */
    DEVICE *dptr;           /* DEVICE linkage (backpointer) */
    int32 wait;             /* wait */
};

/* Unit flags */

#define UNIT_V_UF    16 /* device specific */

#define UNIT_ATTABLE 0000001              /* attachable */
#define UNIT_RO      0000002              /* read only */
#define UNIT_ATT     0000020              /* attached */
#define UNIT_ROABLE  0001000              /* read only ok */

/* Function prototypes */

#include "scp.h"
#include "sim_console.h"
#include "sim_timer.h"

#if defined(assert)
#error "Don't use assert().  Report the error through sim_messagef() instead"
#else
#define assert(_Expression)                                                        \
    do {                                                                           \
        fprintf(stderr,                                                            \
                "Don't use assert().  Report the error through sim_messagef().\n"); \
        abort();                                                                   \
    } while (1)
#endif

#ifdef __cplusplus
}
#endif

#endif
