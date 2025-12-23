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
 * 8. FLUXO DE EXECUÇÃO E JUSTIFICATIVA ARQUITETURAL (PIPELINE)
 * ======================================================================================
 * * Como essas técnicas se conectam para formar a imagem final:
 *
 * PASSO A: GERAÇÃO (Main Loop)
 * - O `main.cpp` varre cada pixel. Para evitar padrões visuais repetitivos, usamos o
 * `PCG Hash` (Técnica 7) para gerar uma semente única por pixel/frame.
 * - Isso alimenta a `Integração de Monte Carlo` (Técnica 1), permitindo que a imagem convirja
 * progressivamente de "ruidosa" para "limpa".
 *
 * PASSO B: TRAVESSIA (Intersection)
 * - O raio gerado precisa encontrar a geometria. Sem otimização, testaríamos milhões de triângulos.
 * - A `BVH` (Técnica 2) entra aqui: reduzimos o teste para algumas dezenas de caixas.
 * - Ao atingir um triângulo, usamos coordenadas baricêntricas para interpolar a `Texturização` (Técnica 6),
 * garantindo que a cor (albedo) esteja no espaço linear correto para os cálculos de física.
 *
 * PASSO C: ILUMINAÇÃO (Shading)
 * - Ao bater em um ponto, temos duas fontes de luz possíveis:
 * 1. Luz Direta: Usamos `NEE` (Técnica 3) para conectar o ponto diretamente à lâmpada. Isso
 * resolve as sombras duras e a iluminação principal instantaneamente.
 * 2. Luz Indireta: O raio "rebate" aleatoriamente (Monte Carlo) para capturar cores de paredes
 * vizinhas (Color Bleeding).
 *
 * PASSO D: TERMINAÇÃO E OTIMIZAÇÃO
 * - Para evitar que o raio rebata para sempre (loop infinito) ou termine cedo demais (imagem escura),
 * usamos a `Russian Roulette` (Técnica 4). Ela mata raios que contribuem pouco para a imagem
 * de forma estatisticamente justa.
 *
 * PASSO E: EXIBIÇÃO (Post-Processing)
 * - O resultado acumulado é um valor HDR (High Dynamic Range), ex: (R=50.0, G=20.0, B=10.0).
 * - O monitor não exibe 50.0. Usamos o `ACES Tone Mapping` (Técnica 5) para converter isso
 * elegantemente para o intervalo 0.0-1.0, preservando detalhes nas áreas estouradas de luz.
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

// Algoritmo PCG Hash (Permuted Congruential Generator).
// Objetivo: Gerar números pseudo-aleatórios rápidos e de alta qualidade.
// Parâmetros: 'state' é a semente (seed) que muda a cada chamada para gerar o próximo número.
inline uint32_t hash_pcg(uint32_t& state) {
    uint32_t prev = state;
    // Multiplicador e incremento mágicos para embaralhar os bits
    state = state * 747796405u + 2891336453u;
    // Xorshift e rotação para garantir distribuição uniforme
    uint32_t word = ((prev >> ((prev >> 28u) + 4u)) ^ prev) * 277803737u;
    return (word >> 22u) ^ word;
}

// Helper para obter um float entre [0.0, 1.0] a partir do hash inteiro.
// Divide o resultado do hash pelo valor máximo possível de um uint32 (2^32).
inline float random_float(uint32_t& seed) {
    return hash_pcg(seed) * (1.0f / 4294967296.0f);
}

// Estrutura simples para coordenadas de textura 2D (U, V).
struct PtVec2 { float u, v; };

// Classe fundamental de Vetor 3D (x, y, z).
// Usada para Posição, Direção e Cor (R, G, B).
struct Vec3 {
    double x, y, z;
    // Construtor: Inicializa com valores padrão ou fornecidos.
    Vec3(double x_ = 0, double y_ = 0, double z_ = 0) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& b) const { return Vec3(x + b.x, y + b.y, z + b.z); }
    Vec3 operator-(const Vec3& b) const { return Vec3(x - b.x, y - b.y, z - b.z); }
    Vec3 operator*(double b) const { return Vec3(x * b, y * b, z * b); }
    Vec3 operator*(const Vec3& b) const { return Vec3(x * b.x, y * b.y, z * b.z); }

    // Normalização
    Vec3 norm() { return *this = *this * (1.0 / std::sqrt(x * x + y * y + z * z)); }

    // Produto Escalar
    double dot(const Vec3& b) const { return x * b.x + y * b.y + z * b.z; }

    // Produto Vetorial
    Vec3 cross(const Vec3& b) const { return Vec3(y * b.z - z * b.y, z * b.x - x * b.z, x * b.y - y * b.x); }

    // Magnitude
    double length() const { return std::sqrt(x*x + y*y + z*z); }
    // Acesso estilo array v[0], v[1], v[2] para loops genéricos.
    double operator[](int i) const { return (&x)[i]; }
};

