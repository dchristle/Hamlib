/* Keep command fixtures on the host while exercising multicast options. */
#define rigctld_stream_multicast_socket_create rigctld_stream_multicast_socket_create_native
#include "../tests/rigctld_stream.c"
#undef rigctld_stream_multicast_socket_create

int rigctld_stream_multicast_socket_create(
    const struct sockaddr_storage *mcast_addr,
    socklen_t addrlen,
    int ttl,
    int *sock_fd,
    int *port)
{
    if (!mcast_addr || mcast_addr->ss_family != AF_INET)
    {
        return -1;
    }

    int ret = rigctld_stream_multicast_socket_create_native(
        mcast_addr, addrlen, ttl, sock_fd, port);

    if (ret != 0)
    {
        return ret;
    }

    struct in_addr loopback;
    loopback.s_addr = htonl(INADDR_LOOPBACK);
    unsigned char local_ttl = 0;

    if (setsockopt(*sock_fd, IPPROTO_IP, IP_MULTICAST_IF,
                   (const char *)&loopback, sizeof(loopback)) != 0
        || setsockopt(*sock_fd, IPPROTO_IP, IP_MULTICAST_TTL,
                       (const char *)&local_ttl, sizeof(local_ttl)) != 0)
    {
        socket_close(*sock_fd);
        *sock_fd = -1;
        return -1;
    }

    return 0;
}
