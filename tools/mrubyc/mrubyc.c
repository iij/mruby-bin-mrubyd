#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>

#include <err.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *sockpath = NULL;
int usock;

static int
mksockpath(char *buf, size_t buflen)
{
  const char *name = getenv("LOGIN");
  if (name != NULL)  {
    return snprintf(buf, buflen, "/tmp/mrubyd-%s", name);
  } else {
    return snprintf(buf, buflen, "/tmp/mrubyd-%d", (int)getuid());
  }
}

static void
send_fds(int sock)
{
  struct cmsghdr *cmsg;
  struct iovec iov;
  struct msghdr msg;
  union {
    struct cmsghdr hdr;
    char buf[CMSG_SPACE(sizeof(int))];
  } cmsgbuf;
  int i;
  char databuf[1];

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

  cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;

  for (i = 0; i < 3; i++) {
    *(int *)CMSG_DATA(cmsg) = i;
    if (sendmsg(sock, &msg, 0) == -1) {
      err(1, "sendmsg");
    }
  }
}

static void
usage(void)
{
  printf("mrubyc [-e script] [-p path] [-q] <script>\n");
}

int
main(int argc, char **argv)
{
  struct sockaddr_un sa_un;
  size_t len;
  int ch, quit, s, status;
  const char *script = NULL;
  char buf[1024+2];

  quit = 0;

  while ((ch = getopt(argc, argv, "e:p:q")) != -1) {
    switch (ch) {
    case 'e':
      if (strlen(optarg) > sizeof(buf) - 2) {
        err(1, "too long script (max=%zd)", sizeof(buf)-2);
      }
      script = optarg;
      break;
    case 'q':
      quit = 1;
      break;
    case '?':
    default:
      usage();
      exit(0);
      break;
    }
  }
  argc -= optind;
  argv += optind;

  if (argc != 1 && script == NULL && !quit) {
    usage();
    exit(1);
  }

  s = socket(AF_UNIX, SOCK_STREAM, 0);
  if (s == -1) {
    err(1, "socket");
  }

  memset(&sa_un, 0, sizeof(sa_un));
  sa_un.sun_family = AF_UNIX;
  sa_un.sun_len = sizeof(sa_un);
  mksockpath(sa_un.sun_path, sizeof(sa_un.sun_path));

  if (connect(s, (struct sockaddr *)&sa_un, sizeof(sa_un)) == -1) {
    err(1, "connect");
  }

  if (script != NULL) {
    len = snprintf(buf, sizeof(buf), "e%s", script);
  } else if (quit) {
    buf[0] = 'q';
    len = 1;
    printf("send quit message to mrubyd...\n");
  } else {
    len = snprintf(buf, sizeof(buf), "f%s", argv[0]);
  }

  if (send(s, buf, len, 0) != len) {
    err(1, "send");
  }
  send_fds(s);

  if (recv(s, &status, sizeof(status), MSG_WAITALL) == -1) {
    status = EXIT_FAILURE;
  }
  close(s);
  exit(status);
}
