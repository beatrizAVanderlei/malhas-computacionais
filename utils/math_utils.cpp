#include "math_utils.h"
#include <cmath>

namespace math_utils {

    std::array<float, 3> cross_product(const std::array<float, 3>& a,
                                       const std::array<float, 3>& b) {
        return { a[1]*b[2] - a[2]*b[1],
                 a[2]*b[0] - a[0]*b[2],
                 a[0]*b[1] - a[1]*b[0] };
    }

    float norm(const std::array<float, 3>& v) {
        return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    }

    std::array<float, 3> normalize(const std::array<float, 3>& v) {
        float n = norm(v);
        if (n == 0)
            return {0.0f, 0.0f, 0.0f};
        return { v[0]/n, v[1]/n, v[2]/n };
    }

    std::array<float, 3> calculate_normal(const std::array<float, 3>& v1,
                                            const std::array<float, 3>& v2,
                                            const std::array<float, 3>& v3) {
        std::array<float, 3> edge1 = { v2[0] - v1[0], v2[1] - v1[1], v2[2] - v1[2] };
        std::array<float, 3> edge2 = { v3[0] - v1[0], v3[1] - v1[1], v3[2] - v1[2] };
        auto cp = cross_product(edge1, edge2);
        return normalize(cp);
    }

} // namespace math_utils
