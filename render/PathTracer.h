#ifndef PATHTRACER_H
#define PATHTRACER_H

#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>
#include <fstream>
#include <cstdint>

// ==========================================
// 1. MATH & HELPERS
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

// Structs
struct PtVec2 { float u, v; };

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
    double length() const { return std::sqrt(x*x + y*y + z*z); }
    double operator[](int i) const { return (&x)[i]; }
};

inline Vec3 randomUnitVector(uint32_t& seed) {
    double z = random_float(seed) * 2.0 - 1.0;
    double a = random_float(seed) * 2.0 * 3.1415926;
    double r = std::sqrt(1.0 - z * z);
    return Vec3(r * std::cos(a), r * std::sin(a), z);
}

struct Ray {
    Vec3 o, d, inv_d;
    Ray(Vec3 o_, Vec3 d_) : o(o_), d(d_) {
        inv_d = Vec3(1.0 / (std::abs(d.x) > 1e-8 ? d.x : 1e-8),
                     1.0 / (std::abs(d.y) > 1e-8 ? d.y : 1e-8),
                     1.0 / (std::abs(d.z) > 1e-8 ? d.z : 1e-8));
    }
};

// [OTIMIZAÇÃO] Usamos float para não precisar converter/calcular gama a cada raio
struct TextureData {
    int width, height;
    std::vector<float> pixels; // Agora armazena float linear (R, G, B)
};

