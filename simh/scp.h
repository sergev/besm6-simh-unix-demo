/* scp.h: simulator control program headers
*/

#ifndef SIM_SCP_H_
#define SIM_SCP_H_ 0

#ifdef __cplusplus
extern "C" {
#endif

/* Framework startup and shutdown, for an entry point of one's own.  Pair
   them: sim_scp_init returning anything but SCPE_OK means stop.  See scp.c. */
t_stat sim_scp_init(int argc, char *argv[]);
t_stat sim_run(void);
int sim_scp_exit(t_stat stat);

/* Allow compiler to help validate printf style format arguments */
#if defined(__GNUC__)
#define GCC_FMT_ATTR(n, m) __attribute__((format(__printf__, n, m)))
#else
#define GCC_FMT_ATTR(n, m)
#endif

/* Event queue */

t_stat sim_process_event(void);
t_stat sim_activate(UNIT *uptr, int32 interval);
t_stat _sim_activate(UNIT *uptr, int32 interval);
t_stat sim_activate_after(UNIT *uptr, double usecs_walltime);
t_stat sim_cancel(UNIT *uptr);
t_bool sim_is_active(UNIT *uptr);
int32 sim_activate_time(UNIT *uptr);
int32 _sim_activate_time(UNIT *uptr);

/* Units and devices */

t_stat attach_unit(UNIT *uptr, const char *cptr);
t_stat detach_unit(UNIT *uptr);
const char *sim_uname(UNIT *dptr);
DEVICE *find_dev_from_unit(UNIT *uptr);

/* Text */

const char *get_sim_sw(const char *cptr);
const char *get_glyph(const char *iptr, char *optr, char mchar);
const char *sim_error_text(t_stat stat);
void sim_printf(const char *fmt, ...) GCC_FMT_ATTR(1, 2);
t_stat sim_messagef(t_stat stat, const char *fmt, ...) GCC_FMT_ATTR(2, 3);
char *sim_basename(const char *filepath);

/* Character classification, called explicitly rather than through macros that
   shadow <ctype.h>.  These are not redundant wrappers: the BESM-6 feeds them
   bytes with the high bit set, and passing those to the libc isxxx() as a
   negative int is undefined behaviour.  sim_toupper is ASCII-only by design,
   independent of locale. */

int sim_isspace(int c);
int sim_isalpha(int c);
int sim_toupper(int c);

/* Only for use in SCP code and libraries - NOT in simulator code */
#define SIM_SCP_ABORT(msg) _sim_scp_abort(msg, __FILE__, __LINE__)
void _sim_scp_abort(const char *msg, const char *filename, int filelinenum);

/* Global data */

extern int32 sim_interval;
extern int32 sim_switches;
extern FILE *sim_deb; /* debug file */
extern UNIT *sim_clock_queue;
extern volatile t_bool sim_is_running;
extern volatile t_bool stop_cpu;

/* VM interface */

extern DEVICE *sim_devices[];
extern const char *sim_stop_messages[SCPE_BASE];
extern t_stat sim_instr(void);

#ifdef __cplusplus
}
#endif

#endif
