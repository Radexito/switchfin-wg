#include "utils/wireguard.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <switch.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include <borealis.hpp>

extern "C" {
#include "wireguard.h"
}

#include "wg_lwip_relay.hpp"

namespace wgrelay {

namespace {

// Logs to the console, to a file on the SD card and over UDP to the PC.
// The UDP copy keeps the last step visible even when the app crashes.
void wglog(const std::string& msg) {
    // No brls::Logger: log to the file only. Calling the UI library from a
    // worker thread used to kill the process.
    static FILE* f = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        f = std::fopen("sdmc:/switch/Switchfin/wg-log.txt", "a");
        if (!f)
            f = std::fopen("/switch/Switchfin/wg-log.txt", "a");
        if (f)
            std::setvbuf(f, nullptr, _IONBF, 0);
    }
    if (f) {
        std::fprintf(f, "%s\n", msg.c_str());
        std::fflush(f);
    }
}

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    auto b = s.find_first_not_of(ws);
    if (b == std::string::npos)
        return "";
    auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool splitEndpoint(const std::string& ep, std::string& host, uint16_t& port) {
    auto pos = ep.rfind(':');
    if (pos == std::string::npos || pos == 0 || pos + 1 >= ep.size())
        return false;
    host = trim(ep.substr(0, pos));
    int p = std::atoi(ep.substr(pos + 1).c_str());
    if (p <= 0 || p > 65535)
        return false;
    port = static_cast<uint16_t>(p);
    return !host.empty();
}

// "10.10.0.2/32" -> "10.10.0.2"
std::string stripPrefix(const std::string& addr) {
    auto pos = addr.find('/');
    return pos == std::string::npos ? trim(addr) : trim(addr.substr(0, pos));
}

}  // namespace

std::string configPath() {
    const char* withSdmc = "sdmc:/switch/Switchfin/wireguard.conf";
    if (FILE* f = std::fopen(withSdmc, "r")) {
        std::fclose(f);
        return withSdmc;
    }
    return "/switch/Switchfin/wireguard.conf";
}

Settings readSettings(const std::string& path) {
    Settings s;
    std::ifstream in(path);
    if (!in.is_open())
        return s;

    std::string line, section;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        if (line.front() == '[') {
            section = lower(trim(line.substr(1, line.find(']') - 1)));
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));
        std::string lowVal = lower(val);

        if (key == "enabled") {
            s.enabled = !(val == "0" || lowVal == "false" || lowVal == "no");
        } else if (key == "privatekey" && section == "interface") {
            s.privateKey = val;
        } else if (key == "address" && section == "interface") {
            s.address = stripPrefix(val);
        } else if (key == "target" && section == "interface") {
            s.target = val;
        } else if (key == "targetport") {
            int v = std::atoi(val.c_str());
            if (v > 0 && v < 65536) s.targetPort = (unsigned)v;
        } else if (key == "localport") {
            int v = std::atoi(val.c_str());
            if (v > 0 && v < 65536) s.localPort = (unsigned)v;
        } else if (key == "mtu") {
            int v = std::atoi(val.c_str());
            if (v >= 1000 && v <= 1400) s.mtu = (unsigned)v;
        } else if (key == "publickey" && section == "peer") {
            s.publicKey = val;
        } else if (key == "endpoint" && section == "peer") {
            s.endpoint = val;
        } else if (key == "persistentkeepalive") {
            int v = std::atoi(val.c_str());
            if (v >= 0 && v < 65536) s.keepalive = (unsigned)v;
        }
    }
    return s;
}

bool writeSettings(const std::string& path, const Settings& s) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open())
        return false;

    out << "[Interface]\n";
    out << "Enabled    = " << (s.enabled ? 1 : 0) << "\n";
    out << "PrivateKey = " << s.privateKey << "\n";
    out << "Address    = " << s.address << "\n";
    out << "Target     = " << s.target << "\n";
    out << "TargetPort = " << s.targetPort << "\n";
    out << "LocalPort  = " << s.localPort << "\n";
    out << "MTU        = " << s.mtu << "\n";
    out << "\n[Peer]\n";
    out << "PublicKey  = " << s.publicKey << "\n";
    out << "Endpoint   = " << s.endpoint << "\n";
    out << "PersistentKeepalive = " << s.keepalive << "\n";
    out.flush();
    return out.good();
}

Relay& Relay::instance() {
    static Relay inst;
    return inst;
}

