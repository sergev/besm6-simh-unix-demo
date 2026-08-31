/* sim_console.h: simulator console I/O library headers
*/

#ifndef SIM_CONSOLE_H_
#define SIM_CONSOLE_H_ 0

#ifdef __cplusplus
extern "C" {
#endif

t_stat sim_poll_kbd(void);
t_stat sim_putchar(int32 c);
t_stat sim_ttinit(void);
t_stat sim_ttrun(void);
t_stat sim_ttcmd(void);

/* Opened from BESM6_DEBUG, closed at exit; both live in scp.c. */
void sim_debug_close(void);

extern int32 sim_int_char;            /* interrupt character */

#ifdef __cplusplus
}
#endif

#endif
