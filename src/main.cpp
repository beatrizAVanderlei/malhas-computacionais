/*
 * ======================================================================================
 * MAIN.CPP - ARQUITETURA E PONTO DE ENTRADA
 * ======================================================================================
 *
 * Este arquivo atua como o "Orquestrador" da aplicação. Ele implementa um padrão
 * arquitetural híbrido que gerencia dois pipelines de renderização distintos:
 *
 * 1. PIPELINE DE RASTERIZAÇÃO (OpenGL Padrão):
 * - Uso: Visualização em tempo real, edição de malha, feedback imediato (60 FPS).
 * - Técnica: Projeção de geometria 3D em plano 2D usando a GPU (Graphics Pipeline).
 *
 * 2. PIPELINE DE RAY TRACING (Path Tracer via CPU):
 * - Uso: Renderização fotorealista fisicamente baseada (PBR).
 * - Técnica: Integração de Monte Carlo acumulativa via CPU, exibida na tela
 * através de uma textura OpenGL (Full-Screen Quad).
 *
 * RESPONSABILIDADES DESTE ARQUIVO:
 * - Inicialização do Contexto Gráfico (GLUT/GLEW).
 * - Gerenciamento de Janela e Input (Redirecionando para controls.cpp).
 * - Carregamento e Normalização de Dados (File I/O + Pré-processamento de Malha).
 * - Loop Principal de Renderização (Display Loop).
 *
 * ======================================================================================
 */

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

// Ponteiro para a cena otimizada (BVH) usada pelo Path Tracer.
// É separada da malha de edição (g_object) para garantir acesso thread-safe e rápido.
SceneData *g_renderMesh = nullptr;

bool g_pathTracingMode = false; // Flag de Estado: Alterna entre OpenGL e Path Tracing
int g_ptSamples = 0; // Acumulador: Número de quadros (samples) já calculados para a média
GLuint g_ptTexture = 0; // Handle OpenGL: Textura onde escrevemos o resultado do Ray Tracing

// Buffers de Imagem (Framebuffers de Software):
// g_accumBuffer: Armazena cores em ponto flutuante (HDR). Permite valores > 1.0 e soma precisa.
std::vector<Vec3> g_accumBuffer;
// g_pixelBuffer: Armazena o resultado final em bytes (LDR 0-255) para envio à GPU.
std::vector<unsigned char> g_pixelBuffer;

// Dimensões da janela (Viewport)
int g_winWidth = 800;
int g_winHeight = 600;

// Armazenamento intermediário da geometria crua.
// Necessário porque g_object encapsula os dados, e o Path Tracer precisa de acesso direto (raw access).
std::vector<Vec3> g_ptVertices;
std::vector<std::vector<unsigned int> > g_ptFaces;

// ---------------------------------------------------------
// VARIÁVEIS GLOBAIS DO MODO GRÁFICO (RASTERIZAÇÃO PADRÃO)
// ---------------------------------------------------------

object::Object *g_object = nullptr; // A instância principal do objeto sendo editado
float g_rotation_x = 0.0f; // Ângulo de Euler X (Pitch)
float g_rotation_y = 0.0f; // Ângulo de Euler Y (Yaw)
float g_offset_x = 0.0f; // Pan X (Translação da câmera)
float g_offset_y = 0.0f; // Pan Y
float g_zoom = 1.0f; // Fator de escala da visualização
bool g_vertex_only_mode = false; // Flag de visualização: Apenas vértices (nuvem de pontos)
bool g_face_only_mode = false; // Flag de visualização: Apenas faces (sem wireframe)

// ---------------------------------------------------------
// INICIALIZAÇÃO DE RECURSOS DO PATH TRACER
// ---------------------------------------------------------
void initPathTracingTexture(int w, int h) {
    g_winWidth = w;
    g_winHeight = h;

    // Redimensiona os buffers da CPU para coincidir com a resolução da janela.
    // Limpa com preto (0,0,0) para iniciar uma nova renderização.
    g_accumBuffer.resize(w * h, Vec3(0, 0, 0));
    g_pixelBuffer.resize(w * h * 3, 0); // 3 canais (R, G, B)
    g_ptSamples = 0; // Reseta o contador de convergência

    // Configura a textura na GPU (OpenGL)
    if (g_ptTexture == 0) glGenTextures(1, &g_ptTexture); // Gera ID apenas se não existir
    glBindTexture(GL_TEXTURE_2D, g_ptTexture);

    // Configura filtros de amostragem.
    // GL_NEAREST é usado aqui para vermos cada pixel do Ray Tracer com nitidez ("pixel perfect"),
    // útil para depurar ruído e convergência. GL_LINEAR borraria o resultado.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Aloca a memória na VRAM da GPU (inicialmente vazia/nullptr)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
}