bool Relay::loadConfig(const std::string& configPath) {
    std::ifstream in(configPath);
    if (!in.is_open()) {
        status_ = "WireGuard: missing config file " + configPath;
        return false;
    }

    std::string line, section;
    bool havePriv = false, havePeer = false, haveEndpoint = false, haveAddress = false, haveTarget = false;

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        if (line.front() == '[') {
            section = lower(trim(line.substr(1, line.find(']') - 1)));
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));

        if (key == "privatekey" && section == "interface") {
            privateKey_ = val;
            havePriv = true;
        } else if (key == "enabled") {
            std::string lowVal = lower(val);
            enabled_ = !(val == "0" || lowVal == "false" || lowVal == "no");
        } else if (key == "address" && section == "interface") {
            address_ = stripPrefix(val);
            haveAddress = !address_.empty();
        } else if (key == "target" && section == "interface") {
            target_ = val;
            haveTarget = !target_.empty();
        } else if (key == "targetport" && section == "interface") {
            targetPort_ = static_cast<uint16_t>(std::atoi(val.c_str()));
        } else if (key == "localport" && section == "interface") {
            localPort_ = static_cast<uint16_t>(std::atoi(val.c_str()));
        } else if (key == "publickey" && section == "peer") {
            peerKey_ = val;
            havePeer = true;
        } else if (key == "endpoint" && section == "peer") {
            haveEndpoint = splitEndpoint(val, endpointHost_, endpointPort_);
        } else if (key == "mtu" && section == "interface") {
            int m = std::atoi(val.c_str());
            if (m >= 1000 && m <= 1400)
                mtu_ = static_cast<uint16_t>(m);
        } else if (key == "persistentkeepalive") {
            keepalive_ = static_cast<uint16_t>(std::atoi(val.c_str()));
        }
    }

    if (!havePriv) { status_ = "WireGuard: missing PrivateKey"; return false; }
    if (!havePeer) { status_ = "WireGuard: missing peer PublicKey"; return false; }
    if (!haveEndpoint) { status_ = "WireGuard: invalid Endpoint"; return false; }
    if (!haveAddress) { status_ = "WireGuard: missing Address"; return false; }
    if (!haveTarget) { status_ = "WireGuard: missing Target (server address on the LAN)"; return false; }
    if (localPort_ == 0) localPort_ = 18096;
    if (targetPort_ == 0) targetPort_ = 8096;

    return true;
}

