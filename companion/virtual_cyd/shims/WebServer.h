#pragma once

#include "Arduino.h"
#include <functional>
#include <map>
#include <string>

#ifndef HTTP_GET
#define HTTP_GET 1
#endif
#ifndef HTTP_POST
#define HTTP_POST 2
#endif

class WebServer {
public:
    using THandlerFunction = std::function<void()>;

    explicit WebServer(int port = 80) : _port(port) {}

    void on(const char* uri, THandlerFunction fn) {
        if (uri) _routes[uri] = fn;
    }
    void on(const char* uri, int method, THandlerFunction fn) {
        (void)method;
        if (uri) _routes[uri] = fn;
    }
    void begin() { _started = true; }
    void handleClient() {}

    bool hasArg(const char* name) const {
        return name && _args.find(name) != _args.end();
    }
    String arg(const char* name) const {
        if (!name) return String("");
        auto it = _args.find(name);
        return it == _args.end() ? String("") : String(it->second);
    }
    void send(int code, const char* content_type, const String& content) {
        _lastCode = code;
        _lastBody = content;
        (void)content_type;
    }

private:
    int _port = 80;
    bool _started = false;
    int _lastCode = 200;
    String _lastBody;
    std::map<std::string, THandlerFunction> _routes;
    std::map<std::string, std::string> _args;
};

