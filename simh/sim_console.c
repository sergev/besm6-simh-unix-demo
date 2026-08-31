/* sim_console.c: simulator console I/O library
 *
 * stdin/stdout in raw mode, and nothing else.  ^E (sim_int_char) reaches the
 * simulator as SIGINT through termios VINTR, not through sim_poll_kbd().
 */

#include "sim_defs.h"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

int32 sim_int_char = 005; /* interrupt character */

static struct termios cmdtty, runtty;
static int cmdfl, runfl; /* TTY flags */

static t_bool sim_ttisatty(void)
{
    static int answer = -1;

    if (answer == -1)
        answer = isatty(0);
    return (t_bool)(answer != 0);
}

/* Poll for character */

t_stat sim_poll_kbd(void)
{
    unsigned char buf[1];

    if (!sim_ttisatty())
        return SCPE_OK;
    if (read(0, buf, 1) != 1)
        return SCPE_OK;
    return (buf[0] | SCPE_KFLAG);
}

/* Output character */

t_stat sim_putchar(int32 c)
{
    char ch = c;

    if (write(1, &ch, 1) != 1) {
        /* nothing useful to do about a full or closed stdout */
    }
    return SCPE_OK;
}

t_stat sim_ttinit(void)
{
    cmdfl = fcntl(0, F_GETFL, 0); /* get old flags  and status */
    /*
     * make sure systems with broken termios (that don't honor
     * VMIN=0 and VTIME=0) actually implement non blocking reads.
     * This will have no negative effect on other systems since
     * this is turned on and off depending on whether simulation
     * is running or not.
     */
    runfl = cmdfl | O_NONBLOCK;
    if (!sim_ttisatty()) /* skip if !tty */
        return SCPE_OK;
    if (tcgetattr(0, &cmdtty) < 0) /* get old flags */
        return SCPE_TTIERR;
    runtty         = cmdtty;
    runtty.c_lflag = runtty.c_lflag & ~(ECHO | ICANON); /* no echo or edit */
    runtty.c_oflag = runtty.c_oflag & ~OPOST;           /* no output edit */
    runtty.c_iflag = runtty.c_iflag & ~ICRNL;           /* no cr conversion */
    runtty.c_iflag = runtty.c_iflag & ~IGNCR;           /* don't ignore cr */
    runtty.c_iflag = runtty.c_iflag & ~IXANY;           /* don't restart after stop */
    runtty.c_iflag = runtty.c_iflag & ~IMAXBEL;         /* don't ring bell on input queue full */
    runtty.c_lflag = runtty.c_lflag & ~PENDIN;          /* don't retype pending input (state) */
    runtty.c_lflag = runtty.c_lflag | ECHOK;            /* echo NL after line kill */
    runtty.c_cc[VINTR]  = sim_int_char;                 /* interrupt */
    runtty.c_cc[VQUIT]  = 0;                            /* no quit */
    runtty.c_cc[VERASE] = 0;
    runtty.c_cc[VKILL]  = 0;
    runtty.c_cc[VEOF]   = 0;
    runtty.c_cc[VEOL]   = 0;
    runtty.c_cc[VSTART] = 0; /* no host sync */
    runtty.c_cc[VSUSP]  = 0;
    runtty.c_cc[VSTOP]  = 0;
#if defined(VREPRINT)
    runtty.c_cc[VREPRINT] = 0; /* no specials */
#endif
#if defined(VDISCARD)
    runtty.c_cc[VDISCARD] = 0;
#endif
#if defined(VWERASE)
    runtty.c_cc[VWERASE] = 0;
#endif
#if defined(VLNEXT)
    runtty.c_cc[VLNEXT] = 0;
#endif
    runtty.c_cc[VMIN]  = 0; /* no waiting */
    runtty.c_cc[VTIME] = 0;
#if defined(VDSUSP)
    runtty.c_cc[VDSUSP] = 0;
#endif
#if defined(VSTATUS)
    runtty.c_cc[VSTATUS] = 0;
#endif
    return SCPE_OK;
}

t_stat sim_ttrun(void)
{
    if (!sim_ttisatty()) /* skip if !tty */
        return SCPE_OK;
    (void)fcntl(0, F_SETFL, runfl);    /* non-block mode */
    runtty.c_cc[VINTR] = sim_int_char; /* in case changed */
    if (tcsetattr(0, TCSAFLUSH, &runtty) < 0)
        return SCPE_TTIERR;
    return SCPE_OK;
}

t_stat sim_ttcmd(void)
{
    if (!sim_ttisatty()) /* skip if !tty */
        return SCPE_OK;
    (void)fcntl(0, F_SETFL, cmdfl); /* block mode */
    if (tcsetattr(0, TCSAFLUSH, &cmdtty) < 0)
        return SCPE_TTIERR;
    return SCPE_OK;
}