// Gera um vetor unitário aleatório uniformemente distribuído na superfície de uma esfera.
// Usado para simular reflexão difusa (espalha a luz em todas as direções) e amostragem de luz de área.
inline Vec3 randomUnitVector(uint32_t& seed) {
    double z = random_float(seed) * 2.0 - 1.0; // Z aleatório entre -1 e 1
    double a = random_float(seed) * 2.0 * 3.1415926; // Ângulo aleatório (0 a 2*PI)
    double r = std::sqrt(1.0 - z * z); // Raio no plano XY
    return Vec3(r * std::cos(a), r * std::sin(a), z); // Coordenadas esféricas -> Cartesianas
}

// Estrutura do Raio de Luz.
// Um raio é definido por uma Origem (o) e uma Direção (d).
struct Ray {
    Vec3 o, d, inv_d;
    Ray(Vec3 o_, Vec3 d_) : o(o_), d(d_) {
        // Pré-calcula o inverso da direção (1/d).
        // Isso acelera muito a interseção Raio-AABB (Slab Method), trocando divisões por multiplicações.
        // Adiciona um epsilon (1e-8) para evitar divisão por zero se o raio for paralelo a um eixo.
        inv_d = Vec3(1.0 / (std::abs(d.x) > 1e-8 ? d.x : 1e-8),
                     1.0 / (std::abs(d.y) > 1e-8 ? d.y : 1e-8),
                     1.0 / (std::abs(d.z) > 1e-8 ? d.z : 1e-8));
    }
};

// Armazena dados de textura em memória.
// Usa 'float' para guardar cores lineares (pós-gama) e permitir interpolação suave.
struct TextureData {
    int width, height;
    std::vector<float> pixels;
};

// ==========================================
// 2. ESTRUTURAS DE ACELERAÇÃO (BVH)
// ==========================================

// AABB (Axis-Aligned Bounding Box).
// Uma caixa retangular alinhada aos eixos XYZ que envolve um grupo de triângulos.
// Usada para descartar rapidamente raios que passam longe da geometria.
struct AABB {
    Vec3 min, max;
    // Inicializa com "infinito invertido" para garantir que o primeiro ponto expanda a caixa corretamente.
    AABB() { double inf = 1e20; min = Vec3(inf, inf, inf); max = Vec3(-inf, -inf, -inf); }

    // Expande a caixa para incluir o ponto p.
    void expand(const Vec3& p) {
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }

    // Teste de interseção Raio vs Caixa (Slab Method).
    // Verifica se o raio entra e sai da caixa em intervalos consistentes nos eixos X, Y e Z.
    // Retorna true se houver sobreposição válida dentro da distância t_max.
    inline bool intersect(const Ray& r, double t_max) const {
        double t1 = (min.x - r.o.x) * r.inv_d.x; double t2 = (max.x - r.o.x) * r.inv_d.x;
        double tmin = std::min(t1, t2), tmax = std::max(t1, t2); // Intervalo no eixo X

        t1 = (min.y - r.o.y) * r.inv_d.y; t2 = (max.y - r.o.y) * r.inv_d.y;
        tmin = std::max(tmin, std::min(t1, t2)); tmax = std::min(tmax, std::max(t1, t2)); // Intersecção com Y

        t1 = (min.z - r.o.z) * r.inv_d.z; t2 = (max.z - r.o.z) * r.inv_d.z;
        tmin = std::max(tmin, std::min(t1, t2)); tmax = std::min(tmax, std::max(t1, t2)); // Intersecção com Z

        return tmax >= tmin && tmin < t_max && tmax > 0;
    }
};

// Nó da árvore BVH.
struct BVHNode {
    AABB box; // Caixa que envolve tudo abaixo deste nó
    BVHNode *left = nullptr, *right = nullptr; // Filhos da esquerda e direita
    int firstTriIndex = -1, triCount = 0; // Se for folha, aponta para onde começam os triângulos
};