// ---------------------------------------------------------
// ATUALIZAÇÃO DO FRAME (PATH TRACING LOOP)
// ---------------------------------------------------------
// Esta função é chamada a cada frame quando o modo 'P' está ativo.
// Ela dispara raios, acumula luz e atualiza a textura.
void updatePathTracingFrame() {
    if (!g_renderMesh) return;

    // --- 1. Detecção de Movimento ---
    static float last_rot_x = 0.0f;
    static float last_rot_y = 0.0f;
    static float last_zoom = 0.0f;
    static float last_off_x = 0.0f;
    static float last_off_y = 0.0f;

    bool isMoving = false;

    // Verifica se houve mudança na câmera
    if (last_rot_x != g_rotation_x || last_rot_y != g_rotation_y ||
        last_zoom != g_zoom || last_off_x != g_offset_x || last_off_y != g_offset_y) {
        isMoving = true;
        g_ptSamples = 0; // Reinicia o acumulador
        std::fill(g_accumBuffer.begin(), g_accumBuffer.end(), Vec3(0, 0, 0));

        last_rot_x = g_rotation_x;
        last_rot_y = g_rotation_y;
        last_zoom = g_zoom;
        last_off_x = g_offset_x;
        last_off_y = g_offset_y;
    }

    // --- 2. Resolução Dinâmica (OTIMIZAÇÃO CRÍTICA) ---
    // Se estiver movendo (ou nos primeiros frames), reduz a resolução.
    // step = 1 (Qualidade Máxima), step = 4 ou 6 (Rápido/Pixelado)
    int step = (g_ptSamples < 4) ? 6 : 1;

    // --- 3. Cálculo da Câmera ---
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

    double aspect = (double) g_winWidth / (double) g_winHeight;
    Vec3 cx = right * 0.5135 * aspect;
    Vec3 cy = up * -0.5135;

    g_ptSamples++;

    // --- 4. Render Loop com Salto de Pixels ---
    // O loop pula 'step' pixels para ganhar velocidade
#pragma omp parallel for schedule(dynamic, 2)
    for (int y = 0; y < g_winHeight; y += step) {
        uint32_t seed = (y * 91214) + (g_ptSamples * 71932);

        for (int x = 0; x < g_winWidth; x += step) {
            int i = (g_winHeight - 1 - y) * g_winWidth + x;

            // Se for modo rápido (step > 1), não fazemos anti-aliasing jittering
            float dx = 0, dy = 0;
            if (step == 1) {
                float r1 = 2.0f * random_float(seed);
                float r2 = 2.0f * random_float(seed);
                dx = (r1 < 1.0f) ? sqrt(r1) - 1.0f : 1.0f - sqrt(2.0f - r1);
                dy = (r2 < 1.0f) ? sqrt(r2) - 1.0f : 1.0f - sqrt(2.0f - r2);
            }

            Vec3 d = cx * (((x + dx) / g_winWidth) - 0.5) * 2.0 +
                     cy * (((y + dy) / g_winHeight) - 0.5) * 2.0 + cam.d;

            Vec3 rayColor = radiance(Ray(cam.o, d.norm()), seed);

            // Acumula
            if (step == 1) {
                g_accumBuffer[i] = g_accumBuffer[i] + rayColor;
            } else {
                // No modo rápido, não acumulamos, apenas sobrescrevemos para feedback instantâneo
                g_accumBuffer[i] = rayColor * g_ptSamples;
            }

            Vec3 color = g_accumBuffer[i] * (1.0 / g_ptSamples);

            // Converte cor
            unsigned char r = toInt(color.x);
            unsigned char g = toInt(color.y);
            unsigned char b = toInt(color.z);

            // PREENCHIMENTO DE BLOCO (Pixelation Effect)
            // Se step > 1, preenchemos o bloco vizinho com a mesma cor para não ficar tela preta
            for (int by = 0; by < step; ++by) {
                if (y + by >= g_winHeight) break;
                for (int bx = 0; bx < step; ++bx) {
                    if (x + bx >= g_winWidth) break;

                    int blockIndex = ((g_winHeight - 1 - (y + by)) * g_winWidth + (x + bx)) * 3;
                    g_pixelBuffer[blockIndex + 0] = r;
                    g_pixelBuffer[blockIndex + 1] = g;
                    g_pixelBuffer[blockIndex + 2] = b;
                }
            }
        }
    }

    glBindTexture(GL_TEXTURE_2D, g_ptTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_winWidth, g_winHeight, GL_RGB, GL_UNSIGNED_BYTE, g_pixelBuffer.data());
}

