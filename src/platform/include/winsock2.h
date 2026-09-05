/* winsock2.h -- the Winsock surface src/inject/control.c uses, over BSD
 * sockets. A SOCKET is a file descriptor; the names differ and the
 * SO_RCVTIMEO argument differs (Winsock takes milliseconds in a DWORD,
 * POSIX a timeval), and setsockopt below absorbs that. */
#ifndef AM2_PLATFORM_WINSOCK2_H
#define AM2_PLATFORM_WINSOCK2_H

#include <windows.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

AM2_EXTERN_C_BEGIN

typedef int32_t SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)

typedef struct WSAData {
    WORD    wVersion;
    WORD    wHighVersion;
    char    szDescription[257];
    char    szSystemStatus[129];
    uint16_t iMaxSockets;
    uint16_t iMaxUdpDg;
    char   *lpVendorInfo;
} WSADATA, *LPWSADATA;

int32_t WINAPI WSAStartup(WORD version, LPWSADATA out);
int32_t WINAPI WSACleanup(void);
int32_t WINAPI WSAGetLastError(void);
int32_t WINAPI closesocket(SOCKET s);
int32_t am2_ws_setsockopt(SOCKET s, int32_t level, int32_t name,
                          const char *val, int32_t len);
#define setsockopt am2_ws_setsockopt

AM2_EXTERN_C_END

#endif /* AM2_PLATFORM_WINSOCK2_H */
