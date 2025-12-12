#ifndef PATHTRACER_H
#define PATHTRACER_H

#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>
#include <fstream> // [CORREÇÃO] Adicionado para corrigir o erro 'incomplete type'
#include <cstdint>

// ==========================================
// 1. MATH & RANDOM
// ==========================================
inline uint32_t hash_pcg(uint32_t& state) {
    uint32_t prev = state;
    state = state * 747796405u + 2891336453u;
    uint32_t word = ((prev >> ((prev >> 28u) + 4u)) ^ prev) * 277803737u;
    return (word >> 22u) ^ word;
}

inline float random_float(uint32_t& seed) {
    return hash_pcg(seed) * (1.0f / 4294967296.0f);
}

struct Vec3 {
    double x, y, z;
    Vec3(double x_ = 0, double y_ = 0, double z_ = 0) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& b) const { return Vec3(x + b.x, y + b.y, z + b.z); }
    Vec3 operator-(const Vec3& b) const { return Vec3(x - b.x, y - b.y, z - b.z); }
    Vec3 operator*(double b) const { return Vec3(x * b, y * b, z * b); }
    Vec3 operator*(const Vec3& b) const { return Vec3(x * b.x, y * b.y, z * b.z); }
    Vec3 norm() { return *this = *this * (1.0 / std::sqrt(x * x + y * y + z * z)); }
    double dot(const Vec3& b) const { return x * b.x + y * b.y + z * b.z; }
    Vec3 cross(const Vec3& b) const { return Vec3(y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x); }
    double operator[](int i) const { return (&x)[i]; }
};

struct Ray {
    Vec3 o, d, inv_d;
    Ray(Vec3 o_, Vec3 d_) : o(o_), d(d_) {
        inv_d = Vec3(1.0 / (std::abs(d.x) > 1e-8 ? d.x : 1e-8),
                     1.0 / (std::abs(d.y) > 1e-8 ? d.y : 1e-8),
                     1.0 / (std::abs(d.z) > 1e-8 ? d.z : 1e-8));
    }
};

// ==========================================
// 2. BVH STRUCTURES
// ==========================================
struct AABB {
    Vec3 min, max;
    AABB() {
        double inf = std::numeric_limits<double>::infinity();
        min = Vec3(inf, inf, inf); max = Vec3(-inf, -inf, -inf);
    }
    void expand(const Vec3& p) {
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }
    inline bool intersect(const Ray& r, double t_max) const {
        double t1 = (min.x - r.o.x) * r.inv_d.x;
        double t2 = (max.x - r.o.x) * r.inv_d.x;
        double tmin = std::min(t1, t2);
        double tmax = std::max(t1, t2);
        t1 = (min.y - r.o.y) * r.inv_d.y;
        t2 = (max.y - r.o.y) * r.inv_d.y;
        tmin = std::max(tmin, std::min(t1, t2));
        tmax = std::min(tmax, std::max(t1, t2));
        t1 = (min.z - r.o.z) * r.inv_d.z;
        t2 = (max.z - r.o.z) * r.inv_d.z;
        tmin = std::max(tmin, std::min(t1, t2));
        tmax = std::min(tmax, std::max(t1, t2));
        return tmax >= tmin && tmin < t_max && tmax > 0;
    }
};

struct BVHNode {
    AABB box;
    BVHNode *left = nullptr, *right = nullptr;
    int firstTriIndex = -1, triCount = 0;
};

struct SceneData {
    std::vector<Vec3> vertices;
    std::vector<std::vector<unsigned int>> faces;
    std::vector<int> triIndices;
    BVHNode* bvhRoot = nullptr;
    ~SceneData() { clearTree(bvhRoot); }
    void clearTree(BVHNode* node) {
        if (!node) return;
        clearTree(node->left);
        clearTree(node->right);
        delete node;
    }
};

extern SceneData* g_renderMesh;

// ==========================================
// 3. BVH BUILDER
// ==========================================
inline Vec3 getCentroid(const SceneData& scene, int triIdx) {
    const auto& f = scene.faces[triIdx];
    return (scene.vertices[f[0]] + scene.vertices[f[1]] + scene.vertices[f[2]]) * 0.333333;
}