// Written with raw write() so it also works inside a signal handler.
extern "C" void wgFaultHandler(int sig) {
    const char* msg = "!! CRASH: process received a fatal signal\n";
    int fd = ::open("sdmc:/switch/Switchfin/wg-log.txt", O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (fd >= 0) {
        ::write(fd, msg, std::strlen(msg));
        ::close(fd);
    }
    ::_exit(1);
}

void logLine(const std::string& msg) {
    wglog(msg);
}

void installCrashHandlers() {
    std::signal(SIGSEGV, wgFaultHandler);
    std::signal(SIGABRT, wgFaultHandler);
    std::signal(SIGILL, wgFaultHandler);
    std::signal(SIGFPE, wgFaultHandler);
    std::set_terminate([]() {
        rawlog("!! TERMINATE: uncaught exception");
        // Pull the exception text out so the log says WHAT happened.
        try {
            std::exception_ptr ep = std::current_exception();
            if (ep)
                std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            rawlog(e.what());
        } catch (...) {
            rawlog("exception of unknown type");
        }
        std::_Exit(1);
    });
}

// Exiting through HOME kills the process without running the cleanup
// handlers, so the tunnel is closed from the applet exit hook.
static AppletHookCookie g_exitHook;

static void onExitHook(AppletHookType type, void* param) {
    if (type == AppletHookType_OnExitRequest) {
        rawlog("E0: system requested exit, closing the tunnel");
        wgrelay::Relay::instance().stop();
        rawlog("E1: tunnel closed");
        // The launcher returns from the app with a Data Abort at address zero
        // because our threads and sockets outlive us in the shared process.
        // End the process ourselves.
        svcExitProcess();
    }
}

// C-level logging: no allocations, so it cannot throw.
void rawlog(const char* tag) {
    int fd = ::open("sdmc:/switch/Switchfin/wg-log.txt", O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (fd < 0)
        fd = ::open("/switch/Switchfin/wg-log.txt", O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (fd >= 0) {
        ::write(fd, tag, std::strlen(tag));
        ::write(fd, "\n", 1);
        ::fsync(fd);
        ::close(fd);
    }
}

bool Relay::start(const std::string& configPath) {
    rawlog("A: entered start()");
    std::lock_guard<std::mutex> lock(mutex_);
    rawlog("B: mutex locked");
    if (running_.load())
        return true;

    // The UI gets no artificial delay any more: starting late during the UI
    // bring-up is what used to kill the app.
    rawlog("C: no wait, continuing");

    wglog("step 1: reading config from " + configPath);
    if (!loadConfig(configPath)) {
        wglog("WARNING: " + status_);
        return false;
    }
    if (!enabled_) {
        status_ = "WireGuard: disabled in settings";
        wglog(status_);
        return false;
    }

    WgConfig cfg{};
    if (wg_key_from_base64(cfg.private_key, privateKey_.c_str()) != 0) {
        status_ = "WireGuard: PrivateKey is not valid base64";
        wglog("ERROR: " + status_);
        return false;
    }
    if (wg_key_from_base64(cfg.peer_public_key, peerKey_.c_str()) != 0) {
        status_ = "WireGuard: peer PublicKey is not valid base64";
        wglog("ERROR: " + status_);
        return false;
    }

    cfg.tunnel_ip.s_addr = inet_addr(address_.c_str());
    if (cfg.tunnel_ip.s_addr == INADDR_NONE) {
        status_ = "WireGuard: invalid Address (" + address_ + ")";
        wglog("ERROR: " + status_);
        return false;
    }
    std::strncpy(cfg.endpoint_host, endpointHost_.c_str(), sizeof(cfg.endpoint_host) - 1);
    cfg.endpoint_port = endpointPort_;
    cfg.keepalive_interval = keepalive_;
    // Tunnel MTU comes from the config (default 1200). Measured on a mobile
    // carrier: a 1268-byte outer packet passes, 1288 already vanishes, so a
    // 1280 tunnel MTU produced 1340-byte packets and a black hole.
    cfg.mtu = mtu_;
    cfg.has_preshared_key = 0;

    wglog("applet mode: " + std::to_string((int)appletGetAppletType()));
    wg_set_log_callback([](const char* m) { wglog(std::string("[lib] ") + m); });
    wglog("step 2: config parsed, calling wg_init");
    WgTunnel* tun = wg_init(&cfg);
    if (!tun) {
        status_ = "WireGuard: wg_init failed";
        wglog("ERROR: " + status_);
        return false;
    }

    wglog("step 3: handshake with " + endpointHost_ + ":" + std::to_string(endpointPort_));
    int err = wg_connect(tun);
    wglog("step 3a: wg_connect returned " + std::to_string(err));
 if (err != WG_OK) {
        status_ = "WireGuard: handshake failed (code " + std::to_string(err) + ")";
        wglog("ERROR: " + status_);
        wg_close(tun);
        return false;
    }

    wglog("step 4: handshake OK, starting threads");
    wglog("step 4a: tunnel MTU = " + std::to_string((int)wg_get_mtu(tun)));
    err = wg_start(tun);
    if (err != WG_OK) {
        status_ = "WireGuard: thread start failed (code " + std::to_string(err) + ")";
        wglog("ERROR: " + status_);
        wg_close(tun);
        return false;
    }

    wgnx::LwipRelayConfig relayCfg;
    // Relay messages used to go to the screen only; route them into the SD
    // log, otherwise the Jellyfin path stays invisible.
    relayCfg.debug_logging = true;
    relayCfg.log_callback = [](wgnx::LogLevel level, const char* msg) {
        wglog(std::string(level == wgnx::LogLevel::Error ? "[relay-ERR] " : "[relay] ") + (msg ? msg : ""));
    };

    wglog("step 5: creating LwipRelay");
    auto* relay = new wgnx::LwipRelay(tun, relayCfg);
    wglog("step 6: relay->start(" + address_ + ", " + target_ + ")");
    if (!relay->start(address_, target_)) {
        status_ = "WireGuard: relay did not start";
        wglog("ERROR: " + status_);
        delete relay;
        wg_close(tun);
        return false;
    }

    wglog("step 7: startTcpRelay (FIX: target port, EBADF survival, lossless stream)");
    uint16_t bound = relay->startTcpRelay(targetPort_, localPort_);
    if (bound != localPort_) {
        status_ = "WireGuard: could not bind port " + std::to_string(localPort_);
        wglog("ERROR: " + status_);
        relay->stop();
        delete relay;
        wg_close(tun);
        return false;
    }

    tunnel_ = tun;
    relay_ = relay;
    running_.store(true);
    status_ = "WireGuard: tunnel active, 127.0.0.1:" + std::to_string(localPort_) + " -> " + target_ + ":" + std::to_string(targetPort_);
    // Exiting through HOME ends the process without running code after the
    // main loop, so tunnel teardown is registered as an applet exit hook.
    appletHook(&g_exitHook, onExitHook, nullptr);
    rawlog("step 8: exit hook registered");
    wglog(status_);
    return true;
}

void Relay::stop() {
    rawlog("S1: starting shutdown");
    std::lock_guard<std::mutex> lock(mutex_);
    if (relay_) {
        rawlog("S2: stopping relay");
        static_cast<wgnx::LwipRelay*>(relay_)->stop();
        rawlog("S3: relay stopped, deleting");
        delete static_cast<wgnx::LwipRelay*>(relay_);
        rawlog("S4: relay deleted");
        relay_ = nullptr;
    }
    if (tunnel_) {
        rawlog("S5: wg_stop");
        wg_stop(static_cast<WgTunnel*>(tunnel_));
        rawlog("S6: wg_close");
        wg_close(static_cast<WgTunnel*>(tunnel_));
        rawlog("S7: tunnel closed");
        tunnel_ = nullptr;
    }
    rawlog("S8: shutdown finished");
    running_.store(false);
    status_ = "WireGuard: disabled";
}

std::string Relay::statusText() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

}  // namespace wgrelay
