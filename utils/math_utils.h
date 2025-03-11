#ifndef MATH_UTILS_H
#define MATH_UTILS_H

#include <array>

namespace math_utils {

    std::array<float, 3> cross_product(const std::array<float, 3>& a,
                                       const std::array<float, 3>& b);
    float norm(const std::array<float, 3>& v);
    std::array<float, 3> normalize(const std::array<float, 3>& v);
    std::array<float, 3> calculate_normal(const std::array<float, 3>& v1,
                                            const std::array<float, 3>& v2,
                                            const std::array<float, 3>& v3);

} // namespace math_utils

#endif // MATH_UTILS_H
