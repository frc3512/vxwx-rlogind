/* rlogind */

#ifndef _RLOGIND_H
#define _RLOGIND_H

#include <selectLib.h>
#include <sockLib.h>
#include <netinet/sctp.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configurable parameters */
#define RLOGIND_PORT 513
#define RLOGIND_CONN_MAX 10
#define RLOGIND_BUFSIZE 1024
#define RLOGIND_USESERIAL 1
#define RLOGIND_AUTORLOGIN_PORT 35120
#define RLOGIND_AUTORLOGIN_TRIES 6
/* #define RLOGIND_DEBUG */

struct rlogind_term_t {
  /* Is this index is allocated to a socket?
     0 = no
     1 = yes */
  int inuse;

  /* Have we finished setting up the rlogin connection?
     0 = setting up
     1 = connected/ready to transfer data */
  int state;

  /* The number of NULL (0x00) characters received so
     far during the connection setup process. This is
     only meaningful when state == 0 */
  int nullsrxd;

  /* The buffer to recieve into */
  /* char rxbuf[RLOGIND_BUFSIZE];
  int pos;
  int length; */

  /* The client's address */
  struct sockaddr_in cliaddr;

  /* The TCP socket we're using */
  int socket;
};

struct rlogind_state_t {
  /* Set to cause the rlogind task to exit */
  /* int exitnow; */

  /* The former global file descriptor and
     it's former options */
  int serialfd;
  int siooldopts;

  /* The master and slave pty. The slave pty
     is set to the new global file descrpitor. */
  int pty;
  int ptyslave;

  /* The listener socket */
  int listener;

  /* A list of connections open to rlogind */
  struct rlogind_term_t fdlist[RLOGIND_CONN_MAX];
};

/* Returns the larger of x and y */
#define maxn(x, y) ((x > y) ? x : y)

/* A global pointer to the rlogind state object. */
struct rlogind_state_t *rlogind_gstate;

int rlogind_entry(void);
/* int rlogind_destruct(void); */
int rlogind_clientShow(void);
int rlogind_disconnect(int index);
int rlogind_listen(int port);
int rlogind_setnonblock(int sd);
int rlogind_setfds(struct rlogind_term_t *fdlist, int nelem, int *nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds);
int rlogind_doread(struct rlogind_term_t *fdlist, int nelem, int index, int pty);
int rlogind_checkfds(struct rlogind_term_t *fdlist, int nelem, int vtytx, fd_set *readfds, fd_set *writefds, fd_set *exceptfds);
int rlogind_writeall(struct rlogind_term_t *fdlist, int nelem, char *buf, int length);
int rlogind_closeall(struct rlogind_term_t *fdlist, int nelem);
int rlogind_accept(struct rlogind_term_t *fdlist, int nelem, int listener);
int rlogind_lookupsd(struct rlogind_term_t *fdlist, int nelem, int sd);
int rlogind_close(struct rlogind_term_t *fdlist, int nelem, int index);
void rlogind_randstring(char *str);
int rlogind_pipe(int pipefd[2]);
int rlogind_sendbroadcast(void);
void rlogind_cleanup(struct rlogind_state_t *state);
int rlogind_main(void);

#ifdef __cplusplus
}
#endif

#endif /* _RLOGIND_H */

