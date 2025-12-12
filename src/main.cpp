#include <iostream>
#include <string>
#include <vector>
#include <array>

#include "../models/file_io/file_io.h"
#include "../models/object/Object.h"
#include "performance.h"
#include "performance-no-prep.h"
#include "../render/PathTracer.h"

#include "../render/render.h"
#include "../render/controls.h"
#include <GL/glew.h>
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

// ---------------------------------------------------------
// VARIÁVEIS GLOBAIS DE CONTROLE (PATH TRACING INTERATIVO)
// ---------------------------------------------------------

SceneData* g_renderMesh = nullptr;

bool g_pathTracingMode = false;           // Flag: Ativa/Desativa o modo
int g_ptSamples = 0;                      // Contador: Quantas amostras já acumulamos
GLuint g_ptTexture = 0;                   // OpenGL: ID da textura onde desenhamos os pixels
std::vector<Vec3> g_accumBuffer;          // CPU: Buffer de alta precisão (float) para somar luz
std::vector<unsigned char> g_pixelBuffer; // CPU: Buffer de bytes (0-255) para mandar pra tela

// Dimensões da janela para o buffer de renderização
int g_winWidth = 800;
int g_winHeight = 600;

// Armazenamento global da geometria para o Path Tracer acessar
// (Necessário pois o g_object encapsula isso e precisamos dos dados brutos na função de render)
std::vector<Vec3> g_ptVertices;
std::vector<std::vector<unsigned int>> g_ptFaces;

// ---------------------------------------------------------
// VARIÁVEIS GLOBAIS DO MODO GRÁFICO (RASTERIZAÇÃO PADRÃO)
// ---------------------------------------------------------

object::Object* g_object = nullptr;
float g_rotation_x = 0.0f;
float g_rotation_y = 0.0f;
float g_offset_x = 0.0f;
float g_offset_y = 0.0f;
float g_zoom = 1.0f;
bool g_vertex_only_mode = false;
bool g_face_only_mode = false;

void initPathTracingTexture(int w, int h) {
    g_winWidth = w;
    g_winHeight = h;

    // Redimensiona buffers da CPU
    g_accumBuffer.resize(w * h, Vec3(0,0,0));
    g_pixelBuffer.resize(w * h * 3, 0);
    g_ptSamples = 0;

    // Configura textura na GPU
    if (g_ptTexture == 0) glGenTextures(1, &g_ptTexture);
    glBindTexture(GL_TEXTURE_2D, g_ptTexture);

    // Filtros NEAREST para ver os pixels exatos (bom para debugar path tracing)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Aloca a memória na GPU
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
}