inline BVHNode* buildBVHRecursive(SceneData& scene, int left, int right) {
    BVHNode* node = new BVHNode();
    for (int i = left; i < right; ++i) {
        int triIdx = scene.triIndices[i];
        const auto& f = scene.faces[triIdx];
        node->box.expand(scene.vertices[f[0]]);
        node->box.expand(scene.vertices[f[1]]);
        node->box.expand(scene.vertices[f[2]]);
    }
    int count = right - left;
    if (count <= 2) {
        node->firstTriIndex = left; node->triCount = count;
        return node;
    }
    Vec3 size = node->box.max - node->box.min;
    int axis = (size.x > size.y) ? (size.x > size.z ? 0 : 2) : (size.y > size.z ? 1 : 2);
    double split = node->box.min[axis] + size[axis] * 0.5;

    int mid = left;
    for (int i = left; i < right; ++i) {
        if (getCentroid(scene, scene.triIndices[i])[axis] < split) {
            std::swap(scene.triIndices[i], scene.triIndices[mid++]);
        }
    }
    if (mid == left || mid == right) mid = left + count/2;

    node->left = buildBVHRecursive(scene, left, mid);
    node->right = buildBVHRecursive(scene, mid, right);
    return node;
}

inline void buildBVH(SceneData& scene) {
    if (scene.faces.empty()) return;
    scene.triIndices.resize(scene.faces.size());
    for(size_t i=0; i<scene.faces.size(); ++i) scene.triIndices[i] = i;
    scene.bvhRoot = buildBVHRecursive(scene, 0, scene.faces.size());
    std::cout << "BVH construida." << std::endl;
}

// ==========================================
// 4. INTERSECTION (OTIMIZADO)
// ==========================================
inline double intersectTriangle(const Ray& r, const Vec3& v0, const Vec3& v1, const Vec3& v2) {
    const double EPS = 1e-6;
    Vec3 e1 = v1 - v0;
    Vec3 e2 = v2 - v0;
    Vec3 h = r.d.cross(e2);
    double a = e1.dot(h);
    if (a > -EPS && a < EPS) return 0;
    double f = 1.0 / a;
    Vec3 s = r.o - v0;
    double u = f * s.dot(h);
    if (u < 0.0 || u > 1.0) return 0;
    Vec3 q = s.cross(e1);
    double v = f * r.d.dot(q);
    if (v < 0.0 || u + v > 1.0) return 0;
    double t = f * e2.dot(q);
    return (t > EPS) ? t : 0;
}

inline void intersectBVH_Iterative(const Ray& r, double& t_closest, int& id_closest, Vec3& normal_closest, const SceneData& scene) {
    const BVHNode* stack[64];
    int stackPtr = 0;
    stack[stackPtr++] = scene.bvhRoot;

    while (stackPtr > 0) {
        const BVHNode* node = stack[--stackPtr];
        if (!node->box.intersect(r, t_closest)) continue;

        if (node->triCount > 0) {
            for (int i = 0; i < node->triCount; ++i) {
                int idx = scene.triIndices[node->firstTriIndex + i];
                const auto& f = scene.faces[idx];
                const Vec3& v0 = scene.vertices[f[0]];
                const Vec3& v1 = scene.vertices[f[1]];
                const Vec3& v2 = scene.vertices[f[2]];

                double d = intersectTriangle(r, v0, v1, v2);
                if (d > 0 && d < t_closest) {
                    t_closest = d;
                    id_closest = 1;
                    normal_closest = (v1 - v0).cross(v2 - v0).norm();
                }
            }
        } else {
            if (node->right) stack[stackPtr++] = node->right;
            if (node->left) stack[stackPtr++] = node->left;
        }
    }
}

inline bool getIntersection(const Ray& r, double& t, int& id, Vec3& normalHit) {
    t = 1e20; id = 0; bool hit = false;

    if (g_renderMesh && g_renderMesh->bvhRoot) {
        intersectBVH_Iterative(r, t, id, normalHit, *g_renderMesh);
        if (id == 1) hit = true;
    }

    if (std::abs(r.d.y) > 1e-6) {
        double t_plane = (-1.2 - r.o.y) * r.inv_d.y;
        if (t_plane > 1e-4 && t_plane < t) {
            t = t_plane; id = 2; hit = true; normalHit = Vec3(0, 1, 0);
        }
    }

    Vec3 L(10.0, 20.0, 10.0);
    Vec3 op = L - r.o;
    double b = op.dot(r.d);
    double det = b * b - op.dot(op) + 25.0;
    if (det > 0) {
        double t_luz = b - std::sqrt(det);
        if (t_luz > 1e-4 && t_luz < t) {
            t = t_luz; id = 3; hit = true; normalHit = (r.o + r.d * t - L).norm();
        }
    }
    return hit;
}

