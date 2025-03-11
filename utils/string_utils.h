#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <string>
#include <vector>

namespace string_utils {

    std::string to_lower(const std::string &s);
    std::string to_upper(const std::string &s);
    std::string trim(const std::string &s);
    std::vector<std::string> split(const std::string &s);
    bool starts_with(const std::string &s, const std::string &prefix);
    std::string get_extension(const std::string &filename);
    std::string fix_filename(const std::string &filename);

} // namespace string_utils

#endif // STRING_UTILS_H
