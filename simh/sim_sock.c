/* sim_sock.c: OS-dependent socket routines
*/

#include "sim_sock.h"

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif

static SOCKET sim_err_sock(SOCKET s, const char *emsg)
{
    sim_printf("Sockets: %s error %d - %s\n", emsg, errno, strerror(errno));
    if (s != INVALID_SOCKET) {
        int err = errno;
        sim_close_sock(s);
        errno = err; /* Retain Original socket error value */
    }
    return INVALID_SOCKET;
}

/* Split "host:port" (or a bare port) into its parts.  Returns 0 on success,
   -1 if the port is not a number in 1..65535 or a buffer is too small. */

static int sim_parse_addr(const char *cptr, char *host, size_t host_len, char *port,
                          size_t port_len)
{
    char gbuf[CBUFSIZE];
    const char *hostp;
    char *portp, *endc;
    unsigned long portval;

    memset(host, 0, host_len);
    memset(port, 0, port_len);
    gbuf[sizeof(gbuf) - 1] = '\0';
    strncpy(gbuf, cptr, sizeof(gbuf) - 1);
    hostp = gbuf;                       /* default addr */
    if ((portp = strrchr(gbuf, ':')))   /* x:y? split */
        *portp++ = 0;
    else {                              /* no colon: the whole thing is the port */
        portp = gbuf;
        hostp = NULL;
    }
    if (*portp == '\0')
        return -1;
    portval = strtoul(portp, &endc, 10);
    if ((*endc != '\0') || (portval == 0) || (portval > 65535))
        return -1;
    if (strlen(portp) >= port_len)
        return -1; /* no room */
    strcpy(port, portp);
    if (hostp != NULL) {
        if (strlen(hostp) >= host_len)
            return -1; /* no room */
        strcpy(host, hostp);
    }
    return 0;
}

static int sim_setnonblock(SOCKET sock)
{
    int fl, sta;

    fl = fcntl(sock, F_GETFL, 0); /* get flags */
    if (fl == -1)
        return SOCKET_ERROR;
    sta = fcntl(sock, F_SETFL, fl | O_NONBLOCK); /* set nonblock */
    if (sta == -1)
        return SOCKET_ERROR;
    sta = fcntl(sock, F_SETOWN, getpid()); /* set ownership */
    if (sta == -1)
        return SOCKET_ERROR;
    return 0;
}

/* Open a listening socket on "host:port" (or a bare port).  The listener is a
   dual-stack IPv6 socket where the host has one, so a telnet client reaching
   it over IPv4 arrives as an IPv4-mapped address. */

SOCKET sim_master_sock(const char *hostport, int *parse_status)
{
    SOCKET newsock = INVALID_SOCKET;
    int sta;
    char host[CBUFSIZE], port[CBUFSIZE];
    int r;
    struct addrinfo hints;
    struct addrinfo *result = NULL, *preferred;

    r = sim_parse_addr(hostport, host, sizeof(host), port, sizeof(port));
    if (parse_status)
        *parse_status = r;
    if (r)
        return newsock;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags    = AI_PASSIVE;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host[0] ? host : NULL, port[0] ? port : NULL, &hints, &result)) {
        if (parse_status)
            *parse_status = -1;
        return newsock;
    }
    /* Prefer the IPv6 addrinfo, so that one socket serves both families. */
    for (preferred = result; preferred != NULL; preferred = preferred->ai_next) {
        if (preferred->ai_family == AF_INET6)
            break;
    }
    if (preferred == NULL)
        preferred = result;
