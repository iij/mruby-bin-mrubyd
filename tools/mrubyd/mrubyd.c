#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <err.h>
#include <signal.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mruby.h"
#include "mruby/compile.h"

const char *sockpath = NULL;
int *childpipes = NULL;
int maxchilds = 5;


static const char *
allocsockpath(void)
{
  char *cp, *name;
  char buf[100];

  name = getenv("LOGIN");
  if (name != NULL)  {
    snprintf(buf, sizeof(buf), "/tmp/mrubyd-%s", name);
  } else {
    snprintf(buf, sizeof(buf), "/tmp/mrubyd-%d", (int)getuid());
  }

  cp = strdup(buf);
  if (cp == NULL) {
    errx(1, "no memory");
  }
  return cp;
}

static int
recvdesc(int sock)
{
  struct cmsghdr *cmsg;
  struct iovec iov;
  struct msghdr msg;
  union {
    struct cmsghdr  hdr;
    char            buf[CMSG_SPACE(sizeof(int))];
  } cmsgbuf;
  char databuf[1] = "-";

  memset(&iov, 0, sizeof(iov));
  iov.iov_base = databuf;
  iov.iov_len = sizeof(databuf);

  memset(&msg, 0, sizeof(msg));
  msg.msg_name = NULL;
  msg.msg_namelen = 0;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsgbuf.buf;
  msg.msg_controllen = sizeof(cmsgbuf.buf);
  if (recvmsg(sock, &msg, 0) == -1) {
    err(1, "recvmsg");
  }
  cmsg = CMSG_FIRSTHDR(&msg);
  if (cmsg == NULL || cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
      errx(1, "invalid error");
  }
  if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
    errx(1, "invalid cmsg");
  }
  return *(int *)CMSG_DATA(cmsg);
}

static int
child(mrb_state *mrb, int sock)
{
  mrbc_context *c;
  mrb_value v = mrb_nil_value();
  FILE *fp;
  ssize_t n;
  int exitcode;
  char buf[1000];

  n = recv(sock, buf, sizeof(buf)-1, MSG_WAITALL);
  if (n == -1) {
    err(1, "recv");
  }
  buf[n] = '\0';

  if (buf[0] == 'q') {
    killpg(0, SIGTERM);
    /* committing suicide... */
  }

  setsid();
  dup2(recvdesc(sock), 0);
  dup2(recvdesc(sock), 1);
  dup2(recvdesc(sock), 2);

  c = mrbc_context_new(mrb);
  if (buf[0] == 'e') {
    mrbc_filename(mrb, c, "-e");
    v = mrb_load_string_cxt(mrb, buf+1, c);
  } else if (buf[0] == 'f') {
    fp = fopen(buf+1, "r");
    if (fp == NULL) {
      err(1, "fopen");
    }
    mrbc_filename(mrb, c, buf);
    v = mrb_load_file_cxt(mrb, fp, c);
    fclose(fp);
  }
  mrbc_context_free(mrb, c);

  exitcode = EXIT_SUCCESS;
  if (mrb->exc) {
    if (!mrb_undef_p(v)) {
      mrb_print_error(mrb);
    }
    exitcode = EXIT_FAILURE;
  }
  mrb_close(mrb);

  return exitcode;
}

static void
closechildpipes(void)
{
  int i;

  for (i = 0; i < maxchilds; i++) {
    if (childpipes[i] == -1) {
      close(childpipes[i]);
      childpipes[i] = -1;
    }
  }
}

static void
spawn(mrb_state *mrb, int idx, int lsock)
{
  struct sockaddr_un sa_un;
  pid_t pid;
  socklen_t salen;
  int pipefds[2], sock, status;

  if (pipe(pipefds) == -1) {
    warnx("pipe");
    return;
  }

  pid = fork();
  if (pid == 0) {
    /*
     * child process
     */
    close(pipefds[0]);
    closechildpipes();

    setproctitle("%s", "listener");
    salen = sizeof(sa_un);
    sock = accept(lsock, (struct sockaddr *)&sa_un, &salen);
    if (sock == -1) {
      warn("accept");
      _exit(1);
    }
    close(lsock);
    close(pipefds[1]);  /* notify parent */

    setproctitle("%s", "worker");
    status = child(mrb, sock);
    (void)send(sock, &status, sizeof(status), MSG_DONTWAIT);
    _exit(0);
  } else if (pid == -1) {
    err(1, "fork");
  }

  childpipes[idx] = pipefds[0];
  close(pipefds[1]);
}

static void
usage(void)
{
  printf("usage: mrubyd [-c num] [-p path]\n");
}

int
main(int argc, char **argv)
{
  struct sockaddr_un sa_un;
  fd_set fds;
  mode_t oldmask;
  mrb_state *mrb;
  long l;
  int ch, i, maxfd, n, lsock;

  while ((ch = getopt(argc, argv, "c:hp:")) != -1) {
    switch (ch) {
    case 'c':
      l = strtol(optarg, NULL, 0);
      if (l < 1 || l > 10000) {
        errx(1, "-c is out of range (must be 1..10000)");
      }
      maxchilds = (int)l;
      break;
    case 'p':
      sockpath = optarg;
      break;
    case 'h':
    default:
      usage();
      exit(0);
    }
  }
  argc -= optind;
  argv += optind;

  if (sockpath == NULL) {
    sockpath = allocsockpath();
  }

  (void)unlink(sockpath);
  lsock = socket(AF_UNIX, SOCK_STREAM, 0);

  memset(&sa_un, 0, sizeof(sa_un));
  sa_un.sun_family = AF_UNIX;
  sa_un.sun_len = sizeof(sa_un);
  strncpy(sa_un.sun_path, sockpath, sizeof(sa_un.sun_path));

  oldmask = umask(0077);
  if (bind(lsock, (struct sockaddr *)&sa_un, sizeof(sa_un)) == -1) {
    err(1, "bind");
  }
  umask(oldmask);

  if (listen(lsock, 5) == -1) {
    err(1, "listen");
  }

  setenv("MRUBYD_SOCK", sockpath, 1);

  mrb = mrb_open();
  mrb_full_gc(mrb);

  childpipes = calloc(maxchilds, sizeof(int));
  if (childpipes == NULL) {
    err(1, "calloc");
  }
  for (i = 0; i < maxchilds; i++) {
    childpipes[i] = -1;
  }

  for (i = 0; i < maxchilds; i++) {
    spawn(mrb, i, lsock);
  }

  while (1) {
    FD_ZERO(&fds);
    maxfd = -1;
    for (i = 0; i < maxchilds; i++) {
      FD_SET(childpipes[i], &fds);
      if (maxfd < childpipes[i]) {
        maxfd = childpipes[i];
      }
    }
    n = select(maxfd+1, &fds, NULL, NULL, NULL);
    if (n == -1) {
      warnx("select");
    }
    for (i = 0; n > 0 && i < maxchilds; i++) {
      if (FD_ISSET(childpipes[i], &fds)) {
        close(childpipes[i]);
        spawn(mrb, i, lsock);
        n--;
      }

      while (waitpid(WAIT_ANY, NULL, WNOHANG) > 0) {
        /* remove zombies */
      } 
    }
  }

  /* NOTREAHED */
  exit(0);
}
