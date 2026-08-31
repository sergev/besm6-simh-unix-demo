/* sim_tmxr.c: Telnet terminal multiplexer library
 *
 * One listening socket per line; a telnet client that connects to it becomes
 * that terminal.  See sim_tmxr.h for what upstream's version did that this
 * one does not.
 */

#include "sim_defs.h"
#include "sim_sock.h"
#include "sim_timer.h"
#include "sim_tmxr.h"
#include "scp.h"

/* Telnet protocol constants */

/* Commands */
#define TN_IAC    0xFFu /* protocol delim */
#define TN_DONT   0xFEu /* dont */
#define TN_DO     0xFDu /* do */
#define TN_WONT   0xFCu /* wont */
#define TN_WILL   0xFBu /* will */
#define TN_SB     0xFAu /* subnegotiation begin */
#define TN_SE     0xF0u /* subnegotiation end */
#define TN_BRK    0xF3u /* break */

/* Options.  Only the four we negotiate need names; every other option number
   is rejected on sight, see tmxr_uninteresting_opt(). */

#define TN_BIN  0 /* binary */
#define TN_ECHO 1 /* echo */
#define TN_SGA  3 /* suppress go ahead */
#define TN_LINE 34 /* line mode */

#define TN_CR  015 /* carriage return */
#define TN_LF  012 /* line feed */
#define TN_NUL 000 /* null */

/* Telnet line states */

#define TNS_NORM  000 /* normal */
#define TNS_IAC   001 /* IAC seen */
#define TNS_WILL  002 /* WILL seen */
#define TNS_WONT  003 /* WONT seen */
#define TNS_SKIP  004 /* skip next cmd */
#define TNS_CRPAD 005 /* CR padding */
#define TNS_DO    006 /* DO request pending rejection */
#define TNS_SB    007 /* IAC SB seen: skipping a subnegotiation */
#define TNS_SBIAC 010 /* IAC seen inside a subnegotiation */

/* Telnet Option Sent Flags */

#define TNOS_DONT 001 /* Don't has been sent */
#define TNOS_WONT 002 /* Won't has been sent */

static u_char mantra[] = { /* Telnet Option Negotiation Mantra */
                           TN_IAC,  TN_WILL, TN_LINE, TN_IAC, TN_WILL, TN_SGA, TN_IAC, TN_WILL,
                           TN_ECHO, TN_IAC,  TN_WILL, TN_BIN, TN_IAC,  TN_DO,  TN_BIN
};

#define TMXR_MAXBUF 256 /* line buffer size */
#define TMXR_GUARD  16  /* headroom kept free for telnet escapes */

/* Options we reject rather than negotiate: everything but the four in the
   mantra.  Rejected once per option per connection -- a client that keeps
   asking would otherwise loop forever. */

static t_bool tmxr_uninteresting_opt(u_char opt)
{
    return (opt != TN_BIN) && (opt != TN_ECHO) && (opt != TN_SGA) && (opt != TN_LINE);
}

/* Count of characters buffered for transmit on a line */

static int32 tmxr_tqln(const TMLN *lp)
{
    return (lp->txbpi - lp->txbpr + ((lp->txbpi < lp->txbpr) ? TMXR_MAXBUF : 0));
}

/* Append one byte to the transmit ring, dropping the oldest on overrun. */

static void tmxr_txb_put(TMLN *lp, int32 chr)
{
    lp->txb[lp->txbpi++] = (char)chr;
    lp->txbpi %= TMXR_MAXBUF;
    if (lp->txbpi == lp->txbpr)
        lp->txbpr = (1 + lp->txbpr) % TMXR_MAXBUF;
}

/* Reply IAC <verb> <opt>.  The bare IAC has to bypass tmxr_putc_ln's
   IAC-doubling, so it goes into the buffer directly. */

static void tmxr_send_opt(TMLN *lp, u_char verb, u_char opt)
{
    tmxr_txb_put(lp, TN_IAC);
    tmxr_putc_ln(lp, verb);
    tmxr_putc_ln(lp, opt);
}

/* Initialize the line state.  A connected line stays connected. */

