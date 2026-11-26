// util.hpp - small helpers shared by every module (time, strings, logging).
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace util {

inline int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

inline std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Split "a.b.c" on '.' -> {"a","b","c"}. Empty labels are skipped.
inline std::vector<std::string> splitLabels(const std::string& host) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= host.size()) {
        size_t dot = host.find('.', start);
        if (dot == std::string::npos) dot = host.size();
        if (dot > start) out.push_back(host.substr(start, dot - start));
        start = dot + 1;
    }
    return out;
}

// Hostnames come from untrusted clients and end up inside the block page,
// so they MUST be escaped (otherwise a crafted CONNECT target is an XSS vector).
inline std::string htmlEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

inline std::string isoTime(int64_t ms) {
    std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S UTC", &tm);
    return buf;
}

// One mutex so lines from different threads never interleave.
inline void log(const char* level, const std::string& msg) {
    static std::mutex m;
    std::lock_guard<std::mutex> g(m);
    std::cerr << isoTime(nowMs()) << " [" << level << "] " << msg << std::endl;
}
inline void info(const std::string& m) { log("INFO", m); }
inline void warn(const std::string& m) { log("WARN", m); }

}  // namespace util
