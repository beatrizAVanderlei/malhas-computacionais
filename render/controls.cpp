#include "controls.h"
#include <cctype>
#include <iostream>
#include <set>
#include <vector>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glew.h>
#include <GL/glut.h>
#endif

#include "../models/object/Object.h"
#include "../models/file_io/file_io.h"
#include "tinyfiledialogs.h"
#include "../render/PathTracer.h"
#include <queue>

/*
 * ======================================================================================
 * CONTROLS - GERENCIAMENTO DE ESTADO E INPUT
 * ======================================================================================
 * Este arquivo atua como a "ponte" (Controller) entre a entrada do usuário (Teclado/Mouse)
 * e a lógica da aplicação (Model/View).
 * * Responsabilidades:
 * 1. Processar teclas e atalhos.
 * 2. Gerenciar a câmera (rotação, zoom, pan).
 * 3. Alternar entre modos de renderização (OpenGL vs Path Tracing).
 * 4. Preparar e sincronizar dados entre a CPU (Malha editável) e a Engine de Render.
 */

// -------------------------------------------------------
// VARIÁVEIS GLOBAIS EXTERNAS (Estado da Aplicação)
// -------------------------------------------------------
// Referências para variáveis definidas no main.cpp.
// O uso de 'extern' permite que diferentes arquivos modifiquem o mesmo estado global.
extern object::Object *g_object; // Ponteiro para o objeto 3D sendo editado
extern float g_zoom;
extern bool g_vertex_only_mode;
extern bool g_face_only_mode;
extern float g_rotation_x;
extern float g_rotation_y;
extern float g_offset_x;
extern float g_offset_y;

// Variáveis do Path Tracing
extern bool g_pathTracingMode;
extern std::vector<Vec3> g_ptVertices;
extern std::vector<std::vector<unsigned int> > g_ptFaces;

void initPathTracingTexture(int w, int h);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Helper para gerar geometria de esfera
void generateSphereGeometry(float radius, float cx, float cy, float cz, int sectors, int stacks,
                            std::vector<std::array<float, 3> > &outVertices,
                            std::vector<std::vector<unsigned int> > &outFaces,
                            int startIndexOffset) {
    float x, y, z, xy;
    float sectorStep = 2 * M_PI / sectors;
    float stackStep = M_PI / stacks;
    float sectorAngle, stackAngle;

    // 1. Gera Vértices
    for (int i = 0; i <= stacks; ++i) {
        stackAngle = M_PI / 2 - i * stackStep; // de pi/2 a -pi/2
        xy = radius * cosf(stackAngle); // r * cos(u)
        z = radius * sinf(stackAngle); // r * sin(u)

        for (int j = 0; j <= sectors; ++j) {
            sectorAngle = j * sectorStep; // de 0 a 2pi

            x = xy * cosf(sectorAngle); // r * cos(u) * cos(v)
            y = xy * sinf(sectorAngle); // r * cos(u) * sin(v)

            // Adiciona vértice transladado pelo centro escolhido
            outVertices.push_back({x + cx, y + cy, z + cz});
        }
    }

    // 2. Gera Faces (Triângulos)
    // Conecta os vértices gerados usando índices relativos ao offset inicial
    int k1, k2;
    for (int i = 0; i < stacks; ++i) {
        k1 = i * (sectors + 1) + startIndexOffset; // Início da stack atual
        k2 = k1 + sectors + 1; // Início da próxima stack

        for (int j = 0; j < sectors; ++j, ++k1, ++k2) {
            // Cria 2 triângulos por setor
            if (i != 0) {
                outFaces.push_back({(unsigned int) k1, (unsigned int) k2, (unsigned int) (k1 + 1)});
            }
            if (i != (stacks - 1)) {
                outFaces.push_back({(unsigned int) (k1 + 1), (unsigned int) k2, (unsigned int) (k2 + 1)});
            }
        }
    }
}

// -------------------------------------------------------

namespace {
    // Sets para rastrear teclas pressionadas (permite múltiplas teclas ao mesmo tempo)
    std::set<unsigned char> keysDown;
    std::set<int> specialKeysDown;
    static int lastLeftClickTime = 0; // Para detecção de duplo clique

    void getViewport(int viewport[4]) {
        glGetIntegerv(GL_VIEWPORT, viewport);
    }
}

namespace controls {
    //---------------------------
    // Gerenciamento de Estado do Teclado
    //---------------------------