static void tmxr_init_line(TMLN *lp)
{
    lp->tsta  = 0; /* init telnet state */
    lp->xmte  = 1; /* enable transmit */
    lp->dstb  = 0; /* default bin mode */
    lp->rxbpr = lp->rxbpi = lp->rxcnt = 0;
    lp->txbpr = lp->txbpi = lp->txcnt = 0;
    if (lp->txb == NULL)
        lp->txb = (char *)malloc(TMXR_MAXBUF);
    if (lp->rxb == NULL)
        lp->rxb = (char *)malloc(TMXR_MAXBUF);
}

/* Read into the line's receive buffer: count read, 0 if none, -1 on error. */

static int32 tmxr_read(TMLN *lp, int32 length)
{
    return sim_read_sock(lp->sock, &(lp->rxb[lp->rxbpi]), length);
}

/* Write from the line's transmit buffer: count written, -1 on error. */

static int32 tmxr_write(TMLN *lp, int32 length)
{
    int32 written;

    if (!lp->sock)
        return 0;
    written = sim_write_sock(lp->sock, &(lp->txb[lp->txbpr]), length);
    if (written == SOCKET_ERROR)
        return -1;
    return written;
}

/* Remove a character from the receive buffer, sliding the rest down. */

static void tmxr_rmvrc(TMLN *lp, int32 p)
{
    for (; p < lp->rxbpi; p++)
        lp->rxb[p] = lp->rxb[p + 1];
    lp->rxbpi = lp->rxbpi - 1;
}

/* Send whatever is buffered for the line; returns the count still buffered. */

static int32 tmxr_send_buffered_data(TMLN *lp)
{
    int32 nbytes, sbytes;

    nbytes = tmxr_tqln(lp); /* avail bytes */
    if (nbytes) {           /* >0? write */
        if (lp->txbpr < lp->txbpi)
            sbytes = tmxr_write(lp, nbytes); /* no wrap: write it all */
        else
            sbytes = tmxr_write(lp, TMXR_MAXBUF - lp->txbpr); /* write to end of buf */
        if (sbytes < 0) {                                   /* I/O error? */
            lp->txbpi = lp->txbpr = 0;                      /* drop what we can't send */
            tmxr_reset_ln(lp);                              /* close line on error */
            return nbytes;
        }
        lp->txbpr = lp->txbpr + sbytes; /* update remove ptr */
        if (lp->txbpr >= TMXR_MAXBUF)     /* wrap? */
            lp->txbpr = 0;
        lp->txcnt = lp->txcnt + sbytes;
        nbytes    = nbytes - sbytes;
        if (nbytes && (lp->txbpr == 0)) { /* more data and wrapped? */
            sbytes = tmxr_write(lp, nbytes);
            if (sbytes > 0) {
                lp->txbpr = lp->txbpr + sbytes;
                if (lp->txbpr >= TMXR_MAXBUF)
                    lp->txbpr = 0;
                lp->txcnt = lp->txcnt + sbytes;
                nbytes    = nbytes - sbytes;
            }
        }
    }
    return nbytes;
}

/* Greet a newly connected client. */

static void tmxr_report_connection(TMXR *mp, TMLN *lp)
{
    char msgbuf[256];

    sprintf(msgbuf, "\n\r\nConnected to the BESM-6 simulator, line %d\r\n\n",
            (int)(lp - mp->ldsc));
    lp->rxcnt = lp->txcnt = 0; /* init counters */
    tmxr_linemsg(lp, msgbuf);
    tmxr_send_buffered_data(lp);
}

/* Poll every line's listener for a new connection.

   Returns the line number that just connected, or -1 if none did. */

int32 tmxr_poll_conn(TMXR *mp)
{
    SOCKET newsock;
    TMLN *lp;
    int32 i;
    char *address;

    for (i = 0; i < mp->lines; i++) {
        lp = mp->ldsc + i;
        if (!lp->master)
            continue;
        newsock = sim_accept_conn(lp->master, &address);
        if (newsock == INVALID_SOCKET)
            continue;
        if (lp->conn) { /* line already busy? */
            static const char busy[] = "Line connection busy\r\n";

            sim_write_sock(newsock, busy, (int32)(sizeof(busy) - 1));
            sim_close_sock(newsock);
            free(address);
            continue;
        }
        lp->conn = TRUE;
        lp->sock = newsock;
        free(lp->ipad);
        lp->ipad = address; /* peer address, for the connect message */
        tmxr_init_line(lp);
        sim_write_sock(newsock, (char *)mantra, sizeof(mantra));
        lp->telnet_sent_opts = (uint8 *)realloc(lp->telnet_sent_opts, 256);
        memset(lp->telnet_sent_opts, 0, 256);
        tmxr_report_connection(mp, lp);
        return i;
    }
    return -1;
}

