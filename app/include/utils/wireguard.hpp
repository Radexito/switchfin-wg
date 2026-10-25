#pragma once

#include <atomic>
#include <mutex>
#include <string>

namespace wgrelay {

/*
 * Brings up a WireGuard tunnel (wg-nx) and exposes a local TCP port that is
 * tunnelled to the Jellyfin server on the home network. Switchfin then connects
 * to http://127.0.0.1:<LocalPort> and all traffic (API plus video stream) goes
 * through the tunnel, with no sysmodule and no Horizon socket hooks.
 *
 * Konfiguracja: /switch/Switchfin/wireguard.conf
 *
 *   [Interface]
 *   PrivateKey = <base64>
 *   Address    = 10.10.0.2
 *   Target     = 192.168.1.5
 *   TargetPort = 8096
 *   LocalPort  = 18096
 *   MTU        = 1200
 *
 *   [Peer]
 *   PublicKey  = <base64>
 *   Endpoint   = vpn.example.com:51820
 *   PersistentKeepalive = 25
 */
// Appends one line to the log on the SD card (safe from a worker thread).
void logLine(const std::string& msg);

// Tunnel settings, in the shape the settings screen edits them.
struct Settings {
    bool enabled = true;
    std::string privateKey;
    std::string publicKey;
    std::string endpoint;  // host:port
    std::string address;   // console address inside the tunnel
    std::string target;    // adres serwera Jellyfin widziany z serwera VPN
    unsigned int targetPort = 8096;
    unsigned int localPort = 18096;
    unsigned int mtu = 1200;
    unsigned int keepalive = 25;
};

// Path to the config file (sdmc: on the console, plain path on PC).
std::string configPath();

// Reads settings from the file. A missing Enabled key means the tunnel is on,
// so existing configs keep working unchanged.
Settings readSettings(const std::string& path);

// Writes settings back to the file, creating it when needed.
bool writeSettings(const std::string& path, const Settings& s);

// C-level logging: no allocations, exception safe.
void rawlog(const char* tag);

// Installs fatal-signal handlers and a custom terminate so every crash leaves
// a line in the log instead of the process vanishing silently.
void installCrashHandlers();

class Relay {
  public:
    static Relay& instance();

    // Returns true when the tunnel and the local relay came up.
    bool start(const std::string& configPath);

    // Stops the relay and the tunnel. Safe to call more than once.
    void stop();

    bool isRunning() const { return running_.load(); }

    // Krotki opis stanu do UI / logow.
    std::string statusText() const;

    // Parses without starting anything (unit test / preview)
    bool loadConfig(const std::string& configPath);

  private:
    Relay() = default;
    ~Relay() = default;
    Relay(const Relay&) = delete;
    Relay& operator=(const Relay&) = delete;

    mutable std::mutex mutex_;
    std::atomic<bool> running_{false};

    void* tunnel_ = nullptr;   // WgTunnel*
    void* relay_ = nullptr;    // wgnx::LwipRelay*

    std::string status_ = "WireGuard: disabled";

    // pola z pliku konfiguracyjnego
    std::string privateKey_;
    std::string peerKey_;
    std::string address_;
    std::string target_;
    std::string endpointHost_;
    uint16_t endpointPort_ = 51820;
    uint16_t targetPort_ = 8096;
    uint16_t localPort_ = 18096;
    uint16_t keepalive_ = 25;
    // Tunnel MTU. 1200 is safe on mobile networks too: the outer packet is
    // MTU + 60 bytes (IP + UDP + WireGuard) and carriers often give a 1280-byte
    // path. Can be overridden with the MTU key in the config file.
    uint16_t mtu_ = 1200;
    // The tunnel can be turned off from the settings screen (Enabled = 0).
    bool enabled_ = true;
};

}  // namespace wgrelay
