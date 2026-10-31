#include "sysfs.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace linosd::sysfs {

std::optional<std::string> readText(const std::string& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;
    std::stringstream ss;
    ss << f.rdbuf();
    if (f.bad()) return std::nullopt;
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

std::optional<int64_t> readInt(const std::string& path) {
    auto t = readText(path);
    if (!t || t->empty()) return std::nullopt;
    char* end = nullptr;
    long long v = std::strtoll(t->c_str(), &end, 10);
    if (end == t->c_str()) return std::nullopt;
    return static_cast<int64_t>(v);
}

std::optional<double> readDouble(const std::string& path) {
    auto t = readText(path);
    if (!t || t->empty()) return std::nullopt;
    char* end = nullptr;
    double v = std::strtod(t->c_str(), &end);
    if (end == t->c_str()) return std::nullopt;
    return v;
}

std::vector<std::string> listDir(const std::string& path) {
    std::vector<std::string> out;
    DIR* d = opendir(path.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        out.push_back(n);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string pciName(uint32_t vendor, uint32_t device) {
    const char* paths[] = {"/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids", "/usr/share/pci.ids"};
    for (const char* p : paths) {
        std::ifstream f(p);
        if (!f) continue;
        char vbuf[8], dbuf[8];
        std::snprintf(vbuf, sizeof(vbuf), "%04x", vendor);
        std::snprintf(dbuf, sizeof(dbuf), "%04x", device);
        std::string line, vendorName;
        bool inVendor = false;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (line[0] != '\t') {
                if (inVendor) break;
                if (line.compare(0, 4, vbuf) == 0) {
                    inVendor = true;
                    vendorName = trim(line.substr(4));
                }
                continue;
            }
            if (inVendor && line.size() > 6 && line[1] != '\t' && line.compare(1, 4, dbuf) == 0) {
                std::string dev = trim(line.substr(5));
                // "Navi 21 [Radeon RX 6800/6800 XT / 6900 XT]" -> prefere o nome entre colchetes
                size_t lb = dev.find('['), rb = dev.rfind(']');
                if (lb != std::string::npos && rb != std::string::npos && rb > lb) dev = dev.substr(lb + 1, rb - lb - 1);
                return dev;
            }
        }
        if (inVendor) return vendorName;
    }
    return {};
}

}  // namespace linosd::sysfs
