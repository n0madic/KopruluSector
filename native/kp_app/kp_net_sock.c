/* The provider's sockets over the system's UDP (kp_net_sock.h; phase N, stage N2). */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L /* clock_gettime and the sockets under -std=c99 */
#endif
#include "kp_net_sock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOOPBACK_IP 0x7f000001u /* 127.0.0.1 */
#define UNMAPPED_SELF 0x7f000002u /* 127.0.0.2: to itself, a program the map does not hold */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET Fd;
#define NO_FD INVALID_SOCKET
#define close_fd closesocket
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#elif !defined(__EMSCRIPTEN__)
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int Fd;
#define NO_FD (-1)
#define close_fd close
#else
#include <time.h>
#endif

uint32_t kp_net_sock_now(void)
{
#ifdef _WIN32
	return (uint32_t)GetTickCount();
#else
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u);
#endif
}

/* The address's name and port, a dotted IPv4 name as its address: 1 resolved, 0 a name for the resolver, -1 no address
 * (the reason in error). */
static int parse_host(const char *text, KpNetAddr *out, char *name, uint16_t *port, char *error, size_t size)
{
	uint32_t ip;
	if (!kp_net_host_parse(text, name, KP_NET_HOST, port)) {
		if (size) snprintf(error, size, "\"%s\" is not an address (name or a.b.c.d, then :port)", text ? text : "");
		return -1;
	}
	if (!kp_net_ipv4_parse(name, &ip)) return 0;
	*out = kp_net_addr(ip, *port);
	return 1;
}

#ifdef __EMSCRIPTEN__

/* The web has no UDP, and no network play (kp_netplay.h). */
KpNetSock *kp_net_sock_open(const KpNetSockConfig *c, char *error, size_t size)
{
	(void)c;
	if (size) snprintf(error, size, "no UDP sockets on the web");
	return NULL;
}
int kp_net_sock_resolve(const char *text, KpNetAddr *out, char *error, size_t size)
{
	char name[KP_NET_HOST];
	uint16_t port;
	int r = parse_host(text, out, name, &port, error, size);
	if (r == 0 && size) snprintf(error, size, "no resolver on the web");
	return r > 0;
}
void kp_net_sock_close(KpNetSock *s) { (void)s; }
KpNetIo kp_net_sock_io(KpNetSock *s)
{
	KpNetIo io = {NULL, NULL, s};
	return io;
}
int kp_net_sock_wait(KpNetSock *s, uint32_t ms, KpNetSockDeliver fn, void *ud)
{
	(void)s;
	(void)ms;
	(void)fn;
	(void)ud;
	return -1;
}
uint16_t kp_net_sock_port(const KpNetSock *s, int socket)
{
	(void)s;
	(void)socket;
	return 0;
}
long kp_net_sock_send_errors(const KpNetSock *s)
{
	(void)s;
	return 0;
}

#else

struct KpNetSock {
	Fd fd[KP_NET_SOCKETS];
	uint16_t port[KP_NET_SOCKETS]; /* the real ports, host order */
	uint32_t self_ip;               /* with a map: this program's virtual address */
	uint16_t map_base;
	int map_hosts, map_self;
	long send_errors;
	uint8_t buf[0x10000];
};

#ifdef _WIN32
static int wsa_users;
#endif

static int last_error(void)
{
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

static int would_block(int e)
{
#ifdef _WIN32
	return e == WSAEWOULDBLOCK;
#else
	return e == EAGAIN || e == EWOULDBLOCK;
#endif
}

/* An arrival error that ends nothing: an ICMP "port unreachable" reported for an earlier send (Windows reports it on
 * an unconnected socket unless told not to), a datagram larger than the buffer, an interrupted call. */
static int transient(int e)
{
#ifdef _WIN32
	return e == WSAECONNRESET || e == WSAEMSGSIZE || e == WSAENETRESET;
#else
	return e == EINTR || e == ECONNREFUSED;
#endif
}

static struct sockaddr_in real_address(uint32_t ip, uint16_t port)
{
	struct sockaddr_in a;
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons(port);
	a.sin_addr.s_addr = htonl(ip);
	return a;
}

static int open_socket(KpNetSock *s, int k, uint32_t ip, uint16_t port, char *error, size_t size)
{
	struct sockaddr_in a = real_address(ip, port);
	int on = 1;
	Fd fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd == NO_FD) {
		snprintf(error, size, "socket: error %d", last_error());
		return 0;
	}
	s->fd[k] = fd;
	if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char *)&on, sizeof on) != 0) {
		snprintf(error, size, "SO_BROADCAST: error %d", last_error());
		return 0;
	}
	if (bind(fd, (const struct sockaddr *)&a, sizeof a) != 0) {
		snprintf(error, size, "bind %s port %u: error %d", ip == LOOPBACK_IP ? "127.0.0.1" : "0.0.0.0", port,
			last_error());
		return 0;
	}
