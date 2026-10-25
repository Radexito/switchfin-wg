/* SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 xlanor
 * This file is part of wg-nx. See COPYING and COPYING.LESSER.
 */

#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <queue>
#include <array>
#include <functional>
#include <cstdint>

extern "C" {
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/netif.h"
#include "wireguard.h"
#include "wg_netif.h"
#include <poll.h>
}

namespace wgnx {

enum class LogLevel {
    Debug,
    Info,
    Error
};

struct LwipRelayConfig {
    std::function<void(LogLevel level, const char* message)> log_callback;
    std::function<void()> thread_affinity_callback;
    bool debug_logging = false;
};

class LwipRelay {
public:
    explicit LwipRelay(WgTunnel* tunnel, const LwipRelayConfig& config = {});
    ~LwipRelay();

    LwipRelay(const LwipRelay&) = delete;
    LwipRelay& operator=(const LwipRelay&) = delete;

    bool start(const std::string& tunnelIp, const std::string& targetIp);
    void stop();

    uint16_t startTcpRelay(uint16_t targetPort, uint16_t localPort);
    uint16_t startUdpRelay(uint16_t targetPort, uint16_t localPort);

    /* Zero-copy ingress entry. Takes ownership of `slot`; the LwipRelay will
     * release it via the tunnel's pool once lwIP is done with the pbuf.
     * Returns non-zero if ownership was taken, 0 if dropped (caller releases). */
    int handleIncomingPacket(WgRecvSlot* slot, const void* data, size_t len);
    void tick();

    static int onTunnelRecv(void* user, WgRecvSlot* slot, const void* data, size_t len);

    bool isRunning() const { return running_; }

private:
    struct TcpConnection {
        int localSock;
        struct tcp_pcb* pcb;
        uint16_t localPort;
        uint16_t targetPort;
        std::vector<uint8_t> pendingData;
        bool connected;
    };

    struct UdpBinding {
        int localSock;
        struct udp_pcb* pcb;
        uint16_t localPort;
        uint16_t targetPort;
        struct sockaddr_in clientAddr;
        bool hasClient;
    };

    void log(LogLevel level, const char* fmt, ...);
    void flushDebugTraffic();

    void runLoop();
    void pollTcpListeners();
    void pollUdpSockets();
    void pollTcpConnections();
    void signalWake();

    // Closes the wake pair and sets wakeDead_. Without it the loop wakes up
    // every 10 ms on its own, so the relay keeps running.
    void dropWakeSocket();

    // Drops connections and listeners whose descriptors no longer exist.
    void pruneDeadSockets();
    short pollRevents(int fd) const;

    int createTcpListener(uint16_t port);
    int createUdpSocket(uint16_t port);

    static err_t onTcpConnected(void* arg, struct tcp_pcb* pcb, err_t err);
    static err_t onTcpRecv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err);
    static err_t onTcpSent(void* arg, struct tcp_pcb* pcb, u16_t len);
    static void onTcpError(void* arg, err_t err);

    static void onUdpRecv(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                          const ip_addr_t* addr, u16_t port);

    void processIncomingQueue();

    WgTunnel* tunnel_;
    LwipRelayConfig config_;

    struct netif netif_;
    ip4_addr_t tunnelAddr_;
    ip4_addr_t targetAddr_;

    std::atomic<bool> running_;
    std::thread loopThread_;
    std::mutex mutex_;

    std::map<uint16_t, int> tcpListeners_;
    // Target port on the LAN for every listen port. Without this the relay
    // dials the listen port inside the tunnel instead of the service (8096).
    std::map<uint16_t, uint16_t> tcpTargetPorts_;
    std::map<int, std::shared_ptr<TcpConnection>> tcpConnections_;
    std::map<uint16_t, std::shared_ptr<UdpBinding>> udpBindings_;

    struct IncomingSlot {
        WgRecvSlot* slot;
        const uint8_t* data;
        size_t len;
    };
    std::queue<IncomingSlot> incomingQueue_;
    std::mutex queueMutex_;

    static constexpr size_t kSlotHolderCount = 128; /* > WG pool size to absorb lwIP hold time */
    std::array<WgSlotPbuf, kSlotHolderCount> slotHolders_;

    int wakeFd_[2] = {-1, -1};
    // On Horizon, poll can report POLLHUP on a working UDP socket. When that
    // happens the wake socket is dropped and the loop switches to a short
    // timeout.
    bool wakeDead_ = false;
    std::vector<pollfd> pollFds_;

    uint64_t debugTcpPackets_ = 0;
    uint64_t debugTcpBytes_ = 0;
    uint64_t debugUdpPackets_ = 0;
    uint64_t debugUdpBytes_ = 0;

    bool initialized_;
};

}