// ==========================================
// 2. BVH
// ==========================================
struct AABB {
    Vec3 min, max;
    AABB() { double inf = 1e20; min = Vec3(inf, inf, inf); max = Vec3(-inf, -inf, -inf); }
    void expand(const Vec3& p) {
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }
    inline bool intersect(const Ray& r, double t_max) const {
        double t1 = (min.x - r.o.x) * r.inv_d.x; double t2 = (max.x - r.o.x) * r.inv_d.x;
        double tmin = std::min(t1, t2), tmax = std::max(t1, t2);
        t1 = (min.y - r.o.y) * r.inv_d.y; t2 = (max.y - r.o.y) * r.inv_d.y;
        tmin = std::max(tmin, std::min(t1, t2)); tmax = std::min(tmax, std::max(t1, t2));
        t1 = (min.z - r.o.z) * r.inv_d.z; t2 = (max.z - r.o.z) * r.inv_d.z;
        tmin = std::max(tmin, std::min(t1, t2)); tmax = std::min(tmax, std::max(t1, t2));
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

    std::vector<TextureData> textures;
    std::vector<int> faceTextureID;
    std::vector<std::vector<PtVec2>> faceUVs;

    BVHNode* bvhRoot = nullptr;
    ~SceneData() { clearTree(bvhRoot); }
    void clearTree(BVHNode* node) { if (!node) return; clearTree(node->left); clearTree(node->right); delete node; }
};

extern SceneData* g_renderMesh;

// --- BVH Builder ---
inline Vec3 getCentroid(const SceneData& scene, int triIdx) {
    const auto& f = scene.faces[triIdx];
    return (scene.vertices[f[0]] + scene.vertices[f[1]] + scene.vertices[f[2]]) * 0.333333;
}
inline BVHNode* buildBVHRecursive(SceneData& scene, int left, int right) {
    BVHNode* node = new BVHNode();
    for (int i = left; i < right; ++i) {
        int idx = scene.triIndices[i];
        const auto& f = scene.faces[idx];
        node->box.expand(scene.vertices[f[0]]);
        node->box.expand(scene.vertices[f[1]]);
        node->box.expand(scene.vertices[f[2]]);
    }
    int count = right - left;
    if (count <= 2) { node->firstTriIndex = left; node->triCount = count; return node; }

    Vec3 size = node->box.max - node->box.min;
    int axis = (size.x > size.y) ? (size.x > size.z ? 0 : 2) : (size.y > size.z ? 1 : 2);
    double split = node->box.min[axis] + size[axis] * 0.5;
    int mid = left;
    for (int i = left; i < right; ++i) {
        if (getCentroid(scene, scene.triIndices[i])[axis] < split)
            std::swap(scene.triIndices[i], scene.triIndices[mid++]);
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
}

// ==========================================
// 4. INTERSECTION
// ==========================================
inline double intersectTriangle(const Ray& r, const Vec3& v0, const Vec3& v1, const Vec3& v2, double& outU, double& outV) {
    const double EPS = 1e-6;
    Vec3 e1 = v1 - v0; Vec3 e2 = v2 - v0;
    Vec3 h = r.d.cross(e2);
    double a = e1.dot(h);
    if (a > -EPS && a < EPS) return 0;
    double f = 1.0 / a;
    Vec3 s = r.o - v0;
    outU = f * s.dot(h);
    if (outU < 0.0 || outU > 1.0) return 0;
    Vec3 q = s.cross(e1);
    outV = f * r.d.dot(q);
    if (outV < 0.0 || outU + outV > 1.0) return 0;
    double t = f * e2.dot(q);
    return (t > EPS) ? t : 0;
}

// ==========================================
// 5. AMOSTRAGEM DE TEXTURA (SUPER RÁPIDA)
// ==========================================
inline Vec3 getPixel(const TextureData& tex, int x, int y) {
    x = std::max(0, std::min(x, tex.width - 1));
    y = std::max(0, std::min(y, tex.height - 1));
    int idx = (y * tex.width + x) * 3;
    // [OTIMIZAÇÃO] Leitura direta do float pré-calculado. Sem pow() aqui.
    return Vec3(tex.pixels[idx], tex.pixels[idx+1], tex.pixels[idx+2]);
}

inline Vec3 sampleTexture(const TextureData& tex, double u, double v) {
    if (tex.pixels.empty()) return Vec3(1, 0, 1);

    // Tiling
    u = u - floor(u); v = v - floor(v);
    double px = u * tex.width - 0.5; double py = v * tex.height - 0.5;
    int x0 = (int)std::floor(px); int y0 = (int)std::floor(py);
    int x1 = x0 + 1; int y1 = y0 + 1;
    double dx = px - x0; double dy = py - y0;

    // Interpolação Bilinear nos valores já lineares
    Vec3 c00 = getPixel(tex, x0, y0); Vec3 c10 = getPixel(tex, x1, y0);
    Vec3 c01 = getPixel(tex, x0, y1); Vec3 c11 = getPixel(tex, x1, y1);
    Vec3 top = c00 * (1.0 - dx) + c10 * dx;
    Vec3 bot = c01 * (1.0 - dx) + c11 * dx;

    // Retorna direto (sem pow e sem multiplicação extra, pois já foi feito na carga)
    return top * (1.0 - dy) + bot * dy;
}

// Intersecção Global
inline bool getIntersection(const Ray& r, double& t, int& id, Vec3& normalHit, int& hitFaceIndex, double& hitU, double& hitV) {
    t = 1e20; id = 0; bool hit = false;
    hitFaceIndex = -1;

    if (g_renderMesh && g_renderMesh->bvhRoot) {
        const BVHNode* stack[64];
        int stackPtr = 0;
        stack[stackPtr++] = g_renderMesh->bvhRoot;
        while (stackPtr > 0) {
            const BVHNode* node = stack[--stackPtr];
            if (!node->box.intersect(r, t)) continue;
            if (node->triCount > 0) {
                for (int i = 0; i < node->triCount; ++i) {
                    int realIdx = g_renderMesh->triIndices[node->firstTriIndex + i];
                    const auto& face = g_renderMesh->faces[realIdx];
                    double u, v;
                    double d = intersectTriangle(r, g_renderMesh->vertices[face[0]], g_renderMesh->vertices[face[1]], g_renderMesh->vertices[face[2]], u, v);
                    if (d > 0 && d < t) {
                        t = d; id = 1; hit = true; hitFaceIndex = realIdx; hitU = u; hitV = v;
                        normalHit = (g_renderMesh->vertices[face[1]] - g_renderMesh->vertices[face[0]]).cross(g_renderMesh->vertices[face[2]] - g_renderMesh->vertices[face[0]]).norm();
                    }
                }
            } else {
                if (node->right) stack[stackPtr++] = node->right;
                if (node->left) stack[stackPtr++] = node->left;
            }
        }
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
    double det = b * b - op.dot(op) + 25.0; // r=5
    if (det > 0) {
        double t_luz = b - std::sqrt(det);
        if (t_luz > 1e-4 && t_luz < t) {
            t = t_luz; id = 3; hit = true; normalHit = (r.o + r.d * t - L).norm();
        }
    }
    return hit;
}

// ==========================================
// 6. RADIANCE COM NEE
// ==========================================
inline Vec3 radiance(Ray r, uint32_t& seed) {
    Vec3 throughput(1.0, 1.0, 1.0);
    Vec3 finalColor(0.0, 0.0, 0.0);
    Vec3 lightPos(10.0, 20.0, 10.0);
    double lightRadius = 5.0;
    Vec3 lightEmission(12.0, 12.0, 12.0);

    for (int depth = 0; depth < 5; ++depth) {
        double t; int id; Vec3 n;
        int hitFaceIdx; double u_bar, v_bar;

        if (!getIntersection(r, t, id, n, hitFaceIdx, u_bar, v_bar)) {
            return finalColor + throughput * Vec3(0.05, 0.05, 0.05);
        }

        if (id == 3) {
            if (depth == 0) return finalColor + throughput * lightEmission;
            else return finalColor;
        }

        Vec3 f;
        Vec3 x = r.o + r.d * t;
        Vec3 nl = n.dot(r.d) < 0 ? n : n * -1;

        if (id == 1) {
            f = Vec3(0.7, 0.7, 0.7);
            if (hitFaceIdx >= 0 && hitFaceIdx < (int)g_renderMesh->faceTextureID.size()) {
                int texID = g_renderMesh->faceTextureID[hitFaceIdx];
                if (texID >= 0 && texID < (int)g_renderMesh->textures.size()) {
                    const auto& uvs = g_renderMesh->faceUVs[hitFaceIdx];
                    if (uvs.size() >= 3) {
                        float interp_u = (1.0 - u_bar - v_bar) * uvs[0].u + u_bar * uvs[1].u + v_bar * uvs[2].u;
                        float interp_v = (1.0 - u_bar - v_bar) * uvs[0].v + u_bar * uvs[1].v + v_bar * uvs[2].v;
                        f = sampleTexture(g_renderMesh->textures[texID], interp_u, interp_v);
                    }
                }
            }
        }
        else {
            bool grid = (int(std::floor(x.x) + std::floor(x.z)) & 1) == 0;
            f = grid ? Vec3(0.8, 0.8, 0.8) : Vec3(0.2, 0.2, 0.2);
        }

        // --- NEXT EVENT ESTIMATION ---
        {
            Vec3 lightSample = lightPos + randomUnitVector(seed) * lightRadius;
            Vec3 toLight = lightSample - x;
            double distSq = toLight.dot(toLight);
            double dist = std::sqrt(distSq);
            Vec3 L_dir = toLight * (1.0 / dist);

            Ray shadowRay(x + nl * 1e-4, L_dir);
            double t_s; int id_s; Vec3 n_s; int fh_s; double u_s, v_s;

            bool visible = false;
            if (getIntersection(shadowRay, t_s, id_s, n_s, fh_s, u_s, v_s)) {
                if (id_s == 3 && t_s < dist + 0.1) visible = true;
            }

            if (visible) {
                double cosTheta = nl.dot(L_dir);
                if (cosTheta > 0) {
                    double area = 4.0 * 3.1415926 * lightRadius * lightRadius;
                    Vec3 directLight = lightEmission * f * cosTheta * (area / distSq);
                    finalColor = finalColor + throughput * directLight * 0.25;
                }
            }
        }

        double p = std::max({f.x, f.y, f.z});
        if (depth > 2) {
            if (random_float(seed) < p) f = f * (1.0 / p); else break;
        }
        throughput = throughput * f;

        double r1 = 2 * 3.14159 * random_float(seed);
        double r2 = random_float(seed);
        double r2s = std::sqrt(r2);
        Vec3 w = nl;
        Vec3 u = ((std::abs(w.x) > 0.1 ? Vec3(0, 1, 0) : Vec3(1, 0, 0)).cross(w)).norm();
        Vec3 v = w.cross(u);
        Vec3 d = (u * std::cos(r1) * r2s + v * std::sin(r1) * r2s + w * std::sqrt(1 - r2)).norm();
        r = Ray(x, d); r.o = r.o + r.d * 1e-4;
    }
    return finalColor;
}

// ACES Tone Mapping
inline double aces(double x) {
    const double a = 2.51f; const double b = 0.03f; const double c = 2.43f; const double d = 0.59f; const double e = 0.14f;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}
inline double clamp(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
inline int toInt(double x) {
    x = aces(x);
    return int(std::pow(clamp(x), 1.0/2.2) * 255.0 + 0.5);
}

inline void renderPathTracing(const std::vector<std::array<float, 3>>& vertices_in, const std::vector<std::vector<unsigned int>>& faces_in, const std::string& outputName) {}

#endif