// Contêiner principal da cena para o Ray Tracer.
// Mantém cópias otimizadas dos dados para acesso rápido e thread-safe.
struct SceneData {
    std::vector<Vec3> vertices;
    std::vector<std::vector<unsigned int>> faces;
    std::vector<int> triIndices; // Índices reordenados pela BVH para acesso coerente

    std::vector<TextureData> textures;
    std::vector<int> faceTextureID;
    std::vector<std::vector<PtVec2>> faceUVs;

    BVHNode* bvhRoot = nullptr; // Raiz da árvore de aceleração
    ~SceneData() { clearTree(bvhRoot); } // Destrutor limpa a árvore
    void clearTree(BVHNode* node) { if (!node) return; clearTree(node->left); clearTree(node->right); delete node; }
};

extern SceneData* g_renderMesh; // Variável global apontando para a cena atual

// --- Construção da BVH ---
// Calcula o centro geométrico de um triângulo. Usado para decidir em qual filho da BVH ele vai.
inline Vec3 getCentroid(const SceneData& scene, int triIdx) {
    const auto& f = scene.faces[triIdx];
    return (scene.vertices[f[0]] + scene.vertices[f[1]] + scene.vertices[f[2]]) * 0.333333;
}

// Construtor recursivo da BVH (Top-Down).
inline BVHNode* buildBVHRecursive(SceneData& scene, int left, int right) {
    BVHNode* node = new BVHNode();

    // 1. Calcula a AABB deste nó varrendo todos os triângulos que ele contém
    for (int i = left; i < right; ++i) {
        int idx = scene.triIndices[i];
        const auto& f = scene.faces[idx];
        node->box.expand(scene.vertices[f[0]]);
        node->box.expand(scene.vertices[f[1]]);
        node->box.expand(scene.vertices[f[2]]);
    }

    int count = right - left;
    // Critério de parada: Se tiver 2 ou menos triângulos, vira folha.
    if (count <= 2) { node->firstTriIndex = left; node->triCount = count; return node; }

    // 2. Escolhe o maior eixo da caixa para dividir (Heurística espacial simples)
    Vec3 size = node->box.max - node->box.min;
    int axis = (size.x > size.y) ? (size.x > size.z ? 0 : 2) : (size.y > size.z ? 1 : 2);
    double split = node->box.min[axis] + size[axis] * 0.5; // Ponto médio

    // 3. Particiona os triângulos (QuickSelect): Joga os menores para a esquerda, maiores para a direita
    int mid = left;
    for (int i = left; i < right; ++i) {
        if (getCentroid(scene, scene.triIndices[i])[axis] < split)
            std::swap(scene.triIndices[i], scene.triIndices[mid++]);
    }

    // Proteção: Garante que não criamos partições vazias infinitas
    if (mid == left || mid == right) mid = left + count/2;

    // 4. Recursão para criar filhos
    node->left = buildBVHRecursive(scene, left, mid);
    node->right = buildBVHRecursive(scene, mid, right);
    return node;
}

// Função de entrada para construir a BVH
inline void buildBVH(SceneData& scene) {
    if (scene.faces.empty()) return;
    scene.triIndices.resize(scene.faces.size());
    for(size_t i=0; i<scene.faces.size(); ++i) scene.triIndices[i] = i; // Inicializa índices sequenciais
    scene.bvhRoot = buildBVHRecursive(scene, 0, scene.faces.size());
}

// ==========================================
// 4. INTERSEÇÃO (Möller–Trumbore)
// ==========================================
// Algoritmo rápido para testar interseção Raio vs Triângulo.
// Retorna a distância 't' e as coordenadas baricêntricas (u, v) para interpolação de textura.
inline double intersectTriangle(const Ray& r, const Vec3& v0, const Vec3& v1, const Vec3& v2, double& outU, double& outV) {
    const double EPS = 1e-6; // Tolerância para erros de ponto flutuante
    Vec3 e1 = v1 - v0; Vec3 e2 = v2 - v0; // Arestas do triângulo
    Vec3 h = r.d.cross(e2);
    double a = e1.dot(h);

    if (a > -EPS && a < EPS) return 0; // Raio paralelo ao triângulo
    double f = 1.0 / a;
    Vec3 s = r.o - v0;
    outU = f * s.dot(h);
    if (outU < 0.0 || outU > 1.0) return 0; // Raio passou fora da aresta 1
    Vec3 q = s.cross(e1);
    outV = f * r.d.dot(q);
    if (outV < 0.0 || outU + outV > 1.0) return 0; // Raio passou fora da aresta 2
    double t = f * e2.dot(q); // Distância da colisão
    return (t > EPS) ? t : 0; // Retorna t apenas se estiver à frente da câmera
}

