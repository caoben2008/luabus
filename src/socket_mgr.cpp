﻿/*
** repository: https://github.com/trumanzhao/luna
** trumanzhao, 2017-04-23, trumanzhao@foxmail.com
*/

#ifdef _MSC_VER
#include <Winsock2.h>
#include <Ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
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
#include "tools.h"
#include "socket_helper.h"
#include "socket_mgr.h"
#include "socket_mgr_impl.h"

socket_mgr::socket_mgr() { }
socket_mgr::socket_mgr(socket_mgr& other) : m_impl(other.m_impl) {}
socket_mgr::socket_mgr(socket_mgr&& other) : m_impl(other.m_impl) {}
socket_mgr::~socket_mgr() { }

bool socket_mgr::setup(int max_connection) {
    m_impl = std::make_shared<socket_mgr_impl>();
    return m_impl->setup(max_connection);
}

int socket_mgr::wait(int timeout){ return m_impl->wait(timeout); }
int socket_mgr::listen(std::string& err, const char ip[], int port){ return m_impl->listen(err, ip, port); }
int socket_mgr::connect(std::string& err, const char node_name[], const char service_name[], int timeout){ return m_impl->connect(err, node_name, service_name, timeout); }
void socket_mgr::set_send_buffer_size(uint32_t token, size_t size){ m_impl->set_send_buffer_size(token, size); }
void socket_mgr::set_recv_buffer_size(uint32_t token, size_t size){ m_impl->set_recv_buffer_size(token, size); }
void socket_mgr::set_nodelay(uint32_t token, int flag){ m_impl->set_nodelay(token, flag); }
void socket_mgr::send(uint32_t token, const void* data, size_t data_len){ m_impl->send(token, data, data_len); }
void socket_mgr::close(uint32_t token){ m_impl->close(token); }
bool socket_mgr::get_remote_ip(uint32_t token, std::string& ip){ return m_impl->get_remote_ip(token, ip); }
void socket_mgr::set_accept_callback(uint32_t token, const std::function<void(uint32_t)>& cb){ m_impl->set_accept_callback(token, cb); }
void socket_mgr::set_connect_callback(uint32_t token, const std::function<void(bool, const char*)>& cb){ m_impl->set_connect_callback(token, cb); }
void socket_mgr::set_package_callback(uint32_t token, const std::function<void(char*, size_t)>& cb){ m_impl->set_package_callback(token, cb); }
void socket_mgr::set_error_callback(uint32_t token, const std::function<void(const char*)>& cb){ m_impl->set_error_callback(token, cb); }

