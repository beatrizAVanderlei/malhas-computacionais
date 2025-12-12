#ifndef PATHTRACER_H
#define PATHTRACER_H

/*
 * ======================================================================================
 * PATH TRACER - DOCUMENTAÇÃO TÉCNICA E TEÓRICA
 * ======================================================================================
 *
 * Este software implementa um renderizador "unbiased" (não enviesado) baseado na
 * Equação de Renderização de Kajiya (1986). Abaixo estão as técnicas utilizadas:
 *
 * 1. INTEGRAÇÃO DE MONTE CARLO (Monte Carlo Integration):
 * - Teoria: A integral da luz que chega a um ponto é impossível de resolver analiticamente
 * para cenas complexas. Monte Carlo aproxima essa integral tirando a média de N amostras aleatórias.
 * - Convergência: O erro (ruído) diminui a uma taxa de O(1/sqrt(N)).
 *
 * 2. BVH (BOUNDING VOLUME HIERARCHY):
 * - O que é: Uma estrutura de dados espacial em árvore binária. Ela particiona os objetos
 * da cena em caixas (AABB - Axis Aligned Bounding Boxes) aninhadas.
 * - Por que usar: O custo de testar a interseção de um raio com a cena é linear O(N) sem
 * aceleração. Com BVH, o custo médio cai para O(log N).
 * - Implementação: Este código usa uma construção "Top-Down" baseada no ponto médio dos
 * centróides (Midpoint Split). Para a travessia, usa-se uma abordagem iterativa com
 * pilha (Stack) para evitar o overhead de chamadas recursivas e melhorar a coerência de cache.
 *
 * 3. NEXT EVENT ESTIMATION (NEE) / AMOSTRAGEM EXPLÍCITA DE LUZ:
 * - Problema: Em Path Tracing puro, encontrar uma pequena fonte de luz aleatoriamente é difícil,
 * gerando muito ruído (variância alta).
 * - Solução: Dividimos a integração em duas partes: Luz Direta (calculada explicitamente conectando
 * o ponto à luz) e Luz Indireta (calculada por rebatimento aleatório - BSDF sampling).
 * - Resultado: Convergência muito mais rápida para sombras e iluminação direta.
 *
 * 4. RUSSIAN ROULETTE (Roleta Russa):
 * - O que é: Uma técnica probabilística para terminar caminhos de raios.
 * - Funcionamento: Se uma superfície reflete 50% da luz, aleatoriamente terminamos o raio
 * com 50% de chance ou aumentamos sua energia (peso) nos outros 50%.
 * - Por que usar: Garante que o algoritmo termine (evita bounces infinitos) sem introduzir
 * viés (bias) no cálculo da energia total (conservação de energia).
 *
 * 5. ACES FILMIC TONE MAPPING:
 * - O que é: Uma curva de transferência eletro-óptica (EOTF) padronizada pela Academy Color
 * Encoding System.
 * - Por que usar: O Path Tracer gera valores de radiância espectral que podem variar de 0 a infinito (HDR).
 * Monitores só exibem 0 a 1 (LDR). O ACES comprime esses valores preservando a saturação
 * nas altas luzes (highlights) e o contraste nas sombras, simulando a resposta de película de filme.
 *
 * 6. TEXTURIZAÇÃO (Interpolação Bilinear + Correção Gama):
 * - Bilinear: Evita o aspecto "blocado" (nearest neighbor) ao ler texels que não alinham com pixels.
 * - Gama: Imagens (PNG/JPG) são armazenadas em espaço sRGB (Gama 2.2) para otimizar armazenamento.
 * O cálculo de luz DEVE ocorrer em espaço Linear. A conversão (pow 2.2) é matemática obrigatória.
 *
 * 7. PCG HASH (Permuted Congruential Generator):
 * - Por que usar: O `rand()` do C++ é lento e tem estado global (ruim para paralelismo).
 * O PCG Hash é "stateless" e extremamente rápido, ideal para gerar ruído branco em GPU/CPU paralela.
 *
 * ======================================================================================
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>
#include <fstream>
#include <cstdint>

// ==========================================
// 1. MATEMÁTICA E GERADOR DE NÚMEROS (PRNG)
// ==========================================

// Algoritmo PCG Hash.
// Transforma um estado inteiro em um número pseudo-aleatório através de bit-shifts e xor.
inline uint32_t hash_pcg(uint32_t& state) {
    uint32_t prev = state;
    state = state * 747796405u + 2891336453u;
    uint32_t word = ((prev >> ((prev >> 28u) + 4u)) ^ prev) * 277803737u;
    return (word >> 22u) ^ word;
}

// Helper para obter float [0.0, 1.0] a partir do hash.
// Multiplica pelo inverso de 2^32 (max uint32).
inline float random_float(uint32_t& seed) {
    return hash_pcg(seed) * (1.0f / 4294967296.0f);
}

// Estrutura para coordenadas de textura (U, V).
struct PtVec2 { float u, v; };

// Classe de Vetor 3D fundamental.
// Usada para Posição (x,y,z), Direção (x,y,z) e Cor (r,g,b).
struct Vec3 {
    double x, y, z;
    // Construtor padrão
    Vec3(double x_ = 0, double y_ = 0, double z_ = 0) : x(x_), y(y_), z(z_) {}

    // Sobrecarga de operadores para álgebra linear vetorial
    Vec3 operator+(const Vec3& b) const { return Vec3(x + b.x, y + b.y, z + b.z); }
    Vec3 operator-(const Vec3& b) const { return Vec3(x - b.x, y - b.y, z - b.z); }
    Vec3 operator*(double b) const { return Vec3(x * b, y * b, z * b); } // Escalar
    Vec3 operator*(const Vec3& b) const { return Vec3(x * b.x, y * b.y, z * b.z); } // Component-wise (Cor)

    // Normalização: Torna o vetor unitário (magnitude = 1). Essencial para direções.
    Vec3 norm() { return *this = *this * (1.0 / std::sqrt(x * x + y * y + z * z)); }

    // Produto Escalar (Dot Product): Mede o alinhamento entre dois vetores.
    // Dot > 0: mesma direção geral. Dot = 0: perpendiculares.
    double dot(const Vec3& b) const { return x * b.x + y * b.y + z * b.z; }

    // Produto Vetorial (Cross Product): Retorna vetor perpendicular a ambos.
    // Usado para calcular a normal de uma superfície definida por dois vetores tangentes.
    Vec3 cross(const Vec3& b) const { return Vec3(y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x); }

    double length() const { return std::sqrt(x*x + y*y + z*z); }
    double operator[](int i) const { return (&x)[i]; } // Acesso array-like
};

// Gera um ponto aleatório na superfície de uma esfera unitária.
// Usado para simular reflexão difusa (Lambertiana) e para escolher pontos na luz esférica.
inline Vec3 randomUnitVector(uint32_t& seed) {
    double z = random_float(seed) * 2.0 - 1.0;
    double a = random_float(seed) * 2.0 * 3.1415926;
    double r = std::sqrt(1.0 - z * z);
    return Vec3(r * std::cos(a), r * std::sin(a), z);
}

// Estrutura do Raio (Ray).
// Um raio é definido parametricamente como P(t) = Origem + t * Direção.
struct Ray {
    Vec3 o, d, inv_d;
    Ray(Vec3 o_, Vec3 d_) : o(o_), d(d_) {
        // Pré-cálculo do inverso da direção.
        // O algoritmo de interseção de caixa (Slab Method) usa (P - O) * (1/D).
        // Multiplicação é mais rápida que divisão em CPUs, por isso pré-calculamos.
        inv_d = Vec3(1.0 / (std::abs(d.x) > 1e-8 ? d.x : 1e-8),
                     1.0 / (std::abs(d.y) > 1e-8 ? d.y : 1e-8),
                     1.0 / (std::abs(d.z) > 1e-8 ? d.z : 1e-8));
    }
};

// Armazena a textura em memória.
// 'float' é usado para guardar valores de cor Linear (pós-gama) para acesso rápido.
struct TextureData {
    int width, height;
    std::vector<float> pixels;
};

// ==========================================
// 2. ESTRUTURAS DE ACELERAÇÃO (BVH)
// ==========================================

// AABB (Axis-Aligned Bounding Box).
// Volume mais simples possível para envolver geometria.
struct AABB {
    Vec3 min, max;
    // Inicia com infinito invertido para que qualquer ponto inserido expanda a caixa corretamente.
    AABB() { double inf = 1e20; min = Vec3(inf, inf, inf); max = Vec3(-inf, -inf, -inf); }

    void expand(const Vec3& p) {
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }

    // Algoritmo "Slab Method" otimizado para interseção Raio-AABB.
    // Verifica a interseção dos planos paralelos aos eixos X, Y e Z.
    // Se os intervalos de interseção se sobrepõem em todos os eixos, o raio acerta a caixa.
    inline bool intersect(const Ray& r, double t_max) const {
        double t1 = (min.x - r.o.x) * r.inv_d.x; double t2 = (max.x - r.o.x) * r.inv_d.x;
        double tmin = std::min(t1, t2), tmax = std::max(t1, t2); // Intervalo no eixo X

        t1 = (min.y - r.o.y) * r.inv_d.y; t2 = (max.y - r.o.y) * r.inv_d.y;
        tmin = std::max(tmin, std::min(t1, t2)); tmax = std::min(tmax, std::max(t1, t2)); // Intersecção X e Y

        t1 = (min.z - r.o.z) * r.inv_d.z; t2 = (max.z - r.o.z) * r.inv_d.z;
        tmin = std::max(tmin, std::min(t1, t2)); tmax = std::min(tmax, std::max(t1, t2)); // Intersecção final XYZ

        // Se tmax >= tmin, existe um intervalo válido de intersecção.
        return tmax >= tmin && tmin < t_max && tmax > 0;
    }
};

// Nó da árvore. Pode ser um nó interno (aponta para filhos) ou folha (aponta para triângulos).
struct BVHNode {
    AABB box;
    BVHNode *left = nullptr, *right = nullptr;
    int firstTriIndex = -1, triCount = 0; // Se triCount > 0, é uma folha contendo geometria.
};

// Contêiner principal dos dados da cena.
struct SceneData {
    std::vector<Vec3> vertices;
    std::vector<std::vector<unsigned int>> faces;

    // Lista de índices indireta. A BVH reordena este vetor, não os dados originais.
    // Isso é mais eficiente para cache e memória.
    std::vector<int> triIndices;

    std::vector<TextureData> textures;
    std::vector<int> faceTextureID;
    std::vector<std::vector<PtVec2>> faceUVs;

    BVHNode* bvhRoot = nullptr;
    ~SceneData() { clearTree(bvhRoot); }
    void clearTree(BVHNode* node) { if (!node) return; clearTree(node->left); clearTree(node->right); delete node; }
};

extern SceneData* g_renderMesh;

// --- Construção da BVH ---
// Calcula o centróide de um triângulo (média dos vértices). Usado para decidir em qual lado da caixa o triângulo fica.
inline Vec3 getCentroid(const SceneData& scene, int triIdx) {
    const auto& f = scene.faces[triIdx];
    return (scene.vertices[f[0]] + scene.vertices[f[1]] + scene.vertices[f[2]]) * 0.333333;
}

// Construtor Recursivo da Árvore
inline BVHNode* buildBVHRecursive(SceneData& scene, int left, int right) {
    BVHNode* node = new BVHNode();

    // 1. Calcula a AABB que envolve todos os triângulos deste nó
    for (int i = left; i < right; ++i) {
        int idx = scene.triIndices[i];
        const auto& f = scene.faces[idx];
        node->box.expand(scene.vertices[f[0]]);
        node->box.expand(scene.vertices[f[1]]);
        node->box.expand(scene.vertices[f[2]]);
    }

    int count = right - left;
    // Critério de parada: Se o nó tem poucos triângulos, vira folha.
    if (count <= 2) { node->firstTriIndex = left; node->triCount = count; return node; }

    // 2. Heurística de Divisão (Split): Ponto Médio no Maior Eixo.
    // Encontra o eixo (X, Y ou Z) onde a caixa é mais comprida.
    Vec3 size = node->box.max - node->box.min;
    int axis = (size.x > size.y) ? (size.x > size.z ? 0 : 2) : (size.y > size.z ? 1 : 2);
    double split = node->box.min[axis] + size[axis] * 0.5;

    // 3. Particionamento (Algoritmo similar ao Quicksort)
    // Move triângulos para a esquerda ou direita do array baseado na posição do centróide.
    int mid = left;
    for (int i = left; i < right; ++i) {
        if (getCentroid(scene, scene.triIndices[i])[axis] < split)
            std::swap(scene.triIndices[i], scene.triIndices[mid++]);
    }

    // Proteção contra loops infinitos (se todos os centróides estiverem no mesmo ponto)
    if (mid == left || mid == right) mid = left + count/2;

    // 4. Constrói filhos recursivamente
    node->left = buildBVHRecursive(scene, left, mid);
    node->right = buildBVHRecursive(scene, mid, right);
    return node;
}

inline void buildBVH(SceneData& scene) {
    if (scene.faces.empty()) return;
    scene.triIndices.resize(scene.faces.size());
    for(size_t i=0; i<scene.faces.size(); ++i) scene.triIndices[i] = i; // Inicializa índices
    scene.bvhRoot = buildBVHRecursive(scene, 0, scene.faces.size());
}

// ==========================================
// 4. INTERSEÇÃO (Möller–Trumbore)
// ==========================================
// Algoritmo padrão da indústria para interseção Raio-Triângulo.
// É rápido porque não requer pré-cálculo da equação do plano do triângulo.
// Retorna 't' (distância) e coordenadas baricêntricas (u, v) se houver colisão.
inline double intersectTriangle(const Ray& r, const Vec3& v0, const Vec3& v1, const Vec3& v2, double& outU, double& outV) {
    const double EPS = 1e-6; // Tolerância para evitar erros de ponto flutuante
    Vec3 e1 = v1 - v0; Vec3 e2 = v2 - v0;
    Vec3 h = r.d.cross(e2);
    double a = e1.dot(h);

    if (a > -EPS && a < EPS) return 0; // Raio paralelo ao triângulo
    double f = 1.0 / a;
    Vec3 s = r.o - v0;
    outU = f * s.dot(h);
    if (outU < 0.0 || outU > 1.0) return 0; // Fora da aresta 1
    Vec3 q = s.cross(e1);
    outV = f * r.d.dot(q);
    if (outV < 0.0 || outU + outV > 1.0) return 0; // Fora da aresta 2
    double t = f * e2.dot(q);
    return (t > EPS) ? t : 0; // Retorna distância se positiva (frente da câmera)
}

// ==========================================
// 5. AMOSTRAGEM DE TEXTURA
// ==========================================
// Acesso seguro ao array de pixels (Clamp to Edge)
inline Vec3 getPixel(const TextureData& tex, int x, int y) {
    x = std::max(0, std::min(x, tex.width - 1));
    y = std::max(0, std::min(y, tex.height - 1));
    int idx = (y * tex.width + x) * 3;
    // Retorna o valor direto (já em float e espaço linear, graças ao pré-processamento no controls.cpp)
    return Vec3(tex.pixels[idx], tex.pixels[idx+1], tex.pixels[idx+2]);
}

// Amostragem com Interpolação Bilinear.
// Pega os 4 pixels vizinhos e calcula uma média ponderada baseada na posição fracionária.
// Isso suaviza a textura quando vista de muito perto.
inline Vec3 sampleTexture(const TextureData& tex, double u, double v) {
    if (tex.pixels.empty()) return Vec3(1, 0, 1);

    // Tiling: Garante que texturas se repitam se U/V passarem de 1.0
    u = u - floor(u); v = v - floor(v);

    // Converte UV (0..1) para coordenadas de pixel
    double px = u * tex.width - 0.5; double py = v * tex.height - 0.5;
    int x0 = (int)std::floor(px); int y0 = (int)std::floor(py);
    int x1 = x0 + 1; int y1 = y0 + 1;
    double dx = px - x0; double dy = py - y0;

    // Fetch dos 4 vizinhos
    Vec3 c00 = getPixel(tex, x0, y0); Vec3 c10 = getPixel(tex, x1, y0);
    Vec3 c01 = getPixel(tex, x0, y1); Vec3 c11 = getPixel(tex, x1, y1);

    // Interpolação em X depois em Y
    Vec3 top = c00 * (1.0 - dx) + c10 * dx;
    Vec3 bot = c01 * (1.0 - dx) + c11 * dx;
    return top * (1.0 - dy) + bot * dy;
}

// Função Principal de Intersecção da Cena (Traversal).
// Substitui a recursão por uma Pilha (Stack) para performance.
inline bool getIntersection(const Ray& r, double& t, int& id, Vec3& normalHit, int& hitFaceIndex, double& hitU, double& hitV) {
    t = 1e20; id = 0; bool hit = false;
    hitFaceIndex = -1;

    // 1. Intersecção com a Malha (Usando BVH)
    if (g_renderMesh && g_renderMesh->bvhRoot) {
        const BVHNode* stack[64]; // Pilha estática (profundidade 64 é mais que suficiente)
        int stackPtr = 0;
        stack[stackPtr++] = g_renderMesh->bvhRoot;

        while (stackPtr > 0) {
            const BVHNode* node = stack[--stackPtr];

            // Otimização Fundamental: Se o raio não toca a caixa (AABB), ignoramos tudo dentro dela.
            if (!node->box.intersect(r, t)) continue;

            if (node->triCount > 0) { // Nó Folha: Testa os triângulos reais
                for (int i = 0; i < node->triCount; ++i) {
                    int realIdx = g_renderMesh->triIndices[node->firstTriIndex + i];
                    const auto& face = g_renderMesh->faces[realIdx];
                    double u, v;
                    // Teste exato Möller–Trumbore
                    double d = intersectTriangle(r, g_renderMesh->vertices[face[0]], g_renderMesh->vertices[face[1]], g_renderMesh->vertices[face[2]], u, v);
                    if (d > 0 && d < t) { // Encontrou interseção mais próxima
                        t = d; id = 1; hit = true; hitFaceIndex = realIdx; hitU = u; hitV = v;
                        // Calcula normal geométrica
                        normalHit = (g_renderMesh->vertices[face[1]] - g_renderMesh->vertices[face[0]]).cross(g_renderMesh->vertices[face[2]] - g_renderMesh->vertices[face[0]]).norm();
                    }
                }
            } else { // Nó Interno: Empilha os filhos para processar depois
                if (node->right) stack[stackPtr++] = node->right;
                if (node->left) stack[stackPtr++] = node->left;
            }
        }
    }

    // 2. Intersecção com Plano Infinito (Chão)
    if (std::abs(r.d.y) > 1e-6) {
        double t_plane = (-1.2 - r.o.y) * r.inv_d.y;
        if (t_plane > 1e-4 && t_plane < t) {
            t = t_plane; id = 2; hit = true; normalHit = Vec3(0, 1, 0);
        }
    }

    // 3. Intersecção com Luz Esférica (Analítico)
    Vec3 L(10.0, 20.0, 10.0);
    Vec3 op = L - r.o;
    double b = op.dot(r.d);
    double det = b * b - op.dot(op) + 25.0; // r^2 = 5^2 = 25
    if (det > 0) {
        double t_luz = b - std::sqrt(det);
        if (t_luz > 1e-4 && t_luz < t) {
            t = t_luz; id = 3; hit = true; normalHit = (r.o + r.d * t - L).norm();
        }
    }
    return hit;
}

// ==========================================
// 6. FUNÇÃO RADIANCE (Motor de Renderização)
// ==========================================
inline Vec3 radiance(Ray r, uint32_t& seed) {
    Vec3 throughput(1.0, 1.0, 1.0); // Fator de atenuação da cor (Albedo acumulado)
    Vec3 finalColor(0.0, 0.0, 0.0); // Luz total coletada

    // Parâmetros da Luz
    Vec3 lightPos(10.0, 20.0, 10.0);
    double lightRadius = 5.0;
    Vec3 lightEmission(12.0, 12.0, 12.0); // Luz HDR (Intensidade > 1)

    // Loop de Rebatimento (Path Tracing Iterativo)
    for (int depth = 0; depth < 5; ++depth) {
        double t; int id; Vec3 n;
        int hitFaceIdx; double u_bar, v_bar;

        // 1. Encontra a superfície mais próxima
        if (!getIntersection(r, t, id, n, hitFaceIdx, u_bar, v_bar)) {
            return finalColor + throughput * Vec3(0.05, 0.05, 0.05); // Luz ambiente (Céu escuro)
        }

        // 2. Se acertar a luz diretamente
        if (id == 3) {
            // Se for o primeiro raio (visão direta), desenha a luz.
            // Se for um raio rebatido, ignoramos (retorna 0) porque o NEE (abaixo) já calculou essa luz.
            // Isso evita contar a iluminação duas vezes.
            if (depth == 0) return finalColor + throughput * lightEmission;
            else return finalColor;
        }

        // 3. Define Material (Albedo)
        Vec3 f;
        Vec3 x = r.o + r.d * t;
        Vec3 nl = n.dot(r.d) < 0 ? n : n * -1; // Normal orientada para fora

        if (id == 1) { // Objeto 3D
            f = Vec3(0.7, 0.7, 0.7); // Cor base

            // Se houver textura, calcula UV interpolado e amostra a imagem
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
        else { // Chão Procedural (Xadrez)
            bool grid = (int(std::floor(x.x) + std::floor(x.z)) & 1) == 0;
            f = grid ? Vec3(0.8, 0.8, 0.8) : Vec3(0.2, 0.2, 0.2);
        }

        // 4. NEXT EVENT ESTIMATION (NEE) - O Segredo da Performance
        // Em vez de esperar o raio bater na luz aleatoriamente, conectamos explicitamente o ponto à luz.
        {
            Vec3 directLightSum(0, 0, 0);
            int shadowSamples = 2; // Múltiplas amostras para sombra suave (penumbra) de alta qualidade

            for(int s=0; s<shadowSamples; ++s) {
                // Sorteia ponto na luz esférica
                Vec3 lightSample = lightPos + randomUnitVector(seed) * lightRadius;
                Vec3 toLight = lightSample - x;
                double distSq = toLight.dot(toLight);
                double dist = std::sqrt(distSq);
                Vec3 L_dir = toLight * (1.0 / dist);

                // Dispara raio de sombra (Shadow Ray) em direção à luz
                Ray shadowRay(x + nl * 1e-4, L_dir);
                double t_s; int id_s; Vec3 n_s; int fh_s; double u_s, v_s;

                // Verifica visibilidade (Se bater na luz antes de qualquer obstáculo)
                bool visible = false;
                if (getIntersection(shadowRay, t_s, id_s, n_s, fh_s, u_s, v_s)) {
                    if (id_s == 3 && t_s < dist + 0.1) visible = true;
                }

                if (visible) {
                    double cosTheta = nl.dot(L_dir); // Lei de Lambert
                    if (cosTheta > 0) {
                        double area = 4.0 * 3.1415926 * lightRadius * lightRadius; // Área da esfera
                        // Geometria da luz: Intensidade decai com o quadrado da distância
                        double geometryTerm = cosTheta * (area / distSq);

                        // CLAMPING: Limita brilho excessivo para evitar "Fireflies" (pixels brancos estourados)
                        if (geometryTerm > 10.0) geometryTerm = 10.0;

                        directLightSum = directLightSum + lightEmission * f * geometryTerm;
                    }
                }
            }
            finalColor = finalColor + throughput * directLightSum * (1.0 / shadowSamples);
        }

        // 5. ROLETA RUSSA (Terminação Probabilística)
        // Decide se o raio morre ou continua baseado na refletividade (p) da superfície.
        // Superfícies escuras (p baixo) têm alta chance de absorver o raio.
        double p = std::max({f.x, f.y, f.z});
        if (depth > 2) { // Só aplica após 2 rebatidas para garantir qualidade mínima
            if (random_float(seed) < p) f = f * (1.0 / p); // Compensa energia se sobreviver
            else break; // Raio absorvido
        }
        throughput = throughput * f; // Acumula a cor da superfície

        // 6. LUZ INDIRETA (Rebatimento Difuso)
        // Escolhe uma direção aleatória no hemisfério orientado pela normal (Cosine Weighted Sampling)
        double r1 = 2 * 3.14159 * random_float(seed);
        double r2 = random_float(seed);
        double r2s = std::sqrt(r2);
        Vec3 w = nl;
        Vec3 u = ((std::abs(w.x) > 0.1 ? Vec3(0, 1, 0) : Vec3(1, 0, 0)).cross(w)).norm();
        Vec3 v = w.cross(u);
        Vec3 d = (u * std::cos(r1) * r2s + v * std::sin(r1) * r2s + w * std::sqrt(1 - r2)).norm();

        // Configura o raio para o próximo loop (rebatimento)
        r = Ray(x, d);
        r.o = r.o + r.d * 1e-4; // Offset epsilon para evitar auto-interseção
    }
    return finalColor;
}

// ==========================================
// 7. TONE MAPPING (ACES FILMIC)
// ==========================================
// Aplica a curva "S" característica de filmes para comprimir o alcance dinâmico.
inline double aces(double x) {
    const double a = 2.51f; const double b = 0.03f; const double c = 2.43f; const double d = 0.59f; const double e = 0.14f;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}
inline double clamp(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

inline int toInt(double x) {
    x = aces(x); // 1. Tone Mapping (HDR -> 0..1 com curva fílmica)
    return int(std::pow(clamp(x), 1.0/2.2) * 255.0 + 0.5); // 2. Gamma Correction (Linear -> Monitor sRGB)
}

inline void renderPathTracing(const std::vector<std::array<float, 3>>& vertices_in, const std::vector<std::vector<unsigned int>>& faces_in, const std::string& outputName) {}

#endif