/* Get a character from a line.  Returns TMXR_VALID | char, or 0 if none. */

int32 tmxr_getc_ln(TMLN *lp)
{
    t_stat val = 0;
    uint32 tmp;

    if (lp->conn && lp->rcve) {              /* connected and enabled? */
        if (lp->rxbpi - lp->rxbpr) {         /* any input chars? */
            tmp = lp->rxb[lp->rxbpr];        /* get char */
            val       = TMXR_VALID | (tmp & 0377); /* valid + chr */
            lp->rxbpr = lp->rxbpr + 1;                 /* adv pointer */
        }
    }
    if (lp->rxbpi == lp->rxbpr) /* empty? zero ptrs */
        lp->rxbpi = lp->rxbpr = 0;
    return val;
}

/* Poll all lines for input, stripping the telnet protocol out of what
   arrives before making it available to the simulator. */

void tmxr_poll_rx(TMXR *mp)
{
    int32 i, nbytes, j;
    TMLN *lp;

    for (i = 0; i < mp->lines; i++) {
        lp = mp->ldsc + i;
        if (!lp->sock || !lp->rcve) /* skip if not connected */
            continue;

        nbytes = 0;
        if (lp->rxbpi == 0)                                 /* need input? */
            nbytes = tmxr_read(lp, TMXR_MAXBUF - TMXR_GUARD); /* leave spc for telnet cruft */
        else if (lp->tsta)                                  /* mid telnet sequence? */
            nbytes = tmxr_read(lp, TMXR_MAXBUF - lp->rxbpi);  /* read to end */

        if (nbytes < 0) {              /* line error? */
            lp->txbpi = lp->txbpr = 0; /* drop what we know we can't send */
            tmxr_reset_ln(lp);         /* disconnect line */
            continue;
        }
        if (nbytes == 0)
            continue;

        j         = lp->rxbpi;          /* start of new data */
        lp->rxbpi = lp->rxbpi + nbytes; /* adv pointers */
        lp->rxcnt = lp->rxcnt + nbytes;

        while (j < lp->rxbpi) {              /* loop thru char */
            u_char tmp = (u_char)lp->rxb[j]; /* get char */
            switch (lp->tsta) {              /* case telnet state */

            case TNS_NORM:              /* normal */
                if (tmp == TN_IAC) {    /* IAC? */
                    lp->tsta = TNS_IAC; /* change state */
                    tmxr_rmvrc(lp, j);  /* remove char */
                    break;
                }
                if ((tmp == TN_CR) && lp->dstb) /* CR, no bin */
                    lp->tsta = TNS_CRPAD;       /* skip pad char */
                j = j + 1;                      /* advance j */
                break;

            case TNS_IAC:                /* IAC prev */
                if (tmp == TN_IAC) {     /* IAC + IAC */
                    lp->tsta = TNS_NORM; /* treat as normal */
                    j        = j + 1;    /* keep one IAC */
                    break;
                }
                if (tmp == TN_BRK) {   /* IAC + BRK? */
                    lp->tsta   = TNS_NORM;
                    lp->rxb[j] = 0; /* char is null */
                    j          = j + 1;
                    break;
                }
                switch (tmp) {
                case TN_WILL:
                    lp->tsta = TNS_WILL;
                    break;
                case TN_WONT:
                    lp->tsta = TNS_WONT;
                    break;
                case TN_DO:
                    lp->tsta = TNS_DO;
                    break;
                case TN_DONT:
                    lp->tsta = TNS_SKIP; /* IAC + other */
                    break;
                case TN_SB:              /* subnegotiation: skip to IAC SE */
                    lp->tsta = TNS_SB;
                    break;
                default:                 /* 2-byte command: ignore */
                    lp->tsta = TNS_NORM;
                    break;
                }
                tmxr_rmvrc(lp, j); /* remove char */
                break;

            case TNS_SB:            /* inside a subnegotiation */
                if (tmp == TN_IAC)
                    lp->tsta = TNS_SBIAC;
                tmxr_rmvrc(lp, j); /* remove char */
                break;

            case TNS_SBIAC:                              /* IAC inside a subnegotiation */
                lp->tsta = (tmp == TN_SE) ? TNS_NORM     /* end of subnegotiation */
                                          : TNS_SB;      /* escaped IAC in the payload */
                tmxr_rmvrc(lp, j);                       /* remove char */
                break;

            case TNS_WILL: /* IAC+WILL prev */
                if (tmxr_uninteresting_opt(tmp) &&
                    (0 == (lp->telnet_sent_opts[tmp] & TNOS_DONT))) {
                    tmxr_send_opt(lp, TN_DONT, tmp);
                    lp->telnet_sent_opts[tmp] |= TNOS_DONT; /* record DONT sent */
                }
                /* fall through */
            case TNS_WONT:         /* IAC+WILL/WONT prev */
                if (tmp == TN_BIN) /* BIN? */
                    lp->dstb = (lp->tsta == TNS_WILL) ? 0 : 1;
                tmxr_rmvrc(lp, j);   /* remove it */
                lp->tsta = TNS_NORM; /* next normal */
                break;

                /* RFC 854 requires "CR LF" or "CR NUL" in both directions in
                   ASCII mode, but not every client obeys.  Rather than
                   negotiate fully, look at what follows a CR in non-BIN mode
                   and strip it only if it is LF or NUL.  Conforming clients
                   are unaffected. */

            case TNS_CRPAD:          /* only LF or NUL should follow CR */
                lp->tsta = TNS_NORM; /* next normal */
                if ((tmp == TN_LF) || (tmp == TN_NUL))
                    tmxr_rmvrc(lp, j); /* remove it */
                break;

            case TNS_DO: /* pending DO request */
                if (tmxr_uninteresting_opt(tmp) &&
                    (0 == (lp->telnet_sent_opts[tmp] & TNOS_WONT))) {
                    tmxr_send_opt(lp, TN_WONT, tmp);
                    if (lp->conn)                               /* still connected? */
                        lp->telnet_sent_opts[tmp] |= TNOS_WONT; /* record WONT sent */
                }
                /* fall through */
            case TNS_SKIP:
            default:                 /* skip char */
                tmxr_rmvrc(lp, j);   /* remove char */
                lp->tsta = TNS_NORM; /* next normal */
                break;
            } /* end case state */
        } /* end for char */
    } /* end for lines */
    for (i = 0; i < mp->lines; i++) { /* if buf empty, reset pointers */
        lp = mp->ldsc + i;
        if (lp->rxbpi == lp->rxbpr)
            lp->rxbpi = lp->rxbpr = 0;
    }
}