// ==========================================
// 5. AMOSTRAGEM DE TEXTURA
// ==========================================
// Acesso seguro ao pixel da textura (Clamp to Edge)
inline Vec3 getPixel(const TextureData& tex, int x, int y) {
    x = std::max(0, std::min(x, tex.width - 1));
    y = std::max(0, std::min(y, tex.height - 1));
    int idx = (y * tex.width + x) * 3;
    return Vec3(tex.pixels[idx], tex.pixels[idx+1], tex.pixels[idx+2]);
}

// Amostragem com Interpolação Bilinear.
// Lê os 4 pixels vizinhos e mistura suavemente para evitar o aspecto "pixelado" (blocky).
inline Vec3 sampleTexture(const TextureData& tex, double u, double v) {
    if (tex.pixels.empty()) return Vec3(1, 0, 1);

    // Tiling: Faz a textura repetir se coordenadas passarem de 1.0
    u = u - floor(u); v = v - floor(v);

    // Coordenadas em espaço de pixel
    double px = u * tex.width - 0.5; double py = v * tex.height - 0.5;
    int x0 = (int)std::floor(px); int y0 = (int)std::floor(py);
    int x1 = x0 + 1; int y1 = y0 + 1;

    // Pesos para interpolação
    double dx = px - x0; double dy = py - y0;

    // Leitura dos vizinhos
    Vec3 c00 = getPixel(tex, x0, y0); Vec3 c10 = getPixel(tex, x1, y0);
    Vec3 c01 = getPixel(tex, x0, y1); Vec3 c11 = getPixel(tex, x1, y1);

    // Interpolação linear nos dois eixos
    Vec3 top = c00 * (1.0 - dx) + c10 * dx;
    Vec3 bot = c01 * (1.0 - dx) + c11 * dx;
    return top * (1.0 - dy) + bot * dy;
}

// Função Principal de Intersecção (Scene Traversal).
// Percorre a BVH e testa objetos da cena para encontrar a colisão mais próxima.
inline bool getIntersection(const Ray& r, double& t, int& id, Vec3& normalHit, int& hitFaceIndex, double& hitU, double& hitV) {
    t = 1e20; id = 0; bool hit = false; // Inicializa com "nada encontrado"
    hitFaceIndex = -1;

    // 1. Testa Malha (BVH)
    if (g_renderMesh && g_renderMesh->bvhRoot) {
        const BVHNode* stack[64]; // Pilha para evitar recursão lenta
        int stackPtr = 0;
        stack[stackPtr++] = g_renderMesh->bvhRoot;

        while (stackPtr > 0) {
            const BVHNode* node = stack[--stackPtr];

            // OTIMIZAÇÃO: Se raio não toca a caixa, ignora tudo dentro (Culling)
            if (!node->box.intersect(r, t)) continue;

            if (node->triCount > 0) { // Nó Folha (tem geometria real)
                for (int i = 0; i < node->triCount; ++i) {
                    int realIdx = g_renderMesh->triIndices[node->firstTriIndex + i];
                    const auto& face = g_renderMesh->faces[realIdx];
                    double u, v;
                    // Teste exato com triângulo
                    double d = intersectTriangle(r, g_renderMesh->vertices[face[0]], g_renderMesh->vertices[face[1]], g_renderMesh->vertices[face[2]], u, v);

                    if (d > 0 && d < t) { // Se achou colisão mais próxima
                        t = d; id = 1; hit = true; hitFaceIndex = realIdx; hitU = u; hitV = v;
                        // Calcula normal geométrica (Cross product das arestas)
                        normalHit = (g_renderMesh->vertices[face[1]] - g_renderMesh->vertices[face[0]]).cross(g_renderMesh->vertices[face[2]] - g_renderMesh->vertices[face[0]]).norm();
                    }
                }
            } else { // Nó Interno: Continua descendo na árvore
                if (node->right) stack[stackPtr++] = node->right;
                if (node->left) stack[stackPtr++] = node->left;
            }
        }
    }

    // 2. Testa Chão Infinito (Procedural)
    if (std::abs(r.d.y) > 1e-6) {
        double t_plane = (-1.2 - r.o.y) * r.inv_d.y;
        if (t_plane > 1e-4 && t_plane < t) {
            t = t_plane; id = 2; hit = true; normalHit = Vec3(0, 1, 0); // Normal pra cima
        }
    }

    // 3. Testa Luz Esférica (Geometria Analítica)
    Vec3 L(0.0, 0.6, 0.0); // Posição da luz
    Vec3 op = L - r.o;
    double b = op.dot(r.d);
    double det = b * b - op.dot(op) + 0.01; // r^2 = 25
    if (det > 0) {
        double t_luz = b - std::sqrt(det);
        if (t_luz > 1e-4 && t_luz < t) {
            t = t_luz; id = 3; hit = true; normalHit = (r.o + r.d * t - L).norm();
        }
    }
    return hit;
}

