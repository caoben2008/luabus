﻿/*
** repository: https://github.com/trumanzhao/luna
** trumanzhao, 2016-11-01, trumanzhao@foxmail.com
*/

#ifdef _MSC_VER
#include <Winsock2.h>
#include <Ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <mstcpip.h>
#endif
#ifdef __linux
#include <sys/epoll.h>
#endif
#ifdef __APPLE__
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#endif
#if defined(__linux) || defined(__APPLE__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <algorithm>
#include <assert.h>
#include "tools.h"
#include "var_int.h"
#include "socket_mgr_impl.h"
#include "socket_stream.h"

#ifdef __linux
static const int s_send_flag = MSG_NOSIGNAL;
#endif

#if defined(_MSC_VER) || defined(__APPLE__)
static const int s_send_flag = 0;
#endif

#ifdef _MSC_VER
socket_stream::socket_stream(socket_mgr_impl* mgr, LPFN_CONNECTEX connect_func)
{
    mgr->increase_count();
    m_mgr = mgr;
    m_connect_func = connect_func;
    m_ip[0] = 0;
}
#endif

socket_stream::socket_stream(socket_mgr_impl* mgr)
{
    mgr->increase_count();
    m_mgr = mgr;
    m_ip[0] = 0;
}

socket_stream::~socket_stream()
{
    if (m_socket != INVALID_SOCKET)
    {
        close_socket_handle(m_socket);
        m_socket = INVALID_SOCKET;
    }

    if (m_addr != nullptr)
    {
        freeaddrinfo(m_addr);
        m_addr = nullptr;
    }
    m_mgr->decrease_count();
}

bool socket_stream::get_remote_ip(std::string& ip)
{
    ip = m_ip;
    return true;
}

bool socket_stream::accept_socket(socket_t fd, const char ip[])
{
#ifdef _MSC_VER
    if (!wsa_recv_empty(fd, m_recv_ovl))
        return false;
    m_ovl_ref++;
#endif

    safe_cpy(m_ip, ip);
    m_socket = fd;
    m_connected = true;
    m_last_recv_time = get_time_ms();
    return true;
}

void socket_stream::connect(const char node_name[], const char service_name[], int timeout)
{
    m_node_name = node_name;
    m_service_name = service_name;
    m_connecting_time = get_time_ms() + timeout;
}

bool socket_stream::update(int64_t now)
{
    if (m_closed)
    {
        if (m_socket != INVALID_SOCKET)
        {
            close_socket_handle(m_socket);
            m_socket = INVALID_SOCKET;
        }

#ifdef _MSC_VER
        return m_ovl_ref != 0;
#endif

#if defined(__linux) || defined(__APPLE__)
        return false;
#endif
    }

    if (!m_connected)
    {
        if (now > m_connecting_time)
        {
            on_connect(false, "timeout");
            return true;
        }

        try_connect();
        return true;
    }

    if (m_timeout > 0 && now - m_last_recv_time > m_timeout)
    {
        on_error("timeout");
    }

    return true;
}

#ifdef _MSC_VER
static bool bind_any(socket_t s)
{
    struct sockaddr_in6 v6addr;

    memset(&v6addr, 0, sizeof(v6addr));
    v6addr.sin6_family = AF_INET6;
    v6addr.sin6_addr = in6addr_any;
    v6addr.sin6_port = 0;
    auto ret = bind(s, (sockaddr*)&v6addr, (int)sizeof(v6addr));
    if (ret != SOCKET_ERROR)
        return true;

    struct sockaddr_in v4addr;
    memset(&v4addr, 0, sizeof(v4addr));
    v4addr.sin_family = AF_INET;
    v4addr.sin_addr.s_addr = INADDR_ANY;
    v4addr.sin_port = 0;

    ret = bind(s, (sockaddr*)&v4addr, (int)sizeof(v4addr));
    return ret != SOCKET_ERROR;
}