/* Store a character in a line's transmit buffer. */

t_stat tmxr_putc_ln(TMLN *lp, int32 chr)
{
#define TXBUF_AVAIL(lp) (TMXR_MAXBUF - tmxr_tqln(lp))
    if ((lp->xmte == 0) && (TXBUF_AVAIL(lp) > 1))
        lp->xmte = 1;                        /* enable line transmit */
    if (lp->conn && (TXBUF_AVAIL(lp) > 1)) { /* connected and room for char (+ IAC)? */
        if (TN_IAC == (u_char)chr)           /* char == IAC in telnet session? */
            tmxr_txb_put(lp, TN_IAC);        /* stuff extra IAC char */
        tmxr_txb_put(lp, chr);               /* buffer char & adv pointer */
        if (TXBUF_AVAIL(lp) <= TMXR_GUARD)   /* near full? */
            lp->xmte = 0;                    /* disable until space available */
        if (!sim_is_running) {               /* attach message or other non-simulation output? */
            tmxr_send_buffered_data(lp);     /* put data on the wire */
            sim_os_ms_sleep(1);              /* wait an approximate character delay */
        }
        return SCPE_OK; /* char sent */
    }
    lp->xmte = 0;      /* no room, dsbl line */
    return SCPE_STALL; /* char not sent */
}