    // Registra tecla pressionada
    void keyDown(unsigned char key) {
        keysDown.insert(static_cast<unsigned char>(std::tolower(key)));
    }

    // Remove tecla solta
    void keyUp(unsigned char key) {
        keysDown.erase(static_cast<unsigned char>(std::tolower(key)));
    }

    // Lógica de rotação: Atualiza os ângulos de Euler baseados nas teclas WASD
    void updateRotation(float &rotation_x, float &rotation_y) {
        const float rotationStep = 1.0f;
        if (keysDown.count('w')) rotation_x -= rotationStep; // Cima
        if (keysDown.count('s')) rotation_x += rotationStep; // Baixo
        if (keysDown.count('a')) rotation_y -= rotationStep; // Esquerda
        if (keysDown.count('d')) rotation_y += rotationStep; // Direita
    }

    // Lógica de Zoom (Escala)
    void processZoom(float &zoom, unsigned char key, int modifiers) {
        const float zoomStep = 0.05f;
        if (key == '=' || key == '+' || key == 43)
            zoom += zoomStep;
        else if (key == '-' || key == 45)
            zoom -= zoomStep;
    }

    // Gerenciamento de teclas especiais (Setas, F1-F12, etc.)
    void specialKeyDown(int key) {
        specialKeysDown.insert(key);
    }

    void specialKeyUp(int key) {
        specialKeysDown.erase(key);
    }

    // Lógica de translação: Move o objeto no plano da tela (X/Y)
    void updateNavigation(float &offset_x, float &offset_y) {
        const float moveStep = 0.05f;
        if (specialKeysDown.count(GLUT_KEY_UP)) offset_y += moveStep;
        if (specialKeysDown.count(GLUT_KEY_DOWN)) offset_y -= moveStep;
        if (specialKeysDown.count(GLUT_KEY_LEFT)) offset_x -= moveStep;
        if (specialKeysDown.count(GLUT_KEY_RIGHT)) offset_x += moveStep;
    }