// ---------------------------------------------------------
// ATUALIZAÇÃO DO FRAME (PATH TRACING)
// ---------------------------------------------------------
void updatePathTracingFrame() {
    // Se não houver dados de cena (criados no controls.cpp), não faz nada
    if (!g_renderMesh) return;

    // --- 1. Detecção de Movimento (Reset) ---
    static float last_rot_x = 0.0f;
    static float last_rot_y = 0.0f;
    static float last_zoom = 0.0f;
    static float last_off_x = 0.0f;
    static float last_off_y = 0.0f;

    // Se a câmera mexeu, a imagem acumulada antiga é inválida. Resetamos.
    if (last_rot_x != g_rotation_x || last_rot_y != g_rotation_y ||
        last_zoom != g_zoom || last_off_x != g_offset_x || last_off_y != g_offset_y) {

        g_ptSamples = 0; // Reinicia contador
        std::fill(g_accumBuffer.begin(), g_accumBuffer.end(), Vec3(0,0,0)); // Limpa tela (preto)

        // Atualiza estado
        last_rot_x = g_rotation_x; last_rot_y = g_rotation_y;
        last_zoom = g_zoom;
        last_off_x = g_offset_x; last_off_y = g_offset_y;
    }

    // --- 2. Cálculo da Câmera (Orbit) ---
    float radX = g_rotation_x * 0.0174533f;
    float radY = g_rotation_y * 0.0174533f;
    float dist = 4.0f / (g_zoom > 0.1f ? g_zoom : 0.1f);

    float camX = sin(radY) * cos(radX) * dist;
    float camY = -sin(radX) * dist;
    float camZ = cos(radY) * cos(radX) * dist;
    camX -= g_offset_x;
    camY -= g_offset_y;

    Vec3 origin(camX, camY, camZ);
    Vec3 target(-g_offset_x, -g_offset_y, 0);
    Vec3 direction = (target - origin).norm();
    Ray cam(origin, direction);

    Vec3 worldUp(0, 1, 0);
    Vec3 right = direction.cross(worldUp).norm();
    Vec3 up = right.cross(direction).norm();

    double aspect = (double)g_winWidth / (double)g_winHeight;
    Vec3 cx = right * 0.5135 * aspect;
    Vec3 cy = up * -0.5135; // Invertido para corrigir orientação Y

    // --- 3. Renderização (Multi-Thread) ---
    g_ptSamples++;

    #pragma omp parallel for schedule(dynamic, 1)
    for (int y = 0; y < g_winHeight; y++) {
        // Semente aleatória única por pixel/frame para o Monte Carlo
        uint32_t seed = (y * 91214) + (g_ptSamples * 71932);

        for (int x = 0; x < g_winWidth; x++) {
            int i = (g_winHeight - 1 - y) * g_winWidth + x;

            // Anti-Aliasing (Tent Filter)
            float r1 = 2.0f * random_float(seed);
            float r2 = 2.0f * random_float(seed);
            float dx = (r1 < 1.0f) ? sqrt(r1) - 1.0f : 1.0f - sqrt(2.0f - r1);
            float dy = (r2 < 1.0f) ? sqrt(r2) - 1.0f : 1.0f - sqrt(2.0f - r2);

            Vec3 d = cx * (((x + dx) / g_winWidth) - 0.5) * 2.0 +
                     cy * (((y + dy) / g_winHeight) - 0.5) * 2.0 + cam.d;

            // [CRÍTICO] Chama radiance passando a semente.
            // A textura é processada automaticamente lá dentro via g_renderMesh->textures
            g_accumBuffer[i] = g_accumBuffer[i] + radiance(Ray(cam.o, d.norm()), seed);

            // Média e Tone Mapping
            Vec3 color = g_accumBuffer[i] * (1.0 / g_ptSamples);

            // Grava no buffer de exibição
            g_pixelBuffer[i * 3 + 0] = toInt(color.x);
            g_pixelBuffer[i * 3 + 1] = toInt(color.y);
            g_pixelBuffer[i * 3 + 2] = toInt(color.z);
        }
    }

    // Envia textura para a GPU
    glBindTexture(GL_TEXTURE_2D, g_ptTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_winWidth, g_winHeight, GL_RGB, GL_UNSIGNED_BYTE, g_pixelBuffer.data());
}

// ---------------------------------------------------------
// CALLBACK DE DESENHO (DISPLAY)
// ---------------------------------------------------------
void displayCallback() {
    // Limpa a tela e o buffer de profundidade a cada quadro
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (g_pathTracingMode) {
        // ====================================================
        // MODO PATH TRACING (Renderização Progressiva)
        // ====================================================
        updatePathTracingFrame();

        // Desenha o Quad com a textura do Ray Tracing
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_ptTexture);

        glColor3f(1, 1, 1);
        glBegin(GL_QUADS);
            glTexCoord2f(0, 0); glVertex2f(-1, -1);
            glTexCoord2f(1, 0); glVertex2f( 1, -1);
            glTexCoord2f(1, 1); glVertex2f( 1,  1);
            glTexCoord2f(0, 1); glVertex2f(-1,  1);
        glEnd();

        glDisable(GL_TEXTURE_2D);
        glEnable(GL_DEPTH_TEST);

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);

        glutSwapBuffers();
        glutPostRedisplay();

    } else {
        // ====================================================
        // MODO RASTERIZAÇÃO PADRÃO (OpenGL)
        // ====================================================
        glPushMatrix();
            // Transformações de câmera
            glTranslatef(g_offset_x, g_offset_y, 0.0f);
            glScalef(g_zoom, g_zoom, g_zoom);
            glRotatef(g_rotation_x, 1.0f, 0.0f, 0.0f);
            glRotatef(g_rotation_y, 0.0f, 1.0f, 0.0f);

            render::ColorsMap colors;
            colors["surface"] = {0.8f, 0.8f, 0.8f};
            colors["edge"]    = {0.0f, 0.0f, 0.0f};
            colors["vertex"]  = {0.0f, 0.0f, 0.0f};

            if (g_object) {
                // Desenha a malha base (respeita os modos vertex/face only internamente)
                g_object->draw(colors, g_vertex_only_mode, g_face_only_mode);

                // [CORREÇÃO] Só desenha as texturas se NÃO estiver no modo "Apenas Vértices"
                if (!g_vertex_only_mode) {
                    g_object->drawTexturedFaces();
                }
            }
        glPopMatrix();

        glutSwapBuffers();
    }
}

