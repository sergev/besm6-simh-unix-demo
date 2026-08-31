/* sim_tmxr.h: terminal multiplexer definitions
 *
 * A telnet listener per line, and nothing else.  Upstream's TMXR also spoke
 * to serial ports, UDP datagrams, packet streams, outgoing "virtual null
 * modem" connections and loopbacks, throttled lines to a baud rate, logged
 * each line to its own file and answered a dozen SET/SHOW commands.  The
 * BESM-6 uses one shape: `attach tty Line=<n>,<port>' opens a listener for
 * line n, and a telnet client that connects to it becomes that terminal.
 */

#ifndef SIM_TMXR_H_
#define SIM_TMXR_H_ 0

#ifdef __cplusplus
extern "C" {
#endif

#include "sim_defs.h"
#include "sim_sock.h"

#define TMXR_VALID (1 << 15)

typedef struct tmln TMLN;
typedef struct tmxr TMXR;

struct tmln {
    int conn;                /* line connected flag */
    SOCKET sock;             /* connection socket */
    char *ipad;              /* IP address of the peer */
    SOCKET master;           /* line specific listening socket */
    int32 tsta;              /* Telnet state */
    int32 rcve;              /* rcv enable */
    int32 xmte;              /* xmt enable */
    int32 dstb;              /* disable Telnet binary mode */
    uint8 *telnet_sent_opts; /* Telnet options we have sent a DON'T/WON'T for */
    int32 rxbpr;             /* rcv buf remove */
    int32 rxbpi;             /* rcv buf insert */
    int32 rxcnt;             /* rcv count */
    int32 txbpr;             /* xmt buf remove */
    int32 txbpi;             /* xmt buf insert */
    int32 txcnt;             /* xmt count */
    char *rxb;               /* rcv buffer */
    char *txb;               /* xmt buffer */
};

struct tmxr {
    int32 lines; /* # lines */
    TMLN *ldsc;  /* line descriptors */
};

t_stat tmxr_attach(TMXR *mp, UNIT *uptr, const char *cptr);
t_stat tmxr_detach(TMXR *mp, UNIT *uptr);
int32 tmxr_poll_conn(TMXR *mp);
void tmxr_poll_rx(TMXR *mp);
void tmxr_poll_tx(TMXR *mp);
int32 tmxr_getc_ln(TMLN *lp);
t_stat tmxr_putc_ln(TMLN *lp, int32 chr);
void tmxr_linemsg(TMLN *lp, const char *msg);
void tmxr_reset_ln(TMLN *lp);

#ifdef __cplusplus
}
#endif

#endif /* SIM_TMXR_H_ */