// ==========================================
// 6. FUNÇÃO RADIANCE (Cálculo de Luz)
// ==========================================
inline Vec3 radiance(Ray r, uint32_t& seed) {
    Vec3 throughput(1.0, 1.0, 1.0); // Carrega a "cor" do caminho (Albedo acumulado)
    Vec3 finalColor(0.0, 0.0, 0.0); // Luz total que chega à câmera

    // Configuração da Luz Esférica
    Vec3 lightPos(0.0, 0.6, 0.0);
    double lightRadius = 0.04;

    // Intensidade da luz reduzida para 8.0 para evitar estouro (burnout)
    Vec3 lightEmission(8.0, 8.0, 8.0);

    // Loop de Rebatimento (Bounces)
    // Simula o caminho do fóton inversamente (Câmera -> Luz)
    for (int depth = 0; depth < 5; ++depth) {
        double t; int id; Vec3 n;
        int hitFaceIdx; double u_bar, v_bar;

        // Se o raio for para o infinito (Céu)
        if (!getIntersection(r, t, id, n, hitFaceIdx, u_bar, v_bar)) {
            return finalColor + throughput * Vec3(0.05, 0.05, 0.05); // Luz ambiente fraca
        }

        // Se o raio bater na fonte de luz (Lâmpada)
        if (id == 3) {
            // Só adiciona a emissão se for visão direta (depth 0).
            // Se for luz indireta (depth > 0), o NEE já calculou isso, então ignoramos para não duplicar.
            if (depth == 0) return finalColor + throughput * lightEmission;
            else return finalColor;
        }

        // Define propriedades do material (Ponto de impacto)
        Vec3 f; // Cor da superfície (Albedo)
        Vec3 x = r.o + r.d * t; // Ponto exato da colisão 3D
        Vec3 nl = n.dot(r.d) < 0 ? n : n * -1; // Garante que a normal aponte para fora

        if (id == 1) { // Malha 3D
            f = Vec3(0.7, 0.7, 0.7); // Cor base cinza

            // Lógica de Textura
            if (hitFaceIdx >= 0 && hitFaceIdx < (int)g_renderMesh->faceTextureID.size()) {
                int texID = g_renderMesh->faceTextureID[hitFaceIdx];
                if (texID >= 0 && texID < (int)g_renderMesh->textures.size()) {
                    const auto& uvs = g_renderMesh->faceUVs[hitFaceIdx];
                    if (uvs.size() >= 3) {
                        // Interpola coordenadas UV usando baricêntricas
                        float interp_u = (1.0 - u_bar - v_bar) * uvs[0].u + u_bar * uvs[1].u + v_bar * uvs[2].u;
                        float interp_v = (1.0 - u_bar - v_bar) * uvs[0].v + u_bar * uvs[1].v + v_bar * uvs[2].v;
                        f = sampleTexture(g_renderMesh->textures[texID], interp_u, interp_v);
                    }
                }
            }
        }
        else { // Chão
            // Padrão procedural xadrez baseado na posição X/Z
            bool grid = (int(std::floor(x.x) + std::floor(x.z)) & 1) == 0;
            f = grid ? Vec3(0.8, 0.8, 0.8) : Vec3(0.2, 0.2, 0.2);
        }

        // --- NEXT EVENT ESTIMATION (NEE) ---
        // Técnica crucial para reduzir ruído. Em vez de esperar bater na luz por sorte,
        // conectamos explicitamente o ponto de impacto à fonte de luz.
        {
            Vec3 directLightSum(0, 0, 0);
            int shadowSamples = 1; // 1 raio de sombra por bounce (otimização de performance)

            for(int s=0; s<shadowSamples; ++s) {
                // Sorteia um ponto na superfície da luz
                Vec3 lightSample = lightPos + randomUnitVector(seed) * lightRadius;
                Vec3 toLight = lightSample - x; // Vetor direção para a luz
                double distSq = toLight.dot(toLight); // Distância ao quadrado
                double dist = std::sqrt(distSq);
                Vec3 L_dir = toLight * (1.0 / dist); // Normaliza

                // Dispara "Raio de Sombra"
                Ray shadowRay(x + nl * 1e-4, L_dir); // Offset epsilon evita auto-sombra (acne)
                double t_s; int id_s; Vec3 n_s; int fh_s; double u_s, v_s;

                // Verifica visibilidade (Oclusão)
                bool visible = false;
                if (getIntersection(shadowRay, t_s, id_s, n_s, fh_s, u_s, v_s)) {
                    // Se bateu na luz (id 3) e a distância é compatível
                    if (id_s == 3 && t_s < dist + 0.1) visible = true;
                }

                if (visible) {
                    double cosTheta = nl.dot(L_dir); // Ângulo de incidência
                    if (cosTheta > 0) {
                        // Cálculo físico da radiância
                        double area = 4.0 * 3.1415926 * lightRadius * lightRadius;
                        double geometryTerm = cosTheta * (area / distSq); // Decaimento quadrático da luz

                        // Clamp para evitar pixels brancos explosivos (fireflies)
                        if (geometryTerm > 10.0) geometryTerm = 10.0;

                        // Soma a luz direta ponderada pela cor da superfície (f)
                        directLightSum = directLightSum + lightEmission * f * geometryTerm;
                    }
                }
            }
            // Acumula na cor final
            finalColor = finalColor + throughput * directLightSum * (1.0 / shadowSamples);
        }

        // --- ROLETA RUSSA (Russian Roulette) ---
        // Técnica probabilística para terminar raios sem introduzir viés (bias).
        // Se a superfície é escura, a chance de absorver o raio é alta.
        double p = std::max({f.x, f.y, f.z}); // Probabilidade de sobrevivência baseada no brilho
        if (depth > 2) { // Só ativa após 2 bounces para garantir qualidade mínima
            if (random_float(seed) < p) f = f * (1.0 / p); // Sobreviveu: Aumenta energia para compensar os mortos
            else break; // Morreu: Encerra o caminho
        }

        // Atualiza o throughput (filtro de cor) para o próximo bounce
        throughput = throughput * f;

        // --- LUZ INDIRETA (GI) ---
        // Gera direção aleatória para o próximo raio (Reflexão Difusa)
        double r1 = 2 * 3.14159 * random_float(seed);
        double r2 = random_float(seed);
        double r2s = std::sqrt(r2);

        // Base ortonormal local alinhada à normal
        Vec3 w = nl;
        Vec3 u = ((std::abs(w.x) > 0.1 ? Vec3(0, 1, 0) : Vec3(1, 0, 0)).cross(w)).norm();
        Vec3 v = w.cross(u);

        // Direção final (Cosseno ponderada no hemisfério)
        Vec3 d = (u * std::cos(r1) * r2s + v * std::sin(r1) * r2s + w * std::sqrt(1 - r2)).norm();

        r = Ray(x, d);
        r.o = r.o + r.d * 1e-4; // Offset para evitar auto-interseção
    }
    return finalColor;
}

// ==========================================
// 7. TONE MAPPING (ACES FILMIC)
// ==========================================
// Curva de resposta de filme para converter HDR (0 a infinito) para LDR (0 a 1)
// Preserva contraste e saturação melhor que métodos lineares.
inline double aces(double x) {
    const double a = 2.51f; const double b = 0.03f; const double c = 2.43f; const double d = 0.59f; const double e = 0.14f;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}
inline double clamp(double x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

// Converte cor final para Byte (0-255) para exibição no monitor.
inline int toInt(double x) {
    // 1. Controle de Exposição: Simula diafragma da câmera (0.6 reduz estouro de luz)
    double exposure = 0.6;
    x = x * exposure;

    // 2. Tone Mapping
    x = aces(x);

    // 3. Gamma Correction: Converte de Espaço Linear para sRGB (Monitor)
    // O monitor aplica gama 2.2, então aplicamos o inverso (1/2.2) para compensar.
    return int(std::pow(clamp(x), 1.0/2.2) * 255.0 + 0.5);
}

inline void renderPathTracing(const std::vector<std::array<float, 3>>& vertices_in, const std::vector<std::vector<unsigned int>>& faces_in, const std::string& outputName) {}

#endif