// ==========================================
// 5. RADIANCE ITERATIVO
// ==========================================
// [CORREÇÃO] Removemos o argumento 'depth' que causava erro
inline Vec3 radiance(Ray r, uint32_t& seed) {
    Vec3 throughput(1.0, 1.0, 1.0);
    Vec3 finalColor(0.0, 0.0, 0.0);

    for (int depth = 0; depth < 5; ++depth) {
        double t; int id; Vec3 n;

        if (!getIntersection(r, t, id, n)) {
            return finalColor + throughput * Vec3(0.05, 0.05, 0.05); // Céu
        }

        if (id == 3) {
            return finalColor + throughput * Vec3(12.0, 12.0, 12.0); // Luz
        }

        Vec3 f;
        Vec3 x = r.o + r.d * t;
        Vec3 nl = n.dot(r.d) < 0 ? n : n * -1;

        if (id == 1) f = Vec3(0.7, 0.7, 0.7);
        else {
            bool grid = (int(std::floor(x.x) + std::floor(x.z)) & 1) == 0;
            f = grid ? Vec3(0.8, 0.8, 0.8) : Vec3(0.2, 0.2, 0.2);
        }

        double p = std::max({f.x, f.y, f.z});
        if (depth > 2) {
            if (random_float(seed) < p) f = f * (1.0 / p);
            else break;
        }

        throughput = throughput * f;

        double r1 = 2 * 3.14159 * random_float(seed);
        double r2 = random_float(seed);
        double r2s = std::sqrt(r2);
        Vec3 w = nl;
        Vec3 u = ((std::abs(w.x) > 0.1 ? Vec3(0, 1, 0) : Vec3(1, 0, 0)).cross(w)).norm();
        Vec3 v = w.cross(u);
        Vec3 d = (u * std::cos(r1) * r2s + v * std::sin(r1) * r2s + w * std::sqrt(1 - r2)).norm();

        r = Ray(x, d);
        r.o = r.o + r.d * 1e-4;
    }
    return finalColor;
}

inline double clamp(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
inline int toInt(double x) { x = x/(1+x); return int(std::pow(clamp(x), 1/2.2) * 255 + .5); }

// Render Estático
inline void renderPathTracing(const std::vector<std::array<float, 3>>& vertices_in,
                       const std::vector<std::vector<unsigned int>>& faces_in,
                       const std::string& outputName) {
    SceneData scene;
    for(const auto& v : vertices_in) scene.vertices.push_back(Vec3(v[0], v[1], v[2]));
    scene.faces = faces_in;

    std::cout << "Construindo BVH..." << std::endl;
    buildBVH(scene);
    g_renderMesh = &scene;

    int w = 800, h = 600;
    int samps = 100;
    Ray cam(Vec3(0, 0, 4.0), Vec3(0, 0, -1).norm());
    Vec3 cx = Vec3(w * .5135 / h, 0, 0);
    Vec3 cy = Vec3(0, -.5135, 0);
    std::vector<Vec3> c(w * h);

    #pragma omp parallel for schedule(dynamic, 1)
    for (int y = 0; y < h; y++) {
        uint32_t seed = y * 12345;
        for (int x = 0; x < w; x++) {
            for (int sy = 0; sy < 10; ++sy) {
                for (int sx = 0; sx < 10; ++sx) {
                    float r1 = (sx + random_float(seed)) * 0.1f;
                    float r2 = (sy + random_float(seed)) * 0.1f;
                    float dx = (r1 < 0.5f) ? std::sqrt(2*r1)-1 : 1-std::sqrt(2-2*r1);
                    float dy = (r2 < 0.5f) ? std::sqrt(2*r2)-1 : 1-std::sqrt(2-2*r2);
                    Vec3 d = cx * (((x + dx*0.5 + 0.5)/w)-0.5)*2 + cy * (((y + dy*0.5 + 0.5)/h)-0.5)*2 + cam.d;

                    // [CORREÇÃO] Removido o argumento '0' (profundidade) da chamada
                    c[(h-1-y)*w+x] = c[(h-1-y)*w+x] + radiance(Ray(cam.o, d.norm()), seed) * (1.0/samps);
                }
            }
        }
    }
    std::ofstream ofs(outputName);
    ofs << "P3\n" << w << " " << h << "\n255\n";
    for (int i = 0; i < w * h; i++)
        ofs << toInt(c[i].x) << " " << toInt(c[i].y) << " " << toInt(c[i].z) << " ";
    ofs.close();
    g_renderMesh = nullptr;
}

#endif