#ifdef _WIN32
	{
		u_long nonblocking = 1;
		BOOL report = FALSE;
		DWORD out = 0;
		if (ioctlsocket(fd, FIONBIO, &nonblocking) != 0) {
			snprintf(error, size, "FIONBIO: error %d", last_error());
			return 0;
		}
		/* without it an ICMP "port unreachable" fails the next recvfrom (a peer that left); failing to set it only
		 * makes those failures transient ones */
		WSAIoctl(fd, SIO_UDP_CONNRESET, &report, sizeof report, NULL, 0, &out, NULL, NULL);
	}
#else
	{
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
			snprintf(error, size, "O_NONBLOCK: error %d", last_error());
			return 0;
		}
	}
#endif
	s->port[k] = port;
	return 1;
}

void kp_net_sock_close(KpNetSock *s)
{
	int k;
	if (!s) return;
	for (k = 0; k < KP_NET_SOCKETS; k++)
		if (s->fd[k] != NO_FD) close_fd(s->fd[k]);
	free(s);
#ifdef _WIN32
	if (--wsa_users == 0) WSACleanup();
#endif
}

KpNetSock *kp_net_sock_open(const KpNetSockConfig *c, char *error, size_t size)
{
	KpNetSock *s;
	uint32_t bind_ip = c->map_base ? LOOPBACK_IP : 0; /* a map is 127.0.0.1's ports, else every interface */
	uint32_t port = c->port;
	char scratch[1];
	int k;
	if (!error || !size) {
		error = scratch;
		size = sizeof scratch;
	}
	if (c->map_base && (c->map_hosts < 1 || c->map_self >= c->map_hosts ||
		(uint32_t)c->map_base + 2u * (uint32_t)c->map_hosts > 0x10000u)) {
		snprintf(error, size, "a map of %d hosts from port %u does not hold host %d", c->map_hosts, c->map_base,
			c->map_self);
		return NULL;
	}
	if (!port) port = c->map_base && c->map_self >= 0 ? c->map_base + 2u * (uint32_t)c->map_self : KP_NET_LIST_PORT;
	if (port > 0xfffe) {
		snprintf(error, size, "port %u has no port above it", port);
		return NULL;
	}
	if (!(s = (KpNetSock *)calloc(1, sizeof *s))) {
		snprintf(error, size, "out of memory");
		return NULL;
	}
	for (k = 0; k < KP_NET_SOCKETS; k++) s->fd[k] = NO_FD;
#ifdef _WIN32
	if (wsa_users++ == 0) {
		WSADATA wsa;
		int e = WSAStartup(MAKEWORD(2, 2), &wsa);
		if (e != 0) {
			snprintf(error, size, "WSAStartup: error %d", e);
			wsa_users--;
			free(s);
			return NULL;
		}
	}
#endif
	s->map_base = c->map_base;
	s->map_hosts = c->map_base ? c->map_hosts : 0;
	s->map_self = c->map_base ? c->map_self : -1;
	s->self_ip = s->map_self >= 0 ? KP_NET_MAP_IP + (uint32_t)s->map_self : UNMAPPED_SELF;
	for (k = 0; k < KP_NET_SOCKETS; k++)
		if (!open_socket(s, k, bind_ip, (uint16_t)(port + (uint32_t)k), error, size)) {
			kp_net_sock_close(s);
			return NULL;
		}
	return s;
}

uint16_t kp_net_sock_port(const KpNetSock *s, int socket) { return s->port[socket]; }

long kp_net_sock_send_errors(const KpNetSock *s) { return s->send_errors; }

/* With a map: the map's host at a virtual address, -1 for none. */
static int map_host(const KpNetSock *s, uint32_t ip)
{
	return ip - KP_NET_MAP_IP < (uint32_t)s->map_hosts ? (int)(ip - KP_NET_MAP_IP) : -1;
}

static void send_real(KpNetSock *s, int socket, uint32_t ip, uint16_t port, const uint8_t *data, size_t size)
{
	struct sockaddr_in a = real_address(ip, port);
	if (sendto(s->fd[socket], (const char *)data, (int)size, 0, (const struct sockaddr *)&a, sizeof a) < 0)
		s->send_errors++;
}

static void io_send(void *ud, int socket, const KpNetAddr *to, const uint8_t *data, size_t size)
{
	KpNetSock *s = (KpNetSock *)ud;
	uint32_t ip = kp_net_addr_ip(to);
	uint16_t port = kp_net_addr_port(to);
	int j = port == KP_NET_LIST_PORT ? 0 : port == KP_NET_GAME_PORT ? 1 : -1, k;
	if (!s->map_base) {
		send_real(s, socket, ip, port, data, size);
		return;
	}
	if (ip == KP_NET_BROADCAST) {
		/* every host of the map, and this program when the map does not hold it (a broadcast comes back to its
		 * sender on Windows) */
		if (j < 0) return;
		for (k = 0; k < s->map_hosts; k++)
			send_real(s, socket, LOOPBACK_IP, (uint16_t)(s->map_base + 2 * k + j), data, size);
		if (s->map_self < 0) send_real(s, socket, LOOPBACK_IP, s->port[j], data, size);
	} else if (ip == s->self_ip && j >= 0)
		send_real(s, socket, LOOPBACK_IP, s->port[j], data, size);
	else if ((k = map_host(s, ip)) >= 0 && j >= 0)
		send_real(s, socket, LOOPBACK_IP, (uint16_t)(s->map_base + 2 * k + j), data, size);
	else
		send_real(s, socket, ip, port, data, size);
}