void reshapeCallback(int width, int height) {
    render::setup_opengl(width, height);
}

void idleCallback() {
    controls::updateRotation(g_rotation_x, g_rotation_y);
    controls::updateNavigation(g_offset_x, g_offset_y);
    glutPostRedisplay();
}

void runGraphicalApp(int argc, char** argv) {
    // -------------------------------------------------
    // 1. Inicialização Padrão (GLUT/GLEW)
    // -------------------------------------------------
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    int winWidth = 800, winHeight = 600;
    glutInitWindowSize(winWidth, winHeight);
    glutCreateWindow("Visualizador de Malha - OpenGL");
    glutSetKeyRepeat(GLUT_KEY_REPEAT_ON);

#ifndef __APPLE__
    GLenum err = glewInit();
    if (GLEW_OK != err) {
        std::cerr << "Erro ao inicializar GLEW: " << glewGetErrorString(err) << std::endl;
        exit(EXIT_FAILURE);
    }
#endif

    render::setup_opengl(winWidth, winHeight);

    // -------------------------------------------------
    // 2. Carregamento do Arquivo
    // -------------------------------------------------
    // Detection size pequeno para garantir precisão no clique
    int detection_size = 5;
    std::string filename = "../assets/modelo-planta.obj"; // Ajuste conforme necessário

    fileio::MeshData mesh;
    try {
        mesh = fileio::read_file(filename);
    } catch (const std::exception& e) {
        std::cerr << "Erro ao carregar o arquivo: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    // Copia vértices para vetor local
    std::vector<std::array<float, 3>> vertices;
    for (const auto& v : mesh.vertices) {
        vertices.push_back({
            static_cast<float>(v[0]),
            static_cast<float>(v[1]),
            static_cast<float>(v[2])
        });
    }

    // -------------------------------------------------
    // 3. Normalização e Escala (FIX do Zoom)
    // -------------------------------------------------
    // Calcula Bounding Box
    float minX = vertices[0][0], maxX = vertices[0][0];
    float minY = vertices[0][1], maxY = vertices[0][1];
    float minZ = vertices[0][2], maxZ = vertices[0][2];
    for (const auto &v : vertices) {
        if (v[0] < minX) minX = v[0]; if (v[0] > maxX) maxX = v[0];
        if (v[1] < minY) minY = v[1]; if (v[1] > maxY) maxY = v[1];
        if (v[2] < minZ) minZ = v[2]; if (v[2] > maxZ) maxZ = v[2];
    }

    float centerX = (minX + maxX) / 2.0f;
    float centerY = (minY + maxY) / 2.0f;
    float centerZ = (minZ + maxZ) / 2.0f;

    float width = maxX - minX;
    float height = maxY - minY;
    float depth = maxZ - minZ;
    float maxDimension = std::max(std::max(width, height), depth);

    // Fator de escala para normalizar o objeto para tamanho ~2.0
    float scaleFactor = 2.0f / (maxDimension > 0 ? maxDimension : 1.0f);

    // [IMPORTANTE] Aplica a escala DIRETAMENTE nos vértices locais.
    // Isso conserta o zoom "blocado" em cenas grandes como Cornell Box.
    for (auto &v : vertices) {
        v[0] = (v[0] - centerX) * scaleFactor;
        v[1] = (v[1] - centerY) * scaleFactor;
        v[2] = (v[2] - centerZ) * scaleFactor;
    }

    // Reseta o zoom visual do OpenGL para neutro (1.0)
    g_zoom = 1.0f;

    // Copia faces
    std::vector<std::vector<unsigned int>> faces;
    for (const auto& face : mesh.faces) {
        std::vector<unsigned int> f;
        for (auto idx : face)
            f.push_back(static_cast<unsigned int>(idx));
        faces.push_back(f);
    }

    // -------------------------------------------------
    // 4. Preparação dos Grupos (Face Cells)
    // -------------------------------------------------
    // [IMPORTANTE] Copia os IDs de grupo lidos do arquivo (ex: usemtl)
    // Isso permite que o SHIFT+A selecione objetos lógicos inteiros.
    std::vector<unsigned int> face_cells;

    if (!mesh.faceCells.empty()) {
        for (int id : mesh.faceCells) {
            // Converte int para unsigned int.
            // Se id for -1 (sem grupo), vira 0xFFFFFFFF (Max Uint)
            face_cells.push_back(static_cast<unsigned int>(id));
        }
    } else {
        // Fallback: Se o arquivo não tinha info de grupo, preenche com valor padrão
        face_cells.resize(faces.size(), 0xFFFFFFFF);
    }

    // ==============================================================
    // PREPARAÇÃO DADOS PATH TRACER
    // ==============================================================
    g_ptVertices.clear();
    g_ptFaces.clear();

    // Como os vértices JÁ estão escalados, apenas copiamos para o renderizador
    for (const auto& v : vertices) {
        g_ptVertices.push_back(Vec3(v[0], v[1], v[2]));
    }
    g_ptFaces = faces;
    // ==============================================================

    // -------------------------------------------------
    // 5. Criação do Objeto Gráfico
    // -------------------------------------------------
    std::array<float, 3> position = { 0.0f, 0.0f, 0.0f };

    // Passamos os 'vertices' normalizados e 'face_cells' preenchido corretamente
    g_object = new object::Object(position, vertices, faces, face_cells, filename, detection_size, true);
    g_object->clearColors();

    // Registra os callbacks do GLUT
    glutDisplayFunc(displayCallback);
    glutReshapeFunc(reshapeCallback);
    glutKeyboardFunc(controls::keyboardDownCallback);
    glutKeyboardUpFunc(controls::keyboardUpCallback);
    glutSpecialFunc(controls::specialKeyboardDownCallback);
    glutSpecialUpFunc(controls::specialKeyboardUpCallback);
    glutIdleFunc(idleCallback);
    glutMouseFunc(controls::mouseCallback);

    // Inicia o loop do GLUT
    glutMainLoop();

    // Limpeza
    if (g_object) delete g_object;
}

// -----------------------
// Modo Path Tracing
// -----------------------

void runPathTracingMode() {
    std::string filename = "../assets/indoor_plant_02.obj";
    std::cout << "Modo Path Tracing: Carregando " << filename << "..." << std::endl;

    // 1. Carrega o arquivo
    fileio::MeshData mesh;
    try {
        mesh = fileio::read_file(filename);
    } catch (const std::exception& e) {
        std::cerr << "Erro: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    std::vector<std::array<float, 3>> vertices;
    for (const auto& v : mesh.vertices) {
        vertices.push_back({static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])});
    }

    // 2. Calcula Limites (Min/Max)
    float minX = vertices[0][0], maxX = vertices[0][0];
    float minY = vertices[0][1], maxY = vertices[0][1];
    float minZ = vertices[0][2], maxZ = vertices[0][2];
    for (const auto &v : vertices) {
        if (v[0] < minX) minX = v[0]; if (v[0] > maxX) maxX = v[0];
        if (v[1] < minY) minY = v[1]; if (v[1] > maxY) maxY = v[1];
        if (v[2] < minZ) minZ = v[2]; if (v[2] > maxZ) maxZ = v[2];
    }

    // 3. Calcula Centro
    float centerX = (minX + maxX) / 2.0f;
    float centerY = (minY + maxY) / 2.0f;
    float centerZ = (minZ + maxZ) / 2.0f;

    // 4. Calcula Dimensão Máxima
    float width = maxX - minX;
    float height = maxY - minY;
    float depth = maxZ - minZ;
    float maxDimension = std::max(std::max(width, height), depth);

    // ==============================================================
    // [AQUI ESTÃO AS MODIFICAÇÕES SOLICITADAS]
    // ==============================================================

    // Calcula a escala (com proteção contra divisão por zero)
    // O objetivo é deixar o objeto com tamanho aproximado de 2.0 unidades
    float scale = 2.0f / (maxDimension > 0 ? maxDimension : 1.0f);

    // Aplica Centralização E Escala diretamente nos vértices
    for (auto &v : vertices) {
        v[0] = (v[0] - centerX) * scale;
        v[1] = (v[1] - centerY) * scale;
        v[2] = (v[2] - centerZ) * scale;
    }
    // ==============================================================

    // 5. Prepara as faces
    std::vector<std::vector<unsigned int>> faces;
    for (const auto& face : mesh.faces) {
        std::vector<unsigned int> f;
        for (auto idx : face) f.push_back(static_cast<unsigned int>(idx));
        faces.push_back(f);
    }

    // 6. Renderiza
    // O nome do arquivo de saída pode ser alterado aqui
    renderPathTracing(vertices, faces, "render_output2_plant.ppm");
}

