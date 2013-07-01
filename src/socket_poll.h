#ifndef socket_poll_h
#define socket_poll_h

#include <stdbool.h>
#include <unistd.h>

#if defined(_WIN32) || defined(_WIN64)
#define USE_SELECT 1
#else

static void
closesocket(int fd) {
	close(fd);
}

#endif

#if USE_SELECT
struct select_pool;
typedef struct select_pool * poll_fd;
#else
typedef int poll_fd;
#endif

struct event {
	void * s;
	bool read;
	bool write;
};

static bool sp_invalid(poll_fd fd);
static poll_fd sp_create();
static poll_fd sp_release(poll_fd fd);
static int sp_add(poll_fd fd, int sock, void *ud);
static void sp_del(poll_fd fd, int sock);
static void sp_write(poll_fd, int sock, void *ud, bool enable);
static int sp_wait(poll_fd, struct event *e, int max, int timeout);
static void sp_nonblocking(int sock);

#ifdef __linux__
#include "socket_epoll.h"
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__NetBSD__)
#include "socket_kqueue.h"
#endif

#if defined(_WIN32) || defined(_WIN64)
#include "socket_select.h"
#endif

#endif