// ---------------------------------------------------------
// CALLBACK DE DESENHO (DISPLAY)
// ---------------------------------------------------------
// Função chamada pelo GLUT sempre que a janela precisa ser redesenhada (geralmente 60 FPS).
void displayCallback() {
    // Limpa o fundo e o buffer de profundidade (Z-Buffer)
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f); // Fundo Branco
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Verifica qual modo de renderização está ativo
    if (g_pathTracingMode) {
        // ====================================================
        // MODO PATH TRACING (Renderização via CPU + Textura GPU)
        // ====================================================

        // 1. Calcula o próximo passo da renderização física
        updatePathTracingFrame();

        // 2. Prepara OpenGL para desenhar em 2D (Overlay)
        // Salva e reseta as matrizes para garantir coordenadas ortogonais
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        // Desabilita teste de profundidade (queremos desenhar por cima de tudo)
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_ptTexture);

        // 3. Desenha um Quadrado (Quad) que cobre a tela inteira
        // Mapeia a textura gerada pelo Path Tracer neste quadrado.
        glColor3f(1, 1, 1);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0);
        glVertex2f(-1, -1);
        glTexCoord2f(1, 0);
        glVertex2f(1, -1);
        glTexCoord2f(1, 1);
        glVertex2f(1, 1);
        glTexCoord2f(0, 1);
        glVertex2f(-1, 1);
        glEnd();

        // 4. Restaura estado original
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_DEPTH_TEST);

        glPopMatrix(); // Restaura ModelView
        glMatrixMode(GL_PROJECTION);
        glPopMatrix(); // Restaura Projection
        glMatrixMode(GL_MODELVIEW);

        glutSwapBuffers(); // Troca os buffers (Double Buffering)

        // Solicita redesenho imediato para continuar o loop de refinamento da imagem
        glutPostRedisplay();
    } else {
        // ====================================================
        // MODO RASTERIZAÇÃO PADRÃO (OpenGL Tradicional)
        // ====================================================
        glPushMatrix();
        // Aplica transformações da câmera (View Matrix)
        glTranslatef(g_offset_x, g_offset_y, 0.0f);
        glScalef(g_zoom, g_zoom, g_zoom);
        glRotatef(g_rotation_x, 1.0f, 0.0f, 0.0f);
        glRotatef(g_rotation_y, 0.0f, 1.0f, 0.0f);

        // Definição de cores básicas para o renderizador
        render::ColorsMap colors;
        colors["surface"] = {0.8f, 0.8f, 0.8f}; // Cor padrão das faces
        colors["edge"] = {0.0f, 0.0f, 0.0f}; // Cor das arestas
        colors["vertex"] = {0.0f, 0.0f, 0.0f}; // Cor dos vértices

        if (g_object) {
            // Desenha a geometria base (Vertices, Arestas, Faces sólidas)
            g_object->draw(colors, g_vertex_only_mode, g_face_only_mode);

            // Desenha a camada de texturas por cima da malha
            // [IMPORTANTE] Só desenha se NÃO estiver no modo "Apenas Vértices"
            // para respeitar a visualização de nuvem de pontos.
            if (!g_vertex_only_mode) {
                g_object->drawTexturedFaces();
            }
        }
        glPopMatrix();

        glutSwapBuffers();
    }
}

// Callback chamado quando a janela é redimensionada
void reshapeCallback(int width, int height) {
    // Atualiza o Viewport e a Matriz de Projeção
    render::setup_opengl(width, height);
}

