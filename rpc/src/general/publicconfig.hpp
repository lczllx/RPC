#pragma once
#include <chrono>
#include<cstdint>
#include<string>
#include<utility>
namespace lcz_rpc
{
    typedef std::pair<std::string,int32_t> HostInfo;//主机信息

    // 心跳配置结构体：检查频率、空闲超时、心跳间隔
    struct HeartbeatConfig {
        double check_interval_sec = 5.0;    // 检查频率：每5秒扫描一次
        int idle_timeout_sec = 15;          // 空闲超时：15秒没收到心跳则视为离线
        int heartbeat_interval_sec = 10;    // 心跳间隔：提供者每10秒发一次心跳
    };
    // 主机详情结构体：主机地址 + 负载值
    struct HostDetail {
        HostInfo host;
        int load = 0;
        HostDetail(const HostInfo &host,int load) : host(host),load(load) {}
        HostDetail() : host(HostInfo()),load(0) {}
    };
}