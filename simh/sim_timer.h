/* sim_timer.h: simulator timer library headers
*/

#ifndef SIM_TIMER_H_
#define SIM_TIMER_H_ 0

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

#define SIM_TMAX 500 /* max timer makeup */

#define SIM_INITIAL_IPS 5000000 /* uncalibrated assumption */
                                /* about instructions per second */

void sim_timer_init(void);
int32 sim_rtcn_init(int32 time);
int32 sim_rtcn_calb(uint32 ticksper);
time_t sim_get_time(void);
uint32 sim_os_msec(void);
void sim_os_ms_sleep(unsigned int msec);
t_stat sim_timer_activate(UNIT *uptr, int32 interval);
t_stat sim_timer_activate_after(UNIT *uptr, double usec_delay);
int32 _sim_timer_activate_time(UNIT *uptr);
t_bool sim_timer_is_active(UNIT *uptr);
t_stat sim_timer_cancel(UNIT *uptr);
t_stat sim_register_clock_unit(UNIT *uptr);
t_stat sim_clock_coschedule(UNIT *uptr, int32 interval);
double sim_timer_inst_per_sec(void);

#ifdef __cplusplus
}
#endif

#endif