// -----------------------
// Modo Performance Test
// -----------------------

void runPerformanceTest() {
    std::string filename = "../assets/5-vertebra-save.off";

    std::cout << "Modo de teste de desempenho iniciado." << std::endl;

    // Carrega a malha
    fileio::MeshData mesh;
    try {
        mesh = fileio::read_file(filename);
    } catch (const std::exception& e) {
        std::cerr << "Erro ao carregar o arquivo: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    // Converte os vértices
    std::vector<std::array<float, 3>> vertices;
    for (const auto& v : mesh.vertices) {
        vertices.push_back({
            static_cast<float>(v[0]),
            static_cast<float>(v[1]),
            static_cast<float>(v[2])
        });
    }

    // Converte as faces
    std::vector<std::vector<unsigned int>> faces;
    for (const auto& face : mesh.faces) {
        std::vector<unsigned int> f;
        for (auto idx : face) {
            f.push_back(static_cast<unsigned int>(idx));
        }
        faces.push_back(f);
    }

    // Se houver dados de face_cells, utilize-os; caso contrário, vetor vazio
    std::vector<unsigned int> face_cells;
    std::array<float, 3> position = {0.0f, 0.0f, 0.0f};
    int detection_size = 100;

    object::Object obj(position, vertices, faces, face_cells, filename, detection_size, false);

    exportPerformanceData(obj, "C:/Users/bia/CLionProjects/teste/src/prep/performance-5-vertebra-off.csv");

    std::cout << "Teste de desempenho finalizado." << std::endl;
}

void runPerformanceTestNoPrep() {
    std::string filename = "../assets/hand-hybrid-teste.off";

    std::cout << "Modo de teste de desempenho iniciado." << std::endl;

    // Carrega a malha
    fileio::MeshData mesh;
    try {
        mesh = fileio::read_file(filename);
    } catch (const std::exception& e) {
        std::cerr << "Erro ao carregar o arquivo: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    // Converte os vértices
    std::vector<std::array<float, 3>> vertices;
    for (const auto& v : mesh.vertices) {
        vertices.push_back({
            static_cast<float>(v[0]),
            static_cast<float>(v[1]),
            static_cast<float>(v[2])
        });
    }

    // Converte as faces
    std::vector<std::vector<unsigned int>> faces;
    for (const auto& face : mesh.faces) {
        std::vector<unsigned int> f;
        for (auto idx : face) {
            f.push_back(static_cast<unsigned int>(idx));
        }
        faces.push_back(f);
    }

    // Se houver dados de face_cells, utilize-os; caso contrário, vetor vazio
    std::vector<unsigned int> face_cells;
    std::array<float, 3> position = {0.0f, 0.0f, 0.0f};
    int detection_size = 100;

    object::Object obj(position, vertices, faces, face_cells, filename, detection_size, false);

    exportPerformanceDataNoPrep(obj, "C:/Users/bia/CLionProjects/teste/src/no-prep/performance-hand-no-prep.csv");

    std::cout << "Teste de desempenho finalizado." << std::endl;
}


// -----------------------
// Main: escolhe o modo com base no argumento de linha de comando
// -----------------------

int main(int argc, char** argv) {
    // Se receber um argumento, verifique: "0" para performance test, "1" para a aplicação gráfica.
    if (argc > 1) {
        std::string mode = argv[1];
        if (mode == "0") {
            runPerformanceTest();
        } else if (mode == "1") {
            runGraphicalApp(argc, argv);
        } else if (mode == "2") {
            runPerformanceTestNoPrep();
        } else if (mode == "3") {
            runPathTracingMode();
        } else {
            std::cerr << "Modo inválido. Use '0' para teste de desempenho ou '1' para aplicação gráfica." << std::endl;
            return EXIT_FAILURE;
        }
    } else {
        // Se nenhum argumento for passado a aplicação padrão será a aplicação gráfica
        runGraphicalApp(argc, argv);
    }
    return 0;
}
