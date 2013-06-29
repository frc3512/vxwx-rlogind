/* rlogind */

#include <stddef.h>

#include <shellLib.h>
#include <ptyDrv.h>
#include <sigLib.h>

#include <errnoLib.h>
#include <ioLib.h>
#include <pipeDrv.h>
#include <taskLib.h>
#include <selectLib.h>
#include <sockLib.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <types.h>
#include <socket.h>
#include <netinet/sctp.h>
#include <netinet/in.h>

#include "rlogind.h"

/* ctors && dtors */
#if 0
extern void (*_ctors[])();
void (*_ctors[])() = { rlogind_entry };

extern void (*_dtors[])();
void (*_dtors[])() = { /* rlogind_destruct */ };
#endif

/* The program entry point */
int
rlogind_entry()
{
  int error;

  printf("rlogind starting...\n");

  error = taskSpawn("rlogind", 0 /* prio (0-255) */, 0, 5*1024*1024 /* 5M stack size */,
    (FUNCPTR)rlogind_main, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

  if(error == ERROR) return -1;

  return 0;
}

#if 0
/* Causes the rlogind task to clean up and exit */
int
rlogind_destruct()
{
  if(!rlogind_gstate) return -1;

  rlogind_gstate->exitnow = 1;

  return 0;
}
#endif

/* Prints a list of clients currently connected to
   the rlogind. */
int
rlogind_clientShow()
{
  int i;

  if(!rlogind_gstate) return -1;

  printf("index\thost\n");
  for(i = 0; i < RLOGIND_CONN_MAX; i++) {
    if(rlogind_gstate->fdlist[i].inuse) {
      printf("%d\t%s\n", i, inet_ntoa(rlogind_gstate->fdlist[i].cliaddr.sin_addr));
    }
  }

  return 0;
}

/* Disconnect a client from the rlogind. */
int
rlogind_disconnect(int index)
{
  /* Check the inputs */
  if(!rlogind_gstate) return -1;
  if(index >= RLOGIND_CONN_MAX) return -1;
  if(rlogind_gstate->fdlist[index].inuse == 0) return -1;

  /* Shutdown the connection */
  shutdown(rlogind_gstate->fdlist[index].socket, SHUT_RDWR);

  return 0;
}

/* Listens on a specified port and returns the listener file descriptor,
   or -1 on error. */
int
rlogind_listen(int port)
{
  int sd;
  int error;
  struct sockaddr_in addr;

  /* Create a socket */
  sd = socket(AF_INET, SOCK_STREAM, 0);
  if(sd < 1) return -1;

  /* Bind the socket to the address and port we
     want to listen on. */
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = INADDR_ANY;
  error = bind(sd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
  if(error != 0) {
    close(sd);
    return -1;
  }

  /* Start the socket listening with a backlog
     of ten connections. */
  error = listen(sd, 10);
  if(error != 0) {
    close(sd);
    return -1;
  }

  return sd;
}

/* Sets a socket to be non-blocking */
int
rlogind_setnonblock(int sd)
{
  int on;
  on = 1;

  return ioctl(sd, FIONBIO, (int) /* (void *) */ &on);
}

/* Called before the select(2). Sets read/write/except bits in the
   bitfields where acceptable. */
int
rlogind_setfds(struct rlogind_term_t *fdlist, int nelem,
  int *nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds)
{
  int i;

  for(i = 0; i < nelem; i++) {
    if(fdlist[i].inuse) {

      /* Make sure (*nfds) is correct */
      *nfds = maxn(*nfds, fdlist[i].socket);

      /* Select for reading on the socket */
      FD_SET(fdlist[i].socket, readfds);

      /* Select for exceptions on the socket. */
      FD_SET(fdlist[i].socket, exceptfds);
    }
  }

  return 0;
}

/* Perform the actual read operation. Called from a loop in
   rlogind_checkfds() . */
int
rlogind_doread(struct rlogind_term_t *fdlist, int nelem,
  int index, int pty)
{
  int nread;
  int offset;
  char rxbuf[RLOGIND_BUFSIZE];

  /* Recieve some bytes from the socket */
  nread = recv(fdlist[index].socket, rxbuf, RLOGIND_BUFSIZE, 0);
  offset = 0;

  /* Connection closed by remote host */
  if(nread < 1) {
#ifdef RLOGIND_DEBUG
    fprintf(stderr, "rlogind: connection dropped\n");
#endif
    rlogind_close(fdlist, nelem, index);
    return 0; /* ACHTUNG! */
  }

  /* Connecting */
  if(fdlist[index].state == 0 && nread-offset != 0) {
    /* Keep recieving bytes until we get four zeros */
    for(; offset < nread && fdlist[index].nullsrxd < 4; offset++) {
      if(rxbuf[offset] == '\0') fdlist[index].nullsrxd++;
    }

    /* We got (at least) four zeros */
    if(fdlist[index].nullsrxd >= 4) {
      /* Acknowledge that we've recieved the connection setup
         data, and change our state to connected. */
      if(send(fdlist[index].socket, "\0", 1, 0) != 1) {
        /* Uh-oh... */
        rlogind_close(fdlist, nelem, index);
        return 0; /* ACHTUNG! */
      }

      /* Put the client into raw mode */
      if(send(fdlist[index].socket, "\10", 1, MSG_OOB) != 1) {
        /* Uh-oh... */
        rlogind_close(fdlist, nelem, index);
        return 0; /* ACHTUNG! */
      }

#ifdef RLOGIND_DEBUG
    fprintf(stderr, "rlogind: connected\n");
#endif 
      fdlist[index].state = 1; /* Change state */
    }
  }

  /* Connected */
  if(fdlist[index].state == 1 && nread-offset != 0) {
    /* Write the bytes to the vty */
    write(pty, rxbuf+offset, nread-offset);
  }

  return 0;
}

/* Called after the select(2). Performs read/write/except actions on the
   sockets in fdlist. */
int
rlogind_checkfds(struct rlogind_term_t *fdlist, int nelem,
  int pty, fd_set *readfds, fd_set *writefds, fd_set *exceptfds)
{
  int i;

  for(i = 0; i < nelem; i++) {
    if(fdlist[i].inuse) {

      /* Something went wrong with this socket */
      if(FD_ISSET(fdlist[i].socket, exceptfds)) {
        rlogind_close(fdlist, nelem, i);
      }

      /* Is the socket ready for reading? */
      if(FD_ISSET(fdlist[i].socket, readfds)) {
        rlogind_doread(fdlist, nelem, i, pty);
      }

    }
  }

  return 0;
}

/* Write a buffer to all file descriptors in fdlist */
int
rlogind_writeall(struct rlogind_term_t *fdlist, int nelem,
  char *buf, int length)
{
  int i;
  int error;

  for(i = 0; i < nelem; i++) {
    if(fdlist[i].inuse && fdlist[i].state == 1) {
      error = send(fdlist[i].socket, buf, length, 0);
      if(error == -1) {
        rlogind_close(fdlist, nelem, i);
      }
    }
  }

  return 0;
}

/* Closes all of the open sockets */
int
rlogind_closeall(struct rlogind_term_t *fdlist, int nelem)
{
  int i;

  for(i = 0; i < nelem; i++) {
    if(fdlist[i].inuse) {
      rlogind_close(fdlist, nelem, i);
    }
  }

  return 0;
}

/* Returns the index in the table where the newly accepted socket
   was stored, or -1 on error. */
int
rlogind_accept(struct rlogind_term_t *fdlist, int nelem, int listener)
{
  int error;
  int newsd;
  int i;
  int addrlen;
  struct sockaddr_in cliaddr;

#ifdef RLOGIND_DEBUG
  fprintf(stderr, "rlogind: accepting new connection\n");
#endif

  /* Accept the connection */
  addrlen = sizeof(struct sockaddr_in);
  newsd = accept(listener, (struct sockaddr *) &cliaddr, &addrlen);
  if(newsd < 1) return -1;

  /* Set the new socket non-blocking */
  error = rlogind_setnonblock(newsd);
  if(error != 0) {
    close(newsd);
    return -1;
  }

  /* Find a table entry that's not in use. */
  for(i = 0; fdlist[i].inuse != 0 && i < nelem; i++);

  /* No more space in the table */
  if(i >= nelem) {
    close(newsd);
    errnoSet(EMFILE);
    return -1;
  }

  /* Add it into the table */
  fdlist[i].inuse = 1;
  fdlist[i].socket = newsd;
  fdlist[i].state = 0;
  fdlist[i].nullsrxd = 0;
  fdlist[i].cliaddr = cliaddr;

#ifdef RLOGIND_DEBUG
  fprintf(stderr, "rlogind: new connection in table\n");
#endif

  return i;
}

/* Returns the index in the table for the corresponding
   socket descriptor, or -1 on error. */
int
rlogind_lookupsd(struct rlogind_term_t *fdlist, int nelem, int sd)
{
  int i;

  /* Look up the sd */
  for(i = 0; fdlist[i].socket != sd && i < nelem; i++);

  /* It's not in the table */
  if(i >= nelem) {
    return -1;
  }

  return i;
}

/* Closes a the file descriptor at the specified index and
   removes it from the table. */
int
rlogind_close(struct rlogind_term_t *fdlist, int nelem, int index)
{
  if(index >= nelem) return -1;

#ifdef RLOGIND_DEBUG
  fprintf(stderr, "rlogind: closing connection\n");
#endif
  close(fdlist[index].socket);
  fdlist[index].inuse = 0;

  return 0;
}

/* Fills str with a random five character string containing
   characters between 'a' and 'z'. This is used by rlogind_pipe() . */
void
rlogind_randstring(char *str)
{
  int i;

  for(i = 0; i < 5; i++)
    str[i] = (rand()%25)+97;

  str[5] = 0;

  return;
}

/* Like the UNIX pipe(2) system call. Calls VxWorks's pipeDevCreate
   with a random name, opens the read and write ends, and fills
   pipefd. */
int
rlogind_pipe(int pipefd[2])
{
  char pipename[20];
  char tmpstr[6];

  int readfd;
  int writefd;

  rlogind_randstring(tmpstr);
  strcpy(pipename, "/pipe/");
  strcat(pipename, tmpstr);
  pipeDevCreate(pipename, 10, 512);

  readfd = open(pipename, O_RDONLY, 0);
  writefd = open(pipename, O_WRONLY, 0);
  if(readfd < 1 || writefd < 1) {
    close(readfd);
    close(writefd);
    return -1;
  }

  pipefd[0] = readfd;
  pipefd[1] = writefd;
  return 0;
}

/* Clean up rlogind's state before exit. */
void
rlogind_cleanup(struct rlogind_state_t *state)
{

  /* Restore the previous global file descriptor and associated
     options. */
  if(state->serialfd) {
    if(state->siooldopts >= 0)
      ioctl(state->serialfd, FIOSETOPTIONS, state->siooldopts);
    ioGlobalStdSet(0, state->serialfd);
    ioGlobalStdSet(1, state->serialfd);
    ioGlobalStdSet(2, state->serialfd);
  }

  /* Close the listening socket */
  if(state->listener) {
    close(state->listener);
    state->listener = 0;
  }

  /* Close the open ptys */
  if(state->pty) {
    close(state->pty);
    state->pty = 0;
  }
  if(state->ptyslave) {
    close(state->ptyslave);
    state->ptyslave = 0;
  }

  /* Remove the PTY device we created */
  ptyDevRemove("/dev/pty0.");

  /* Close all open socket descriptors */
  rlogind_closeall(state->fdlist, RLOGIND_CONN_MAX);

  return;
}

/* The program's main select loop */
int
rlogind_selectloop(struct rlogind_state_t *state)
{
  int nread;
  int nfds;
  int error;

  char txbuf[RLOGIND_BUFSIZE];
  char serialrxbuf[RLOGIND_BUFSIZE];

  fd_set readfds;
  fd_set writefds;
  fd_set exceptfds;

  /* select(2) loop */
  while(1) {
    nfds = 0;

    /* Zero the fdsets */
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);

    /* Wait for new connections */
    nfds = maxn(nfds, state->listener);
    FD_SET(state->listener, &readfds);

    /* select(2) for read on the vty */
    nfds = maxn(nfds, state->pty);
    FD_SET(state->pty, &readfds);

    /* select(2) for read on the serial port */
    if(state->serialfd > 0 && RLOGIND_USESERIAL) {
      nfds = maxn(nfds, state->serialfd);
      FD_SET(state->serialfd, &readfds);
    }

    /* Set the sockets into the bitfields */
    error = rlogind_setfds(state->fdlist, RLOGIND_CONN_MAX,
      &nfds, &readfds, &writefds, &exceptfds);
    if(error != 0) {
      perror("rlogind: rlogind_setfds()");
      rlogind_cleanup(state);
      return 1;
    }

    /* Perform the select(2) operation */
    error = select(nfds+1, &readfds, &writefds, &exceptfds, NULL);
    if(error == -1 && errno != EINTR) {
      perror("rlogind: select(2)");
      rlogind_cleanup(state);
      return 1;
    }

#ifdef RLOGIND_DEBUG
    fprintf(stderr, "rlogind: select returned\n");
#endif

    /* Check to see if were signalled to exit. */
    /*
    if(state->exitnow) {
      rlogind_cleanup(state);
      return 0;
    }
    */

    if(error != -1) {
      /* Look for new connections */
      if(FD_ISSET(state->listener, &readfds)) {
        error = rlogind_accept(state->fdlist, RLOGIND_CONN_MAX, state->listener);
        if(error < 0) {
          perror("rlogind: (warning) rlogind_accept()"); 
        }
      }

      /* Look for data on the vty */
      if(FD_ISSET(state->pty, &readfds)) {
        /* Read data from the vty */
        nread = read(state->pty, txbuf, RLOGIND_BUFSIZE);
        if(nread > 0) {

          /* Write it to the connected sockets */
          rlogind_writeall(state->fdlist, RLOGIND_CONN_MAX, txbuf, nread);

          /* Also write the data to the serial port, if it exists */
          if(state->serialfd > 0 && RLOGIND_USESERIAL) {
            write(state->serialfd, txbuf, nread);
          }
        }
      }

      /* Look for data on the serial port */
      if(FD_ISSET(state->serialfd, &readfds)) {
        nread = read(state->serialfd, serialrxbuf, RLOGIND_CONN_MAX);
        write(state->pty, serialrxbuf, nread);
      }

      /* Handle selected sockets */
      error = rlogind_checkfds(state->fdlist, RLOGIND_CONN_MAX,
        state->pty, &readfds, &writefds, &exceptfds);
      if(error != 0) {
        perror("rlogind: rlogind_checkfds()");
        rlogind_cleanup(state);
        return 1;
      }
    }

  }

  /* NOTREACHED */
  return 0;
}

/* rlogind's main function. This is usually spawned as a task
   by rlogind_entry(). */
int
rlogind_main()
{
  int error;

  struct rlogind_state_t state;

  /* Initialize the rlogind_term_t list with zeros. */
  memset(state.fdlist, 0, sizeof(struct rlogind_term_t) * RLOGIND_CONN_MAX);

  /* Initialize the rlogind_state_t structure with zeros. */
  memset(&state, 0, sizeof(struct rlogind_state_t));

  /* Store away the original global stdio file
     descriptor before we touch it. */
  state.serialfd = ioGlobalStdGet(0);
  state.siooldopts = ioctl(state.serialfd, FIOGETOPTIONS, 0);

  /* state.serialfd = 0; */

  /* Maybe there wasn't an original file descriptor */
  if(state.serialfd < 0) {
    state.serialfd = 0;
  }

  /* Clear the options on the serial port */
  if (state.serialfd != 0)
    ioctl(state.serialfd, FIOSETOPTIONS, 0);

  /* Create the PTY device */
  error = ptyDevCreate("/dev/pty0.", RLOGIND_BUFSIZE, RLOGIND_BUFSIZE);
  if(error != 0) {
    perror("rlogind: ptyDevCreate(2)");
    rlogind_cleanup(&state);
    return 1;
  }

  /* Open the master device of the PTY we just created. */
  state.pty = open("/dev/pty0.M", O_RDWR, 0);
  if(state.pty < 1) {
    perror("rlogind: open(2)");
    rlogind_cleanup(&state);
    return 1;
  }

  /* Open the slave device of the PTY we just created. */
  state.ptyslave = open("/dev/pty0.S", O_RDWR, 0);
  if(state.ptyslave < 1) {
    perror("rlogind: open(2)");
    rlogind_cleanup(&state);
    return 1;
  }

#if 1
  /* Redirect all I/O through us */
  ioGlobalStdSet(0, state.ptyslave); /* input */
  ioGlobalStdSet(1, state.ptyslave); /* output */
  ioGlobalStdSet(2, state.ptyslave); /* error */
#endif

#if 1
  shellGenericInit(NULL, 0, "rloginShell", NULL, TRUE, FALSE,
   state.ptyslave, state.ptyslave, state.ptyslave);
#endif

  /* Listen for connections */
  state.listener = rlogind_listen(RLOGIND_PORT);
  if(state.listener < 1) {
    perror("rlogind: rlogind_listen()");
    rlogind_cleanup(&state);
    return 1;
  }

  /* Set the global rlogind_state_t pointer */
  rlogind_gstate = &state;

  /* Do the main select loop */
  error = rlogind_selectloop(&state);

  /* Before we leave scope, set the global state pointer
     to NULL. */
  rlogind_gstate = NULL;

  /* rlogind_cleanup(&state); */ /* This is done in rlogind_selectloop already */
  return error;
}