retry:
    newsock = socket(preferred->ai_family, SOCK_STREAM, 0);
    if (newsock == INVALID_SOCKET) { /* socket error? */
        if ((preferred->ai_family == AF_INET6) && (preferred != result)) {
            preferred = result; /* no IPv6 here; fall back to IPv4 */
            goto retry;
        }
        freeaddrinfo(result);
        if (errno != EAFNOSUPPORT)
            sim_err_sock(newsock, "socket");
        return INVALID_SOCKET;
    }
    if (preferred->ai_family == AF_INET6) {
        int off = 0;
        (void)setsockopt(newsock, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&off, sizeof(off));
    }
    {
        /* Always set SO_REUSEADDR.  Upstream gated it on a `-U' switch that
           nothing in this tree can set, which left a listener unable to rebind
           its port until the last accepted connection left TIME_WAIT -- a
           minute of "Address already in use" after every telnet session. */
        int on = 1;

        (void)setsockopt(newsock, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
    }
    sta = bind(newsock, preferred->ai_addr, preferred->ai_addrlen);
    freeaddrinfo(result);
    if (sta == SOCKET_ERROR) /* bind error? */
        return sim_err_sock(newsock, "bind");
    sta = sim_setnonblock(newsock); /* set nonblocking */
    if (sta == SOCKET_ERROR)        /* fcntl error? */
        return sim_err_sock(newsock, "setnonblock");
    sta = listen(newsock, 1); /* listen on socket */
    if (sta == SOCKET_ERROR)  /* listen error? */
        return sim_err_sock(newsock, "listen");
    return newsock; /* got it! */
}

SOCKET sim_accept_conn(SOCKET master, char **connectaddr)
{
    int sta       = 0, err;
    int keepalive = 1;
    socklen_t size;
    SOCKET newsock;
    struct sockaddr_storage clientname;

    if (master == 0) /* not attached? */
        return INVALID_SOCKET;
    size = sizeof(clientname);
    memset(&clientname, 0, sizeof(clientname));
    newsock = accept(master, (struct sockaddr *)&clientname, &size);
    if (newsock == INVALID_SOCKET) { /* error? */
        err = errno;
        if (err != EWOULDBLOCK)
            sim_err_sock(newsock, "accept");
        return INVALID_SOCKET;
    }
    if (connectaddr != NULL) {
        *connectaddr = (char *)calloc(1, NI_MAXHOST + 1);
        getnameinfo((struct sockaddr *)&clientname, size, *connectaddr, NI_MAXHOST, NULL, 0,
                    NI_NUMERICHOST);
        if (0 == memcmp("::ffff:", *connectaddr, 7)) /* is this a IPv4-mapped IPv6 address? */
            memmove(*connectaddr, 7 + *connectaddr,  /* prefer bare IPv4 address */
                    strlen(7 + *connectaddr) + 1);   /* length to include terminating \0 */
    }

    sta = sim_setnonblock(newsock); /* set nonblocking */
    if (sta == SOCKET_ERROR)        /* fcntl error? */
        return sim_err_sock(newsock, "setnonblock");

    /* enable TCP Keep Alives */
    sta = setsockopt(newsock, SOL_SOCKET, SO_KEEPALIVE, (char *)&keepalive, sizeof(keepalive));
    if (sta == -1)
        return sim_err_sock(newsock, "setsockopt KEEPALIVE");

    return newsock;
}

int sim_read_sock(SOCKET sock, char *buf, int nbytes)
{
    int rbytes, err;

    rbytes = recv(sock, buf, nbytes, 0);
    if (rbytes == 0) /* disconnect */
        return -1;
    if (rbytes == SOCKET_ERROR) {
        err = errno;
        if ((err == EWOULDBLOCK) || (err == EAGAIN)) /* no data */
            return 0;
        if ((err != ECONNABORTED) && (err != ECONNRESET) && /* peer went away */
            (err != EINTR))
            sim_err_sock(INVALID_SOCKET, "read");
        return -1;
    }
    return rbytes;
}

int sim_write_sock(SOCKET sock, const char *msg, int nbytes)
{
    int err, sbytes = send(sock, msg, nbytes, 0);

    if (sbytes == SOCKET_ERROR) {
        err = errno;
        if ((err == EWOULDBLOCK) || (err == EAGAIN)) /* no data */
            return 0;
    }
    return sbytes;
}

void sim_close_sock(SOCKET sock)
{
    shutdown(sock, SHUT_RDWR);
    close(sock);
}