    //---------------------------
    // CALLBACK PRINCIPAL DE TECLADO
    //---------------------------
    // Esta função é chamada pelo GLUT sempre que uma tecla normal (ASCII) é pressionada.
    void keyboardDownCallback(unsigned char key, int x, int y) {
        int modifiers = glutGetModifiers(); // Verifica SHIFT, CTRL, ALT
        char lowerKey = std::tolower(key);

        // --- DELETE: Remoção de Elementos ---
        if (key == 127) {
            g_object->deleteSelectedElements();
            glutPostRedisplay();
        }

        // --- 'P': Alternar para PATH TRACING ---
        else if (lowerKey == 'p') {
            g_pathTracingMode = !g_pathTracingMode;

            if (g_pathTracingMode) {
                std::cout << "Path Tracing Ativado! Sincronizando malha, materiais e texturas..." << std::endl;

                // 1. Coleta dados do objeto atual
                const auto &currentVertices = g_object->getVertices();
                const auto &currentFaces = g_object->getFaces();

                // Coleta dados de textura e materiais
                const auto &textureCache = g_object->getTextureCache();
                const auto &faceTexMap = g_object->getFaceTextureMap();
                const auto &faceUvMap = g_object->getFaceUvMap();

                if (currentVertices.empty()) {
                    g_pathTracingMode = false;
                    return;
                }

                // 2. Normalização e Escala
                float minX = currentVertices[0][0], maxX = currentVertices[0][0];
                float minY = currentVertices[0][1], maxY = currentVertices[0][1];
                float minZ = currentVertices[0][2], maxZ = currentVertices[0][2];
                for (const auto &v: currentVertices) {
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
                float w = maxX - minX, h = maxY - minY, d = maxZ - minZ;
                float maxDim = std::max(std::max(w, h), d);
                float scale = 2.0f / (maxDim > 0 ? maxDim : 1.0f);

                // 3. Prepara a Cena Estática
                static SceneData scene;
                if (scene.bvhRoot) {
                    delete scene.bvhRoot;
                    scene.bvhRoot = nullptr;
                }

                // Limpa vetores
                scene.vertices.clear();
                scene.faces.clear();
                scene.triIndices.clear();
                scene.textures.clear();
                scene.faceTextureID.clear();
                scene.faceUVs.clear();
                scene.faceMaterials.clear();

                // Copia vértices transformados
                for (const auto &v: currentVertices) {
                    scene.vertices.push_back(Vec3(
                        (v[0] - centerX) * scale,
                        (v[1] - centerY) * scale,
                        (v[2] - centerZ) * scale
                    ));
                }

                // 4. Processamento de Texturas
                std::map<GLuint, int> glToPtMap;
                for (auto const &[glID, rawData]: textureCache) {
                    TextureData tex;
                    tex.width = rawData.width;
                    tex.height = rawData.height;
                    int numPixels = tex.width * tex.height;
                    tex.pixels.resize(numPixels * 3);
                    for (int i = 0; i < numPixels * 3; ++i) {
                        float val = rawData.pixels[i] / 255.0f;
                        tex.pixels[i] = std::pow(val, 2.2f) * 1.3f;
                    }
                    scene.textures.push_back(tex);
                    glToPtMap[glID] = (int) scene.textures.size() - 1;
                }

                // 5. Triangulação de Faces e Atribuição de materiais
                for (size_t fIdx = 0; fIdx < currentFaces.size(); ++fIdx) {
                    const auto &face = currentFaces[fIdx];

                    // Verifica se a face no editor está marcada como transparente
                    int matType = 0; // 0 = Difuso/Padrão
                    if (g_object->isFaceTransparent((int) fIdx)) {
                        matType = 2; // 2 = Vidro/Refração (Convenção)
                    }

                    // Texturas
                    int currentTexID = -1;
                    if (faceTexMap.count(fIdx) && glToPtMap.count(faceTexMap.at(fIdx))) {
                        currentTexID = glToPtMap.at(faceTexMap.at(fIdx));
                    }

                    // UVs
                    std::vector<PtVec2> originalUVs;
                    if (currentTexID != -1 && faceUvMap.count(fIdx)) {
                        for (const auto &uv: faceUvMap.at(fIdx)) {
                            originalUVs.push_back({uv.u, uv.v});
                        }
                    }

                    // --- Triangulação ---

                    if (face.size() == 3) {
                        // Triângulo
                        scene.faces.push_back({face[0], face[1], face[2]});
                        scene.faceTextureID.push_back(currentTexID);
                        scene.faceMaterials.push_back(matType);

                        if (currentTexID != -1 && originalUVs.size() >= 3)
                            scene.faceUVs.push_back({originalUVs[0], originalUVs[1], originalUVs[2]});
                        else scene.faceUVs.push_back({});
                    } else if (face.size() == 4) {
                        // Quadrado (2 Triângulos)
                        // Tri 1
                        scene.faces.push_back({face[0], face[1], face[2]});
                        scene.faceTextureID.push_back(currentTexID);
                        scene.faceMaterials.push_back(matType);
                        if (currentTexID != -1 && originalUVs.size() >= 4)
                            scene.faceUVs.push_back({originalUVs[0], originalUVs[1], originalUVs[2]});
                        else scene.faceUVs.push_back({});

                        // Tri 2
                        scene.faces.push_back({face[0], face[2], face[3]});
                        scene.faceTextureID.push_back(currentTexID);
                        scene.faceMaterials.push_back(matType);
                        if (currentTexID != -1 && originalUVs.size() >= 4)
                            scene.faceUVs.push_back({originalUVs[0], originalUVs[2], originalUVs[3]});
                        else scene.faceUVs.push_back({});
                    }
                }

                // 6. Constrói BVH e Inicia
                std::cout << "Construindo BVH..." << std::endl;
                buildBVH(scene);
                g_renderMesh = &scene;

                int winW = glutGet(GLUT_WINDOW_WIDTH);
                int winH = glutGet(GLUT_WINDOW_HEIGHT);
                initPathTracingTexture(winW, winH);
            } else {
                g_renderMesh = nullptr;
            }
            glutPostRedisplay();
        }

        // --- 'A': Seleção Inteligente ---
        else if (lowerKey == 'a') {
            // SHIFT + A: Selecionar o objeto conectado
            if (modifiers & GLUT_ACTIVE_SHIFT) {
                if (g_object->getSelectedFaces().empty()) {
                    std::cout << "Selecione pelo menos uma face antes de usar Shift+A." << std::endl;
                } else {
                    int seedFace = g_object->getSelectedFaces().front();
                    bool groupSelected = false;

                    // Metodo 1: Seleção por Grupo Lógico (definido no arquivo OBJ)
                    const auto &cells = g_object->getFaceCells();
                    if (!cells.empty() && seedFace < cells.size()) {
                        unsigned int groupID = cells[seedFace];
                        if (groupID != 0xFFFFFFFF) {
                            std::cout << "Selecionando por Grupo definido no arquivo (ID: " << groupID << ")..." <<
                                    std::endl;
                            g_object->selectFacesByGroup(seedFace);
                            groupSelected = true;
                        }
                    }

                    // Metodo 2: Fallback Geométrico (BFS (Busca em largura por faces que compartilham da mesma aresta) / Flood Fill)
                    // Usado se o arquivo não tiver grupos definidos. Percorre a malha pela conectividade.
                    if (!groupSelected) {
                        std::cout << "Grupo nao detectado. Usando topologia geometrica (BFS)..." << std::endl;
                        g_object->updateConnectivity();

                        const auto &adjList = g_object->getFaceAdjacency();
                        int numFaces = (int) g_object->getFaces().size();

                        std::vector<bool> visited(numFaces, false);
                        std::queue<int> q;

                        // Adiciona seleção atual na fila
                        for (int fIdx: g_object->getSelectedFaces()) {
                            if (fIdx < numFaces) {
                                visited[fIdx] = true;
                                q.push(fIdx);
                            }
                        }

                        // Expansão em largura (Breadth-First Search)
                        while (!q.empty()) {
                            int current = q.front();
                            q.pop();
                            if (current < 0 || current >= adjList.size()) continue;

                            for (int neighbor: adjList[current]) {
                                if (neighbor >= 0 && neighbor < numFaces && !visited[neighbor]) {
                                    visited[neighbor] = true;
                                    q.push(neighbor);
                                    g_object->getSelectedFaces().push_back(neighbor);
                                    g_object->setFaceColor(neighbor, {1.0f, 0.0f, 0.0f});
                                }
                            }
                        }
                        std::cout << "Concluido (Geometria)." << std::endl;
                    }
                }
            } else {
                keyDown(key);
            }
            glutPostRedisplay();
        }

        // --- 'T': Aplicar Textura ---
        else if (lowerKey == 't') {
            if (modifiers & GLUT_ACTIVE_SHIFT) {
                // --- MODO TRANSPARÊNCIA (VIDRO/ÁGUA) ---
                if (!g_object->getSelectedFaces().empty()) {
                    std::cout << "Definindo faces selecionadas como TRANSPARENTES (Vidro)..." << std::endl;
                    float ior = 1.5f;
                    g_object->setTransparentMaterialForSelectedFaces(true, ior);
                    glutPostRedisplay();
                } else {
                    std::cout << "Selecione faces para aplicar transparencia." << std::endl;
                }
            } else {
                // --- MODO TEXTURA ---
                if (!g_object->getSelectedFaces().empty()) {
                    const char *filepath = tinyfd_openFileDialog(
                        "Selecionar Textura", "", 2,
                        (const char *[]){"*.png", "*.jpg"}, "Imagens", 0
                    );

                    if (filepath) {
                        // Aplica a textura nas faces selecionadas
                        // Função interna do objeto cuida de calcular a projeção UV global
                        g_object->applyTextureToSelectedFaces(std::string(filepath));
                        glutPostRedisplay();
                    }
                } else {
                    std::cout << "Selecione uma ou mais faces para aplicar textura." << std::endl;
                }
            }
        } else if (lowerKey == 'r') {
            if (!g_object->getSelectedFaces().empty()) {
                std::cout << "Resetando faces selecionadas para o padrao (Cinza Solido)..." << std::endl;

                //Chama a função de limpeza na classe objeto
                g_object->resetSelectedFacesToDefault();

                g_object->updateVBOs();

                glutPostRedisplay();
            } else {
                std::cout << "Selecione faces para resetar (tecla R)." << std::endl;
            }
            // --- 'K': Seleção de Adjacência (Vértices) ---
        } else if (lowerKey == 'k') {
            if (!g_object->getSelectedVertices().empty()) {
                int baseVertex = g_object->getSelectedVertices().front();
                g_object->selectAdjacentVertices(baseVertex);
            }
            // Seleciona vértices que compõem uma face
            else if (!g_object->getSelectedFaces().empty()) {
                int faceIndex = g_object->getSelectedFaces().front();
                g_object->selectVerticesFromFace(faceIndex);
            }
        }

        // --- 'L': Seleção de Adjacência (Faces) ---
        else if (lowerKey == 'l') {
            // Seleciona faces conectadas a um vértice
            if (!g_object->getSelectedVertices().empty()) {
                int baseVertex = g_object->getSelectedVertices().front();
                g_object->selectFacesFromVertex(baseVertex);
            }
            // Seleciona vizinhos de uma face
            else if (!g_object->getSelectedFaces().empty()) {
                int baseFace = g_object->getSelectedFaces().front();
                g_object->selectNeighborFacesFromFace(baseFace);
            }
        }

        // --- 'V': Modo Apenas Vértices (Nuvem de Pontos) ---
        else if (lowerKey == 'v') {
            g_vertex_only_mode = !g_vertex_only_mode;
            if (g_vertex_only_mode) g_face_only_mode = false;
            std::cout << "Modo apenas vertices: " << (g_vertex_only_mode ? "ativado" : "desativado") << std::endl;
        }

        // --- 'F': Criar Face / Modo Apenas Faces ---
        else if (lowerKey == 'f') {
            // Se houver 3 ou 4 vértices selecionados, tenta criar uma face entre eles
            if (g_object->getSelectedVertices().size() == 3 || g_object->getSelectedVertices().size() == 4)
                g_object->createFaceFromSelectedVertices();
                // Caso contrário, alterna o modo de visualização "Wireframe vs Sólido"
            else if (g_object->getSelectedFaces().size() < 3 || g_object->getSelectedFace() == -1) {
                g_face_only_mode = !g_face_only_mode;
                if (g_face_only_mode) g_vertex_only_mode = false;
                std::cout << "Modo apenas faces: " << (g_face_only_mode ? "ativado" : "desativado") << std::endl;
            }
        }

        // --- 'N': Criar Vértice ---
        else if (lowerKey == 'n') {
            // Cria vértice no centroide das faces selecionadas
            if (!g_object->getSelectedFaces().empty())
                g_object->createVertexAndLinkToSelectedFaces();
                // Cria vértice no centroide dos vértices selecionados
            else if (!g_object->getSelectedVertices().empty() && g_object->getSelectedVertices().size() <= 3)
                g_object->createVertexAndLinkToSelected();
                // Abre diálogo para criar vértice via coordenadas manuais
            else if (g_object->getSelectedVertices().empty() && g_object->getSelectedFaces().empty() && g_object->
                     getSelectedFace() == -1)
                g_object->createVertexFromDialog();
        }

        // --- 'B': Salvar Arquivo (Backup/Export) ---
        else if (lowerKey == 'b') {
            const char *saveFilename = tinyfd_saveFileDialog(
                "Salvar Arquivo", "modelo", 4,
                (const char *[]){"OFF files *.off", "OBJ files *.obj", "STL files *.stl", "VTK files *.vtk"},
                "Formatos Suportados"
            );
            if (saveFilename) {
                try {
                    fileio::save_file(saveFilename, g_object->getVertices(), g_object->getFaces());
                    std::cout << "Arquivo salvo com sucesso: " << saveFilename << std::endl;
                } catch (const std::exception &e) {
                    std::cerr << "Erro ao salvar o arquivo: " << e.what() << std::endl;
                }
            }
        } // --- 'E': Criar Esfera ---
        else if (lowerKey == 'e') {
            const char *resRadius = tinyfd_inputBox("Nova Esfera", "Raio:", "1.0");
            if (!resRadius) return;
            std::string strRadius = resRadius;

            const char *resX = tinyfd_inputBox("Nova Esfera", "Centro X:", "0.0");
            if (!resX) return;
            std::string strX = resX;

            const char *resY = tinyfd_inputBox("Nova Esfera", "Centro Y:", "0.0");
            if (!resY) return;
            std::string strY = resY;

            const char *resZ = tinyfd_inputBox("Nova Esfera", "Centro Z:", "0.0");
            if (!resZ) return;
            std::string strZ = resZ;

            float r, cx, cy, cz;

            if (sscanf(strRadius.c_str(), "%f", &r) == 1 &&
                sscanf(strX.c_str(), "%f", &cx) == 1 &&
                sscanf(strY.c_str(), "%f", &cy) == 1 &&
                sscanf(strZ.c_str(), "%f", &cz) == 1) {
                std::cout << "Gerando esfera..." << std::endl;

                // A. Copia os dados atuais
                std::vector<std::array<float, 3> > newVertices = g_object->getVertices();
                std::vector<std::vector<unsigned int> > newFaces = g_object->getFaces();
                std::vector<unsigned int> newCells = g_object->getFaceCells();

                // B. Calcula índice
                int startIndex = (int) newVertices.size();

                // C. Gera geometria
                generateSphereGeometry(r, cx, cy, cz, 20, 20, newVertices, newFaces, startIndex);

                // D. Ajusta grupos
                if (!newCells.empty()) {
                    size_t facesAdicionadas = newFaces.size() - g_object->getFaces().size();
                    for (size_t i = 0; i < facesAdicionadas; ++i) newCells.push_back(9999);
                }

                // E. Substituição
                delete g_object;

                // Cria novo objeto
                g_object = new object::Object(
                    {0.0f, 0.0f, 0.0f},
                    newVertices,
                    newFaces,
                    newCells,
                    "scene_edited.obj",
                    1,
                    true
                );

                std::cout << "Esfera criada em (" << cx << "," << cy << "," << cz << ") com raio " << r << std::endl;
                glutPostRedisplay();
            }
        } else {
            processZoom(g_zoom, key, modifiers);
            keyDown(key);
        }
        glutPostRedisplay();
    }

    void keyboardUpCallback(unsigned char key, int x, int y) {
        keyUp(key);
    }

    // Callbacks para teclas especiais (Setas, F1, etc.)
    void specialKeyboardDownCallback(int key, int x, int y) {
        specialKeyDown(key);
    }

    void specialKeyboardUpCallback(int key, int x, int y) {
        specialKeyUp(key);
    }

    //---------------------------
    // CALLBACK DE MOUSE (Picking / Ray Casting)
    //---------------------------
    void mouseCallback(int button, int state, int x, int y) {
        if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {

            // 1. Obter dados comuns (Tempo e Viewport)
            int currentTime = glutGet(GLUT_ELAPSED_TIME);
            int threshold = 300; // milissegundos
            int viewport[4];
            getViewport(viewport);

            // ---------------------------------------------------------
            // 2. Lógica de Duplo Clique (Edição de Vértice)
            // ---------------------------------------------------------
            if (!g_face_only_mode && (currentTime - lastLeftClickTime < threshold)) {
                int vertexIndex = g_object->pickVertex(x, y, viewport);
                if (vertexIndex >= 0) {
                    std::cout << "Duplo clique no vértice " << vertexIndex << std::endl;
                    g_object->editVertexCoordinates(vertexIndex);
                    g_object->setVertexColor(vertexIndex, {0.0f, 1.0f, 0.0f});
                    glutPostRedisplay();
                    lastLeftClickTime = currentTime;
                    return;
                }
            }
            lastLeftClickTime = currentTime;

            // ---------------------------------------------------------
            // 3. Lógica de Seleção Simples (Picking)
            // ---------------------------------------------------------
            int modifiers = glutGetModifiers();
            bool multiSelect = (modifiers & GLUT_ACTIVE_SHIFT) != 0;

            // Variáveis para armazenar o que foi clicado
            int clickedVertex = -1;
            int clickedFace = -1;
            if (!g_face_only_mode) {
                clickedVertex = g_object->pickVertex(x, y, viewport);
            }
            if (clickedVertex == -1 && !g_vertex_only_mode) {
                clickedFace = g_object->pickFace(x, y, viewport);
            }
            if (!multiSelect) {
                g_object->clearSelection();
            }

            // C. Aplica a nova seleção (se houver)
            if (clickedVertex != -1) {
                g_object->getSelectedVertices().push_back(clickedVertex);
                g_object->setVertexColor(clickedVertex, {1.0f, 0.0f, 0.0f});
                std::cout << "Vértice " << clickedVertex << " selecionado." << std::endl;
            }
            else if (clickedFace != -1) {
                g_object->getSelectedFaces().push_back(clickedFace);
                g_object->setFaceColor(clickedFace, {1.0f, 0.0f, 0.0f});
                std::cout << "Face " << clickedFace << " selecionada." << std::endl;
            }
            else {
                if (!multiSelect) {
                     std::cout << "Nenhum elemento selecionado." << std::endl;
                }
            }
            glutPostRedisplay();
        }
    }
} // namespace controls