// Callback chamado quando o sistema está ocioso (Idle)
void idleCallback() {
    // Processa input contínuo (ex: segurar tecla para girar)
    controls::updateRotation(g_rotation_x, g_rotation_y);
    controls::updateNavigation(g_offset_x, g_offset_y);
    glutPostRedisplay();
}

// ---------------------------------------------------------
// APLICAÇÃO GRÁFICA INTERATIVA (MODO 1)
// ---------------------------------------------------------
void runGraphicalApp(int argc, char **argv) {
    // 1. Inicialização do GLUT (Janela e Contexto)
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH); // Buffer Duplo, RGB e Z-Buffer
    int winWidth = 800, winHeight = 600;
    glutInitWindowSize(winWidth, winHeight);
    glutCreateWindow("Visualizador de Malha - OpenGL");
    glutSetKeyRepeat(GLUT_KEY_REPEAT_ON); // Permite repetição de tecla (segurar)

    // Inicialização do GLEW (Apenas Windows/Linux)
#ifndef __APPLE__
    GLenum err = glewInit();
    if (GLEW_OK != err) {
        std::cerr << "Erro ao inicializar GLEW: " << glewGetErrorString(err) << std::endl;
        exit(EXIT_FAILURE);
    }
#endif

    // Configura estado inicial do OpenGL (Cor de fundo, Luzes, etc.)
    render::setup_opengl(winWidth, winHeight);

    // 2. Carregamento do Arquivo 3D
    int detection_size = 5; // Tolerância para clique do mouse (picking)
    std::string filename = "../assets/cornell_box.obj";

    fileio::MeshData mesh;
    try {
        mesh = fileio::read_file(filename); // Parse do arquivo
    } catch (const std::exception &e) {
        std::cerr << "Erro ao carregar o arquivo: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    // Converte formato da struct de IO para vetor local
    std::vector<std::array<float, 3> > vertices;
    for (const auto &v: mesh.vertices) {
        vertices.push_back({
            static_cast<float>(v[0]),
            static_cast<float>(v[1]),
            static_cast<float>(v[2])
        });
    }

    // 3. Normalização e Escala (Passo Crítico)
    // Calcula o Bounding Box (Caixa envolvente) para centralizar e escalar o objeto.
    // Isso é vital para garantir que a câmera e o Path Tracer funcionem com valores numéricos estáveis.
    float minX = vertices[0][0], maxX = vertices[0][0];
    float minY = vertices[0][1], maxY = vertices[0][1];
    float minZ = vertices[0][2], maxZ = vertices[0][2];
    for (const auto &v: vertices) {
        if (v[0] < minX) minX = v[0];
        if (v[0] > maxX) maxX = v[0];
        if (v[1] < minY) minY = v[1];
        if (v[1] > maxY) maxY = v[1];
        if (v[2] < minZ) minZ = v[2];
        if (v[2] > maxZ) maxZ = v[2];
    }

    float centerX = (minX + maxX) / 2.0f;
    float centerY = (minY + maxY) / 2.0f;
    float centerZ = (minZ + maxZ) / 2.0f;

    float width = maxX - minX;
    float height = maxY - minY;
    float depth = maxZ - minZ;
    float maxDimension = std::max(std::max(width, height), depth);

    // Escala para caber em um cubo de tamanho 2.0
    float scaleFactor = 2.0f / (maxDimension > 0 ? maxDimension : 1.0f);

    // Aplica a transformação diretamente nos vértices (Baking)
    for (auto &v: vertices) {
        v[0] = (v[0] - centerX) * scaleFactor;
        v[1] = (v[1] - centerY) * scaleFactor;
        v[2] = (v[2] - centerZ) * scaleFactor;
    }

    g_zoom = 1.0f; // Reseta zoom da câmera

    // Prepara topologia (Faces)
    std::vector<std::vector<unsigned int> > faces;
    for (const auto &face: mesh.faces) {
        std::vector<unsigned int> f;
        for (auto idx: face)
            f.push_back(static_cast<unsigned int>(idx));
        faces.push_back(f);
    }

    // 4. Preparação de Grupos (Face Cells)
    // Copia IDs de grupo (ex: 'g' ou 'usemtl' do OBJ) para permitir seleção lógica.
    std::vector<unsigned int> face_cells;
    if (!mesh.faceCells.empty()) {
        for (int id: mesh.faceCells) {
            face_cells.push_back(static_cast<unsigned int>(id));
        }
    } else {
        // Se não houver grupos, preenche com valor sentinela (Max UINT)
        face_cells.resize(faces.size(), 0xFFFFFFFF);
    }

    // Prepara dados globais para o Path Tracer
    g_ptVertices.clear();
    g_ptFaces.clear();
    for (const auto &v: vertices) {
        g_ptVertices.push_back(Vec3(v[0], v[1], v[2]));
    }
    g_ptFaces = faces;

    // 5. Instanciação do Objeto "Deus" (God Object)
    std::array<float, 3> position = {0.0f, 0.0f, 0.0f};
    g_object = new object::Object(position, vertices, faces, face_cells, filename, detection_size, true);
    g_object->clearColors(); // Reseta para cor padrão (Cinza Claro)

    // Registra Callbacks
    glutDisplayFunc(displayCallback);
    glutReshapeFunc(reshapeCallback);
    glutKeyboardFunc(controls::keyboardDownCallback);
    glutKeyboardUpFunc(controls::keyboardUpCallback);
    glutSpecialFunc(controls::specialKeyboardDownCallback);
    glutSpecialUpFunc(controls::specialKeyboardUpCallback);
    glutIdleFunc(idleCallback);
    glutMouseFunc(controls::mouseCallback);

    // Entra no Loop Principal
    glutMainLoop();

    // Limpeza
    if (g_object) delete g_object;
}