/* Send a string to a line, blocking until it all fits. */

void tmxr_linemsg(TMLN *lp, const char *msg)
{
    while (*msg) {
        while (SCPE_STALL == tmxr_putc_ln(lp, (int32)(*msg)))
            if (TMXR_MAXBUF == tmxr_send_buffered_data(lp))
                sim_os_ms_sleep(10);
        ++msg;
    }
}

/* Poll all lines for output. */

void tmxr_poll_tx(TMXR *mp)
{
    int32 i;
    TMLN *lp;

    for (i = 0; i < mp->lines; i++) {
        lp = mp->ldsc + i;
        if (!lp->conn) /* skip if not connected */
            continue;
        if (tmxr_send_buffered_data(lp) == 0) /* buf empty? enab line */
            lp->xmte = 1;
    }
}

/* Drop a line's connection.  Its listener stays open, so a client can
   reconnect. */

void tmxr_reset_ln(TMLN *lp)
{
    tmxr_send_buffered_data(lp); /* send any buffered data */

    if (lp->sock) {
        sim_close_sock(lp->sock); /* close socket */
        free(lp->telnet_sent_opts);
        lp->telnet_sent_opts = NULL;
        lp->sock             = 0;
        lp->conn             = FALSE;
        lp->xmte             = 1;
    }
    free(lp->ipad);
    lp->ipad = NULL;
    tmxr_init_line(lp); /* initialize line state */
}

/* Attach: open a listener for one line.

   The only form accepted is "Line=<n>,<port>", which is the one
   besm6_main.c uses. */

t_stat tmxr_attach(TMXR *mp, UNIT *uptr, const char *cptr)
{
    char gbuf[CBUFSIZE];
    const char *tptr = cptr;
    t_stat r;
    int32 line;
    TMLN *lp;
    SOCKET sock;

    tptr = get_glyph(tptr, gbuf, '=');
    if (strcmp(gbuf, "LINE") != 0)
        return sim_messagef(SCPE_ARG, "Expected Line=<line>,<port>, got: %s\n", cptr);
    if ((tptr == NULL) || (*tptr == '\0'))
        return sim_messagef(SCPE_2FARG, "Missing line specifier\n");

    tptr = get_glyph(tptr, gbuf, ',');
    {
        char *end;
        long val = strtol(gbuf, &end, 10);

        if ((end == gbuf) || (*end != '\0') || (val < 0) || (val >= mp->lines))
            return sim_messagef(SCPE_ARG, "Invalid line specifier: %s\n", gbuf);
        line = (int32)val;
    }
    if ((tptr == NULL) || (*tptr == '\0'))
        return sim_messagef(SCPE_2FARG, "Missing listen port\n");

    tptr = get_glyph(tptr, gbuf, 0);

    lp = mp->ldsc + line;
    if (lp->master)
        return sim_messagef(SCPE_ALATT, "Line %d already has a listener\n", (int)line);

    sock = sim_master_sock(gbuf, &r); /* make master socket */
    if (r != SCPE_OK)
        return sim_messagef(SCPE_ARG, "Invalid listen specification: %s\n", gbuf);
    if (sock == INVALID_SOCKET) /* open error */
        return sim_messagef(SCPE_OPENERR, "Can't listen on port: %s\n", gbuf);

    lp->master = sock; /* save master socket */
    tmxr_init_line(lp);

    uptr->filename = strdup(cptr);
    uptr->flags |= UNIT_ATT;
    return sim_messagef(SCPE_OK, "Line %d Listening on port %s\n", (int)line, gbuf);
}

/* Detach: close every listener and connection on the mux. */

t_stat tmxr_detach(TMXR *mp, UNIT *uptr)
{
    int32 i;
    TMLN *lp;

    if (!(uptr->flags & UNIT_ATT)) /* attached? */
        return SCPE_OK;
    for (i = 0; i < mp->lines; i++) {
        lp = mp->ldsc + i;
        tmxr_reset_ln(lp);
        if (lp->master) {
            sim_close_sock(lp->master); /* close master socket */
            lp->master = 0;
        }
    }
    free(uptr->filename); /* free setup string */
    uptr->filename = NULL;
    uptr->flags &= ~UNIT_ATT;
    return SCPE_OK;
}
