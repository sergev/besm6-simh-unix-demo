/* sim_sock.h: OS-dependent socket routines header file
*/

#ifndef SIM_SOCK_H_
#define SIM_SOCK_H_ 0

#ifdef __cplusplus
extern "C" {
#endif

#include "sim_defs.h"
#include <sys/types.h>  /* for fcntl, getpid */
#include <sys/socket.h> /* for sockets */
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>  /* for sockaddr_in */
#include <netdb.h>

#define SOCKET         int
#define INVALID_SOCKET ((SOCKET) - 1)
#define SOCKET_ERROR   (-1)

SOCKET sim_master_sock(const char *hostport, int *parse_status);
SOCKET sim_accept_conn(SOCKET master, char **connectaddr);
int sim_read_sock(SOCKET sock, char *buf, int nbytes);
int sim_write_sock(SOCKET sock, const char *msg, int nbytes);
void sim_close_sock(SOCKET sock);

#ifdef __cplusplus
}
#endif

#endif