static uint32_t io_now(void *ud)
{
	(void)ud;
	return kp_net_sock_now();
}

KpNetIo kp_net_sock_io(KpNetSock *s)
{
	KpNetIo io;
	io.send = io_send;
	io.now = io_now;
	io.ud = s;
	return io;
}

/* A real source as the provider sees it: with a map, its virtual address. */
static KpNetAddr source(const KpNetSock *s, const struct sockaddr_in *a)
{
	uint32_t ip = ntohl(a->sin_addr.s_addr);
	uint16_t port = ntohs(a->sin_port);
	if (s->map_base && ip == LOOPBACK_IP) {
		int k;
		for (k = 0; k < KP_NET_SOCKETS; k++)
			if (port == s->port[k]) return kp_net_addr(s->self_ip, (uint16_t)(KP_NET_LIST_PORT + k));
		if (port >= s->map_base && port - s->map_base < 2 * s->map_hosts) {
			k = (port - s->map_base) / 2;
			return kp_net_addr(KP_NET_MAP_IP + (uint32_t)k, (uint16_t)(KP_NET_LIST_PORT + (port - s->map_base) % 2));
		}
	}
	return kp_net_addr(ip, port);
}

/* Every datagram waiting on one socket; -1 on an error that is not transient. */
static int drain(KpNetSock *s, int socket, KpNetSockDeliver fn, void *ud)
{
	int n = 0;
	for (;;) {
		struct sockaddr_in a;
		socklen_t len = sizeof a;
		KpNetAddr from;
		int e;
		long got = (long)recvfrom(s->fd[socket], (char *)s->buf, (int)sizeof s->buf, 0, (struct sockaddr *)&a, &len);
		if (got < 0) {
			e = last_error();
			if (would_block(e)) return n;
			if (transient(e)) continue;
			return -1;
		}
		if (len < (socklen_t)sizeof a || a.sin_family != AF_INET) continue;
		from = source(s, &a);
		fn(ud, socket, &from, s->buf, (size_t)got);
		n++;
	}
}

int kp_net_sock_wait(KpNetSock *s, uint32_t ms, KpNetSockDeliver fn, void *ud)
{
	int k, n = 0, ready;
#ifdef _WIN32
	fd_set set;
	struct timeval t;
	FD_ZERO(&set);
	for (k = 0; k < KP_NET_SOCKETS; k++) FD_SET(s->fd[k], &set);
	t.tv_sec = (long)(ms / 1000);
	t.tv_usec = (long)(ms % 1000) * 1000;
	ready = select(0, &set, NULL, NULL, &t);
	if (ready == SOCKET_ERROR) return -1;
#else
	struct pollfd p[KP_NET_SOCKETS];
	for (k = 0; k < KP_NET_SOCKETS; k++) {
		p[k].fd = s->fd[k];
		p[k].events = POLLIN;
		p[k].revents = 0;
	}
	ready = poll(p, KP_NET_SOCKETS, ms > 0x7fffffffu ? -1 : (int)ms);
	if (ready < 0) return errno == EINTR ? 0 : -1;
#endif
	if (ready == 0) return 0;
	for (k = 0; k < KP_NET_SOCKETS; k++) {
		int got = drain(s, k, fn, ud);
		if (got < 0) return -1;
		n += got;
	}
	return n;
}

int kp_net_sock_resolve(const char *text, KpNetAddr *out, char *error, size_t size)
{
	char name[KP_NET_HOST], scratch[1];
	uint16_t port;
	struct addrinfo hints, *list = NULL, *a;
	int r, found = 0;
	if (!error || !size) {
		error = scratch;
		size = sizeof scratch;
	}
	if ((r = parse_host(text, out, name, &port, error, size)) != 0) return r > 0;
#ifdef _WIN32
	{
		WSADATA wsa; /* getaddrinfo wants Winsock started, sockets open or not */
		if ((r = WSAStartup(MAKEWORD(2, 2), &wsa)) != 0) {
			snprintf(error, size, "WSAStartup: error %d", r);
			return 0;
		}
	}
#endif
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	r = getaddrinfo(name, NULL, &hints, &list);
	for (a = r == 0 ? list : NULL; a && !found; a = a->ai_next) {
		if (a->ai_family == AF_INET && a->ai_addrlen >= (socklen_t)sizeof(struct sockaddr_in)) {
			*out = kp_net_addr(ntohl(((const struct sockaddr_in *)(const void *)a->ai_addr)->sin_addr.s_addr), port);
			found = 1;
		}
	}
	if (list) freeaddrinfo(list);
#ifdef _WIN32
	WSACleanup();
#endif
	if (!found) snprintf(error, size, "%s has no IPv4 address (resolver: %d)", name, r);
	return found;
}

#endif