bool socket_stream::do_connect()
{
    if (!bind_any(m_socket))
    {
        on_connect(false, "bind-failed");
        return false;
    }

    if (!m_mgr->watch_connecting(m_socket, this))
    {
        on_connect(false, "watch-failed");
        return false;
    }

    memset(&m_send_ovl, 0, sizeof(m_send_ovl));

    auto ret = (*m_connect_func)(m_socket, (SOCKADDR*)m_next->ai_addr, (int)m_next->ai_addrlen, nullptr, 0, nullptr, &m_send_ovl);
    if (!ret)
    {
        m_next = m_next->ai_next;
        int err = get_socket_error();
        if (err == ERROR_IO_PENDING)
        {
            m_ovl_ref++;
            return true;
        }

        m_closed = true;
        on_connect(false, "connect-failed");
        return false;
    }

    if (!wsa_recv_empty(m_socket, m_recv_ovl))
    {
        on_connect(false, "connect-failed");
        return false;
    }

    m_ovl_ref++;
    on_connect(true, "ok");
    return true;
}
#endif

#if defined(__linux) || defined(__APPLE__)
bool socket_stream::do_connect()
{
    while (true)
    {
        auto ret = ::connect(m_socket, m_next->ai_addr, (int)m_next->ai_addrlen);
        if (ret != SOCKET_ERROR)
        {
            on_connect(true, "ok");
            break;
        }

        int err = get_socket_error();
        if (err == EINTR)
            continue;

        m_next = m_next->ai_next;

        if (err != EINPROGRESS)
            return false;

        if (!m_mgr->watch_connecting(m_socket, this))
        {
            on_connect(false, "watch-failed");
            return false;
        }
        break;
    }
    return true;
}
#endif

