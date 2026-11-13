#pragma once

#include "Arduino.h"
#include <filesystem>
#include <fstream>
#include <map>

class Preferences {
public:
    bool begin(const char* name, bool readOnly = false) {
        _ns = name ? name : "default";
        _readOnly = readOnly;
        _vals.clear();
        loadFromDisk();
        _opened = true;
        return true;
    }

    void end() {
        if (_opened && !_readOnly) saveToDisk();
        _opened = false;
    }

    bool clear() {
        _vals.clear();
        if (!_readOnly) saveToDisk();
        return true;
    }

    uint8_t getUChar(const char* key, uint8_t def = 0) const {
        auto it = _vals.find(key ? key : "");
        if (it == _vals.end()) return def;
        return static_cast<uint8_t>(std::strtoul(it->second.c_str(), nullptr, 10));
    }
    uint16_t getUShort(const char* key, uint16_t def = 0) const {
        auto it = _vals.find(key ? key : "");
        if (it == _vals.end()) return def;
        return static_cast<uint16_t>(std::strtoul(it->second.c_str(), nullptr, 10));
    }
    bool getBool(const char* key, bool def = false) const {
        auto it = _vals.find(key ? key : "");
        if (it == _vals.end()) return def;
        return it->second == "1" || it->second == "true";
    }
    String getString(const char* key, const char* def = "") const {
        auto it = _vals.find(key ? key : "");
        if (it == _vals.end()) return String(def ? def : "");
        return String(it->second);
    }

    size_t putUChar(const char* key, uint8_t v) {
        if (_readOnly || !key) return 0;
        _vals[key] = std::to_string(v);
        saveToDisk();
        return 1;
    }
    size_t putUShort(const char* key, uint16_t v) {
        if (_readOnly || !key) return 0;
        _vals[key] = std::to_string(v);
        saveToDisk();
        return 2;
    }
    size_t putBool(const char* key, bool v) {
        if (_readOnly || !key) return 0;
        _vals[key] = v ? "1" : "0";
        saveToDisk();
        return 1;
    }
    size_t putString(const char* key, const char* v) {
        if (_readOnly || !key) return 0;
        _vals[key] = v ? v : "";
        saveToDisk();
        return _vals[key].size();
    }
    size_t putString(const char* key, const String& v) {
        return putString(key, v.c_str());
    }

private:
    std::string filePath() const {
        return vcyd::VirtualHw::instance().prefsDir() + "/" + _ns + ".kv";
    }

    void loadFromDisk() {
        std::ifstream f(filePath());
        if (!f.is_open()) return;
        std::string line;
        while (std::getline(f, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);
            if (!v.empty() && v.back() == '\r') v.pop_back();
            _vals[k] = v;
        }
    }

    void saveToDisk() const {
        std::error_code ec;
        std::filesystem::create_directories(vcyd::VirtualHw::instance().prefsDir(), ec);
        std::ofstream f(filePath(), std::ios::trunc);
        if (!f.is_open()) return;
        for (const auto& kv : _vals) {
            f << kv.first << "=" << kv.second << "\n";
        }
    }

    std::string _ns;
    bool _readOnly = false;
    bool _opened = false;
    std::map<std::string, std::string> _vals;
};

