﻿/*
** repository: https://github.com/trumanzhao/luna
** trumanzhao, 2017-02-11, trumanzhao@foxmail.com
*/

#pragma once
#include <memory>
#include <array>
#include <vector>
#include "socket_mgr.h"

enum class msg_id : char
{
    remote_call,
    forward_target,
    forward_master,
    forward_random,
    forward_broadcast,
    forward_hash,
};

const int MAX_SERVICE_GROUP = (UCHAR_MAX + 1);

struct service_node
{
    uint32_t id = 0;
    uint32_t token = 0;
};

struct service_group
{
    uint32_t master = 0;
    std::vector<service_node> nodes;
};

class socket_router
{
public:
    socket_router(std::shared_ptr<socket_mgr>& mgr) : m_mgr(mgr){ }

    void set_master(uint32_t group_idx, uint32_t token);
    void map_token(uint32_t service_id, uint32_t token);
    void erase(uint32_t service_id);
    void do_forward_target(char* data, size_t data_len);
    void do_forward_master(char* data, size_t data_len);
    void do_forward_random(char* data, size_t data_len);
    void do_forward_broadcast(char* data, size_t data_len);
    void do_forward_hash(char* data, size_t data_len);

private:
    std::shared_ptr<socket_mgr> m_mgr;
    std::array<service_group, MAX_SERVICE_GROUP> m_groups;
};

