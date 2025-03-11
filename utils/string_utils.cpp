#include "string_utils.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iterator>

namespace string_utils {

std::string to_lower(const std::string &s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}

std::string to_upper(const std::string &s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), ::toupper);
    return out;
}

std::string trim(const std::string &s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(static_cast<unsigned char>(*start))) {
        ++start;
    }
    auto end = s.end();
    while (end != start && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(start, end);
}

std::vector<std::string> split(const std::string &s) {
    std::istringstream iss(s);
    return { std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>{} };
}

bool starts_with(const std::string &s, const std::string &prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

std::string get_extension(const std::string &filename) {
    std::string clean = filename;
    if (!clean.empty() && clean.front() == '"' && clean.back() == '"') {
        clean = clean.substr(1, clean.size() - 2);
    }
    clean = trim(clean);
    size_t pos = clean.find_last_of('.');
    if (pos == std::string::npos)
        return "";
    std::string ext = clean.substr(pos);
    ext.erase(std::remove_if(ext.begin(), ext.end(), [](unsigned char c) {
        return std::isspace(c) || std::iscntrl(c);
    }), ext.end());
    while (!ext.empty() && !std::isalnum(ext.back())) {
        ext.pop_back();
    }
    return to_lower(ext);
}

std::string fix_filename(const std::string &filename) {
    std::string fixed = filename;
    if (!fixed.empty() && fixed.front() == '"' && fixed.back() == '"') {
        fixed = fixed.substr(1, fixed.size() - 2);
    }
    fixed = trim(fixed);
    size_t firstDot = fixed.find('.');
    size_t lastDot = fixed.find_last_of('.');
    if (firstDot != std::string::npos && lastDot != firstDot) {
        fixed = fixed.substr(0, lastDot);
    }
    return fixed;
}

} // namespace string_utils
