#pragma once

#include "Arduino.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

class File : public Stream {
public:
    File() = default;
    explicit File(const std::string& path, bool writeMode)
        : _path(path), _writeMode(writeMode) {
        if (!writeMode) {
            std::ifstream in(path, std::ios::binary);
            if (in.is_open()) {
                _buf.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                _open = true;
            }
        } else {
            _out = std::make_shared<std::ofstream>(path, std::ios::binary | std::ios::trunc);
            _open = _out && _out->is_open();
        }
    }

    operator bool() const { return _open; }
    size_t size() const { return _buf.size(); }

    int available() override {
        return (_open && !_writeMode && _pos < _buf.size()) ? static_cast<int>(_buf.size() - _pos) : 0;
    }
    int read() override {
        if (!_open || _writeMode || _pos >= _buf.size()) return -1;
        return static_cast<unsigned char>(_buf[_pos++]);
    }
    int peek() override {
        if (!_open || _writeMode || _pos >= _buf.size()) return -1;
        return static_cast<unsigned char>(_buf[_pos]);
    }
    size_t readBytes(char* dst, size_t len) override {
        if (!_open || _writeMode || !dst || _pos >= _buf.size()) return 0;
        size_t avail = _buf.size() - _pos;
        size_t n = len < avail ? len : avail;
        std::memcpy(dst, _buf.data() + _pos, n);
        _pos += n;
        return n;
    }

    size_t write(uint8_t b) override {
        if (!_open || !_writeMode || !_out) return 0;
        _out->put(static_cast<char>(b));
        return 1;
    }
    size_t write(const uint8_t* buf, size_t sz) override {
        if (!_open || !_writeMode || !_out || !buf) return 0;
        _out->write(reinterpret_cast<const char*>(buf), static_cast<std::streamsize>(sz));
        return sz;
    }

    void close() {
        if (_out) {
            _out->flush();
            _out->close();
            _out.reset();
        }
        _open = false;
    }

private:
    std::string _path;
    bool _writeMode = false;
    bool _open = false;
    std::vector<char> _buf;
    size_t _pos = 0;
    std::shared_ptr<std::ofstream> _out;
};

class LittleFSFS {
public:
    bool begin(bool formatOnFail = false) {
        (void)formatOnFail;
        std::error_code ec;
        std::filesystem::create_directories(vcyd::VirtualHw::instance().littlefsRoot(), ec);
        _mounted = true;
        return true;
    }

    bool exists(const char* path) const {
        if (!_mounted || !path) return false;
        std::error_code ec;
        return std::filesystem::exists(resolvePath(path), ec);
    }

    File open(const char* path, const char* mode = "r") const {
        if (!_mounted || !path) return File();
        bool w = mode && (mode[0] == 'w' || mode[0] == 'a');
        std::string full = resolvePath(path);
        if (w) {
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(full).parent_path(), ec);
        }
        return File(full, w);
    }

    bool remove(const char* path) const {
        if (!_mounted || !path) return false;
        std::error_code ec;
        return std::filesystem::remove(resolvePath(path), ec);
    }

private:
    std::string resolvePath(const char* rel) const {
        std::string p = rel ? rel : "";
        while (!p.empty() && p.front() == '/') p.erase(p.begin());
        return vcyd::VirtualHw::instance().littlefsRoot() + "/" + p;
    }
    bool _mounted = false;
};

inline LittleFSFS LittleFS;

