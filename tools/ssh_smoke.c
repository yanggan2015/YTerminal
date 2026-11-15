#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <libssh2.h>

int main(void)
{
    WSADATA wsa;
    SOCKET sock;
    struct addrinfo hints = {0}, *res = NULL;
    LIBSSH2_SESSION *session;
    LIBSSH2_CHANNEL *channel;
    char buf[1024];
    ssize_t n;
    int rc;

    WSAStartup(MAKEWORD(2,2), &wsa);
    libssh2_init(0);
    hints.ai_socktype = SOCK_STREAM;
    getaddrinfo("sshbridge.online", "52222", &hints, &res);
    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (connect(sock, res->ai_addr, (int)res->ai_addrlen)) { printf("connect fail\n"); return 1; }
    session = libssh2_session_init();
    rc = libssh2_session_handshake(session, sock);
    printf("handshake %d\n", rc);
    rc = libssh2_userauth_password(session, "demo", "123456");
    printf("auth %d\n", rc);
    if (rc) return 2;
    channel = libssh2_channel_open_session(session);
    libssh2_channel_request_pty(channel, "xterm");
    libssh2_channel_shell(channel);
    n = libssh2_channel_read(channel, buf, sizeof(buf)-1);
    if (n > 0) { buf[n] = 0; printf("out: %s\n", buf); }
    else printf("read %zd\n", n);
    libssh2_channel_close(channel);
    libssh2_channel_free(channel);
    libssh2_session_disconnect(session, "x");
    libssh2_session_free(session);
    closesocket(sock);
    printf("OK\n");
    return 0;
}
