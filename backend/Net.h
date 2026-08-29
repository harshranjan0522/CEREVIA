#ifndef CEREVIA_NET_H
#define CEREVIA_NET_H

// ---------------------------------------------------------------------------
// Cross-platform socket shim.
//
// The original build only compiled on Windows because every source file spoke
// raw Winsock. This header hides the two dialects (Winsock2 on Windows, BSD
// sockets everywhere else) behind one tiny surface so the rest of the backend
// is written once and builds on Windows, macOS and Linux.
// ---------------------------------------------------------------------------

#include <string>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")

    namespace net {
        using Socket = SOCKET;
        constexpr Socket kInvalidSocket = INVALID_SOCKET;
        inline int lastError()               { return WSAGetLastError(); }
        inline void closeSocket(Socket s)    { closesocket(s); }
        inline bool startup()                { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w) == 0; }
        inline void shutdownLib()            { WSACleanup(); }
        using SockLen = int;
        using OptVal  = const char *;
    }
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <unistd.h>
    #include <cerrno>
    #include <csignal>

    namespace net {
        using Socket = int;
        constexpr Socket kInvalidSocket = -1;
        inline int lastError()               { return errno; }
        inline void closeSocket(Socket s)    { ::close(s); }
        // Writing to a socket the browser already closed raises SIGPIPE on
        // POSIX, which kills the process. Ignoring it turns that into a plain
        // send() error we can handle.
        inline bool startup()                { ::signal(SIGPIPE, SIG_IGN); return true; }
        inline void shutdownLib()            {}
        using SockLen = socklen_t;
        using OptVal  = const void *;
    }

    #ifndef SOCKET_ERROR
        #define SOCKET_ERROR (-1)
    #endif
    #ifndef INVALID_SOCKET
        #define INVALID_SOCKET (-1)
    #endif
#endif

#endif // CEREVIA_NET_H
