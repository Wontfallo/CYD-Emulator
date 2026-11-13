#pragma once

#include "Arduino.h"

enum wl_status_t {
    WL_NO_SHIELD = 255,
    WL_IDLE_STATUS = 0,
    WL_NO_SSID_AVAIL = 1,
    WL_SCAN_COMPLETED = 2,
    WL_CONNECTED = 3,
    WL_CONNECT_FAILED = 4,
    WL_CONNECTION_LOST = 5,
    WL_DISCONNECTED = 6
};

enum wifi_mode_t {
    WIFI_OFF = 0,
    WIFI_STA = 1,
    WIFI_AP = 2,
    WIFI_AP_STA = 3
};

class IPAddress {
public:
    IPAddress() = default;
    explicit IPAddress(const String& s) : _s(s) {}
    String toString() const { return _s; }
private:
    String _s = "0.0.0.0";
};

class WiFiClass {
public:
    bool mode(wifi_mode_t m) {
        if (m == WIFI_OFF) {
            _status = WL_DISCONNECTED;
            vcyd::VirtualHw::instance().setWifiMeta(false, "", "0.0.0.0");
        }
        return true;
    }
    wl_status_t begin(const char* ssid, const char* pass = nullptr) {
        (void)pass;
        _ssid = ssid ? ssid : "";
        if (!_ssid.isEmpty()) {
            _status = WL_CONNECTED;
            _ip = IPAddress("192.168.1.48");
            vcyd::VirtualHw::instance().setWifiMeta(true, _ssid.c_str(), _ip.toString().c_str());
        } else {
            _status = WL_DISCONNECTED;
            vcyd::VirtualHw::instance().setWifiMeta(false, "", "0.0.0.0");
        }
        return _status;
    }
    void disconnect(bool wifioff = false, bool eraseap = false) {
        (void)wifioff; (void)eraseap;
        _status = WL_DISCONNECTED;
        vcyd::VirtualHw::instance().setWifiMeta(false, "", "0.0.0.0");
    }
    wl_status_t status() const { return _status; }
    IPAddress localIP() const { return _ip; }
    String SSID() const { return _ssid; }
    int32_t RSSI() const { return _status == WL_CONNECTED ? -42 : 0; }
private:
    wl_status_t _status = WL_DISCONNECTED;
    String _ssid;
    IPAddress _ip{"0.0.0.0"};
};

inline WiFiClass WiFi;