// -----------------------
// MODO PATH TRACING OFFLINE (MODO 3)
// -----------------------
// Versão headless/console que gera um arquivo de imagem direto sem interface.
void runPathTracingMode() {
    std::string filename = "../assets/indoor_plant_02.obj";
    std::cout << "Modo Path Tracing: Carregando " << filename << "..." << std::endl;

    // 1. Carrega o arquivo
    fileio::MeshData mesh;
    try {
        mesh = fileio::read_file(filename);
    } catch (const std::exception &e) {
        std::cerr << "Erro: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    std::vector<std::array<float, 3> > vertices;
    for (const auto &v: mesh.vertices) {
        vertices.push_back({static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])});
    }

    // 2. Calcula Limites (Min/Max)
    float minX = vertices[0][0], maxX = vertices[0][0];
    float minY = vertices[0][1], maxY = vertices[0][1];
    float minZ = vertices[0][2], maxZ = vertices[0][2];
    for (const auto &v: vertices) {
        if (v[0] < minX) minX = v[0];
        if (v[0] > maxX) maxX = v[0];
        if (v[1] < minY) minY = v[1];
        if (v[1] > maxY) maxY = v[1];
        if (v[2] < minZ) minZ = v[2];
        if (v[2] > maxZ) maxZ = v[2];
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
    for (auto &v: vertices) {
        v[0] = (v[0] - centerX) * scale;
        v[1] = (v[1] - centerY) * scale;
        v[2] = (v[2] - centerZ) * scale;
    }
    // ==============================================================

    // 5. Prepara as faces
    std::vector<std::vector<unsigned int> > faces;
    for (const auto &face: mesh.faces) {
        std::vector<unsigned int> f;
        for (auto idx: face) f.push_back(static_cast<unsigned int>(idx));
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
    } catch (const std::exception &e) {
        std::cerr << "Erro ao carregar o arquivo: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    // Converte os vértices
    std::vector<std::array<float, 3> > vertices;
    for (const auto &v: mesh.vertices) {
        vertices.push_back({
            static_cast<float>(v[0]),
            static_cast<float>(v[1]),
            static_cast<float>(v[2])
        });
    }

    // Converte as faces
    std::vector<std::vector<unsigned int> > faces;
    for (const auto &face: mesh.faces) {
        std::vector<unsigned int> f;
        for (auto idx: face) {
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
    } catch (const std::exception &e) {
        std::cerr << "Erro ao carregar o arquivo: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }

    // Converte os vértices
    std::vector<std::array<float, 3> > vertices;
    for (const auto &v: mesh.vertices) {
        vertices.push_back({
            static_cast<float>(v[0]),
            static_cast<float>(v[1]),
            static_cast<float>(v[2])
        });
    }

    // Converte as faces
    std::vector<std::vector<unsigned int> > faces;
    for (const auto &face: mesh.faces) {
        std::vector<unsigned int> f;
        for (auto idx: face) {
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

int main(int argc, char **argv) {
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