void socket_stream::try_connect()
{
    if (m_addr == nullptr)
    {
        addrinfo hints;
        struct addrinfo* addr = nullptr;

        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC; // use AF_INET6 to force IPv6
        hints.ai_socktype = SOCK_STREAM;

        int ret = getaddrinfo(m_node_name.c_str(), m_service_name.c_str(), &hints, &addr);
        if (ret != 0 || addr == nullptr)
        {
            on_connect(false, "addr-error");
            return;
        }

        m_addr = addr;
        m_next = addr;
    }

    // socket connecting
    if (m_socket != INVALID_SOCKET)
        return;

    while (m_next != nullptr && !m_closed)
    {
        if (m_next->ai_family != AF_INET && m_next->ai_family != AF_INET6)
        {
            m_next = m_next->ai_next;
            continue;
        }

        m_socket = socket(m_next->ai_family, m_next->ai_socktype, m_next->ai_protocol);
        if (m_socket == INVALID_SOCKET)
        {
            m_next = m_next->ai_next;
            continue;
        }

#ifdef __APPLE__
        int opt = 1;
        setsockopt(m_socket, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif        

        set_no_block(m_socket);
        set_close_on_exec(m_socket);
        set_no_delay(m_socket, 1);
        get_ip_string(m_ip, sizeof(m_ip), m_next->ai_addr, m_next->ai_addrlen);

        if (do_connect())
            return;

        if (m_socket != INVALID_SOCKET)
        {
            close_socket_handle(m_socket);
            m_socket = INVALID_SOCKET;
        }
    }

    on_connect(false, "connect-failed");
}

void socket_stream::send(const void* data, size_t data_len)
{
    if (m_closed)
        return;

    BYTE  header[MAX_VARINT_SIZE];
    size_t header_len = encode_u64(header, sizeof(header), data_len);
    stream_send((char*)header, header_len);
    stream_send((char*)data, data_len);
}

void socket_stream::sendv(const sendv_item items[], int count)
{
    if (m_closed)
        return;

    size_t data_len = 0;
    for (int i = 0; i < count; i++)
    {
        data_len += items[i].len;
    }

    BYTE  header[MAX_VARINT_SIZE];
    size_t header_len = encode_u64(header, sizeof(header), data_len);
    stream_send((char*)header, header_len);

    for (int i = 0; i < count; i++)
    {
        auto item = items[i];
        stream_send((char*)item.data, item.len);
    }
}

void socket_stream::stream_send(const char* data, size_t data_len)
{
    if (m_closed)
        return;

    if (!m_send_buffer->empty())
    {
        if (!m_send_buffer->push_data(data, data_len))
        {
            on_error("send-buffer-full");
        }
        return;
    }

    while (data_len > 0)
    {
        int send_len = ::send(m_socket, data, (int)data_len, s_send_flag);
        if (send_len == SOCKET_ERROR)
        {
            int err = get_socket_error();

#ifdef _MSC_VER
            if (err == WSAEWOULDBLOCK)
            {
                if (!m_send_buffer->push_data(data, data_len))
                {
                    on_error("send-buffer-full");
                    return;
                }

                if (!wsa_send_empty(m_socket, m_send_ovl))
                {
                    on_error("send-failed");
                    return;
                }
                m_ovl_ref++;
                return;
            }
#endif

#if defined(__linux) || defined(__APPLE__)
            if (err == EINTR)
                continue;

            if (err == EAGAIN)
            {
                if (!m_send_buffer->push_data(data, data_len))
                {
                    on_error("send-buffer-full");
                    return;
                }

                if (!m_mgr->watch_send(m_socket, this, true))
                {
                    on_error("watch-error");
                    return;
                }

                return;
            }
#endif

            on_error("send-failed");
            return;
        }

        if (send_len == 0)
        {
            on_error("connection-lost");
            return;
        }

        data += send_len;
        data_len -= send_len;
    }
}

#ifdef _MSC_VER
void socket_stream::on_complete(WSAOVERLAPPED* ovl)
{
    m_ovl_ref--;
    if (m_closed)
        return;

    if (m_connected)
    {
        if (ovl == &m_recv_ovl)
        {
            do_recv(UINT_MAX, false);
        }
        else
        {
            do_send(UINT_MAX, false);
        }
        return;
    }

    int seconds = 0;
    socklen_t sock_len = (socklen_t)sizeof(seconds);
    auto ret = getsockopt(m_socket, SOL_SOCKET, SO_CONNECT_TIME, (char*)&seconds, &sock_len);
    if (ret == 0 && seconds != 0xffffffff)
    {
        if (!wsa_recv_empty(m_socket, m_recv_ovl))
        {
            on_connect(false, "connect-failed");
            return;
        }

        m_ovl_ref++;
        on_connect(true, "ok");
        return;
    }

    // socket连接失败,还可以继续dns解析的下一个地址继续尝试
    close_socket_handle(m_socket);
    m_socket = INVALID_SOCKET;
    if (m_next == nullptr)
    {
        on_connect(false, "connect-failed");
    }
}
#endif

#if defined(__linux) || defined(__APPLE__)
void socket_stream::on_can_send(size_t max_len, bool is_eof)
{
    if (m_closed)
        return;

    if (m_connected)
    {
        do_send(max_len, is_eof);
        return;
    }

    int err = 0;
    socklen_t sock_len = sizeof(err);
    auto ret = getsockopt(m_socket, SOL_SOCKET, SO_ERROR, (char*)&err, &sock_len);
    if (ret == 0 && err == 0 && !is_eof)
    {
        if (!m_mgr->watch_connected(m_socket, this))
        {
            on_connect(false, "watch-error");
            return;
        }

        on_connect(true, "ok");
        return;
    }

    // socket连接失败,还可以继续dns解析的下一个地址继续尝试
    close_socket_handle(m_socket);
    m_socket = INVALID_SOCKET;
    if (m_next == nullptr)
    {
        on_connect(false, "connect-failed");
    }
}
#endif


void socket_stream::do_send(size_t max_len, bool is_eof)
{
    size_t total_send = 0;
    while (total_send < max_len && !m_closed)
    {
        size_t data_len = 0;
        auto* data = m_send_buffer->peek_data(&data_len);
        if (data_len == 0)
        {
            if (!m_mgr->watch_send(m_socket, this, false))
            {
                on_error("watch-error");
                return;
            }
            break;
        }

        size_t try_len = std::min<size_t>(data_len, max_len - total_send);
        int send_len = ::send(m_socket, (char*)data, (int)try_len, s_send_flag);
        if (send_len == SOCKET_ERROR)
        {
            int err = get_socket_error();

#ifdef _MSC_VER
            if (err == WSAEWOULDBLOCK)
            {
                if (!wsa_send_empty(m_socket, m_send_ovl))
                {
                    on_error("send-failed");
                    return;
                }
                m_ovl_ref++;
                break;
            }
#endif

#if defined(__linux) || defined(__APPLE__)
            if (err == EINTR)
                continue;

            if (err == EAGAIN)
                break;
#endif

            on_error("send-failed");
            return;
        }

        if (send_len == 0)
        {
            on_error("connection-lost");
            return;
        }

        total_send += send_len;
        m_send_buffer->pop_data((size_t)send_len);
    }

    m_send_buffer->compact(true);

    if (is_eof || max_len == 0)
    {
        on_error("connection-lost");
    }
}

void socket_stream::do_recv(size_t max_len, bool is_eof)
{
    size_t total_recv = 0;
    while (total_recv < max_len && !m_closed)
    {
        size_t space_len = 0;
        auto* space = m_recv_buffer->peek_space(&space_len);
        if (space_len == 0)
        {
            on_error("package-too-large");
            return;
        }

        size_t try_len = std::min<size_t>(space_len, max_len - total_recv);
        int recv_len = recv(m_socket, (char*)space, (int)try_len, 0);
        if (recv_len < 0)
        {
            int err = get_socket_error();

#ifdef _MSC_VER
            if (err == WSAEWOULDBLOCK)
            {
                if (!wsa_recv_empty(m_socket, m_recv_ovl))
                {
                    on_error("recv-failed");
                    return;
                }
                m_ovl_ref++;
                break;
            }
#endif

#if defined(__linux) || defined(__APPLE__)
            if (err == EINTR)
                continue;

            if (err == EAGAIN)
                break;
#endif

            on_error("recv-failed");
            return;
        }

        if (recv_len == 0)
        {
            on_error("connection-lost");
            return;
        }

        total_recv += recv_len;
        m_recv_buffer->pop_space(recv_len);
        dispatch_package();
    }

    if (is_eof || max_len == 0)
    {
        on_error("connection-lost");
    }
}

void socket_stream::dispatch_package()
{
    while (!m_closed)
    {
        size_t data_len = 0;
        uint64_t package_size = 0;
        auto* data = m_recv_buffer->peek_data(&data_len);
        size_t header_len = decode_u64(&package_size, data, data_len);
        if (header_len == 0)
            break;

        if (data_len < header_len + package_size)
            break;

        m_last_recv_time = get_time_ms();
        m_package_cb((char*)data + header_len, (size_t)package_size);

        m_recv_buffer->pop_data(header_len + (size_t)package_size);
    }

    m_recv_buffer->compact();
}

void socket_stream::on_error(const char err[])
{
    if (!m_closed)
    {
        // kqueue实现下,如果eof时不及时关闭或unwatch,则会触发很多次eof
        if (m_socket != INVALID_SOCKET)
        {
            close_socket_handle(m_socket);
            m_socket = INVALID_SOCKET;
        }

        m_closed = true;
        m_error_cb(err);
    }
}

void socket_stream::on_connect(bool ok, const char reason[])
{
    m_next = nullptr;
    if (m_addr != nullptr)
    {
        freeaddrinfo(m_addr);
        m_addr = nullptr;
    }

    if (!m_closed)
    {
        if (!ok)
        {
            if (m_socket != INVALID_SOCKET)
            {
                close_socket_handle(m_socket);
                m_socket = INVALID_SOCKET;
            }
            m_closed = true;
        }
        m_connected = ok;
        m_last_recv_time = get_time_ms();
        m_connect_cb(ok, reason);
    }
}

