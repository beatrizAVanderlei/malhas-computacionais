#include "object.h"
#include "tinyfiledialogs.h"
#include <algorithm>
#include <iostream>
#include <unordered_set>
#include <sstream>
#include <queue>
#include <set>
#include <cmath>
#include <functional>

namespace object {

    // -----------------------
    // Seleção de vertice/faces
    // -----------------------

    void Object::setFaceColor(int faceIndex, const Color& color) {
        if (faceIndex < 0) return;

        if (faceColors.size() != faces_.size())
            faceColors.resize(faces_.size(), Color{0.8f, 0.8f, 0.8f});

        if (faceIndex >= 0 && faceIndex < static_cast<int>(faceColors.size())) {
            faceColors[faceIndex] = color;
            std::cout << "Colorindo face " << faceIndex << " com cor: "
                    << color[0] << ", " << color[1] << ", " << color[2] << std::endl;
            updateVBOs();
            glutPostRedisplay();
        }
    }

    void Object::setVertexColor(int vertexIndex, const Color& color) {
        if (vertexIndex < 0) return;

        if (vertexColors.size() != vertices_.size())
            vertexColors.resize(vertices_.size(), Color{0.0f, 0.0f, 0.0f});

        if (vertexIndex >= 0 && vertexIndex < static_cast<int>(vertexColors.size()))
            vertexColors[vertexIndex] = color;
    }

    void Object::clearSelection() {
        for (int faceIndex : selectedFaces)
            setFaceColor(faceIndex, Color{0.8f, 0.8f, 0.8f});

        selectedFaces.clear();

        for (int vertexIndex : selectedVertices)
            setVertexColor(vertexIndex, Color{0.0f, 0.0f, 0.0f});

        selectedVertices.clear();
    }

    // -----------------------
    // Seleção de celulas a partir de faces
    // -----------------------

    void Object::selectCellFromSelectedFace(int faceOriginalIndex) {
        // Verifica se o índice está dentro dos limites do vetor de faces originais
        if (faceOriginalIndex < 0 || faceOriginalIndex >= static_cast<int>(facesOriginais.size()))
            return;

        int numVertices = facesOriginais[faceOriginalIndex].size();
        // Verifica se a face possui 3 ou 4 vértices
        if (numVertices != 3 && numVertices != 4) {
            std::cout << "A face selecionada não possui 3 ou 4 vértices." << std::endl;
            return;
        }

        // Define os parâmetros da célula conforme o número de vértices
        int cellFaces = (numVertices == 4) ? 6 : 4;  // quantidade de faces na célula
        int divisor   = (numVertices == 4) ? 6 : 4;    // múltiplo para o cálculo da base da célula
        int cellBase  = faceOriginalIndex - (faceOriginalIndex % divisor);

        std::cout << "Selecionando célula com faces originais de índice " << cellBase << " a " << cellBase + cellFaces - 1 << std::endl;

        // Para cada face da célula, converte o índice original para o índice atual e altera a cor
        for (int i = 0; i < cellFaces; ++i) {
            int faceOrigIdx = cellBase + i;
            int currentIndex = getCurrentIndex(faceOrigIdx); // Função que retorna o índice atual a partir do original
            if (currentIndex >= 0 && currentIndex < static_cast<int>(faces_.size())) {
                selectedFaces.push_back(currentIndex);
                setFaceColor(currentIndex, {1.0f, 0.0f, 0.0f});
                std::cout << "Face " << currentIndex << " selecionada na célula." << std::endl;
            }
        }

        updateVBOs();
        glutPostRedisplay();
    }

    // -----------------------
    // Seleções de adjacencia/metodos de estrela
    // -----------------------

    void Object::selectAdjacentVertices(int vertexIndex) {
        // Verifica se o índice do vértice é válido
        if (vertexIndex < 0 || vertexIndex >= static_cast<int>(vertices_.size()))
            return;

        // Utiliza o mapeamento pré-computado: para o vértice 'vertexIndex',
        // obtemos o vetor de índices das faces que o contêm.
        const std::vector<int>& facesWithVertex = vertexToFacesMapping[vertexIndex];

        // Para cada face que contém o vértice:
        for (int faceIndex : facesWithVertex) {
            // Obtém a face correspondente
            const auto& face = faces_[faceIndex];
            // Percorre todos os vértices desta face
            for (unsigned int adjVertex : face) {
                // Se o vértice não for o próprio 'vertexIndex'
                if (adjVertex != static_cast<unsigned int>(vertexIndex)) {
                    // Se o vértice ainda não estiver selecionado, adiciona-o
                    if (std::find(selectedVertices.begin(), selectedVertices.end(), adjVertex) == selectedVertices.end()) {
                        selectedVertices.push_back(adjVertex);
                        setVertexColor(adjVertex, {1.0f, 0.0f, 0.0f});
                        std::cout << "Vértice " << adjVertex << " (adjacente a " << vertexIndex << ") selecionado." << std::endl;
                    }
                }
            }
        }
        updateVBOs();
        glutPostRedisplay();
    }

    void Object::selectVerticesFromFace(int faceIndex) {
        if (faceIndex < 0 || faceIndex >= static_cast<int>(faces_.size()))
            return;
        auto it = std::find(selectedFaces.begin(), selectedFaces.end(), faceIndex);
        if (it != selectedFaces.end()) {
            selectedFaces.erase(it);
            setFaceColor(faceIndex, Color{60.0f/255.0f, 60.0f/255.0f, 60.0f/255.0f});
        }
        const auto &face = faces_[faceIndex];
        for (unsigned int vertexIndex : face) {
            if (std::find(selectedVertices.begin(), selectedVertices.end(), vertexIndex) == selectedVertices.end()) {
                selectedVertices.push_back(vertexIndex);
                setVertexColor(vertexIndex, {1.0f, 0.0f, 0.0f});
                std::cout << "Vértice " << vertexIndex << " da face " << faceIndex << " selecionado." << std::endl;
            }
        }
        updateVBOs();
        glutPostRedisplay();
    }

    void Object::selectFacesFromVertex(int vertexIndex) {
        // Verifica se o índice do vértice é válido
        if (vertexIndex < 0 || vertexIndex >= static_cast<int>(vertices_.size()))
            return;

        // Se o vértice já estiver selecionado, remove-o e restaura sua cor
        auto itVertex = std::find(selectedVertices.begin(), selectedVertices.end(), vertexIndex);
        if (itVertex != selectedVertices.end()) {
            selectedVertices.erase(itVertex);
            setVertexColor(vertexIndex, Color{0.0f, 0.0f, 0.0f});
        }

        // Utiliza o mapeamento pré-computado para obter todas as faces que contêm o vértice
        const std::vector<int>& facesWithVertex = vertexToFacesMapping[vertexIndex];
        for (int faceIndex : facesWithVertex) {
            if (std::find(selectedFaces.begin(), selectedFaces.end(), faceIndex) == selectedFaces.end()) {
                selectedFaces.push_back(faceIndex);
                setFaceColor(faceIndex, {1.0f, 0.0f, 0.0f});
                std::cout << "Face " << faceIndex << " que contém o vértice " << vertexIndex << " selecionada." << std::endl;
            }
        }
        updateVBOs();
        glutPostRedisplay();
    }

    void Object::selectNeighborFacesFromFace(int faceIndex) {
        // Verifica se o índice da face é válido
        if (faceIndex < 0 || faceIndex >= static_cast<int>(faces_.size()))
            return;

        // Obtém as faces vizinhas diretamente do mapeamento pré-computado
        const std::vector<int>& neighborFaces = faceAdjacencyMapping[faceIndex];
        for (int neighborFaceIndex : neighborFaces) {
            if (std::find(selectedFaces.begin(), selectedFaces.end(), neighborFaceIndex) == selectedFaces.end()) {
                selectedFaces.push_back(neighborFaceIndex);
                setFaceColor(neighborFaceIndex, {1.0f, 0.0f, 0.0f});
                std::cout << "Face " << neighborFaceIndex << " vizinha (compartilha aresta) da face "
                          << faceIndex << " selecionada." << std::endl;
            }
        }
        updateVBOs();
        glutPostRedisplay();
    }

    // -----------------------
    // Criação de vertices e faces
    // -----------------------

    void Object::createFaceFromSelectedVertices() {
        if (selectedVertices.size() < 3 || selectedVertices.size() > 4) {
            std::cout << "Número inválido de vértices para criar uma face." << std::endl;
            return;
        }
        std::vector<unsigned int> newFace;
        for (int index : selectedVertices) {
            newFace.push_back(static_cast<unsigned int>(index));
        }
        faces_.push_back(newFace);
        faceColors.push_back(Color{60.0f/255.0f, 60.0f/255.0f, 60.0f/255.0f});
        edges_ = calculateEdges(faces_);
        updateVBOs();
        std::cout << "Nova face criada com " << selectedVertices.size() << " vértices." << std::endl;
        for (int index : selectedVertices) {
            setVertexColor(index, Color{0.0f, 0.0f, 0.0f});
        }
        selectedVertices.clear();
        glutPostRedisplay();
    }

    void Object::createVertexFromDialog() {
        const char* inputX = tinyfd_inputBox("Novo Vértice", "Digite a coordenada X:", "");
        if (!inputX) {
            std::cout << "Operação cancelada." << std::endl;
            return;
        }
        const char* inputY = tinyfd_inputBox("Novo Vértice", "Digite a coordenada Y:", "");
        if (!inputY) {
            std::cout << "Operação cancelada." << std::endl;
            return;
        }
        const char* inputZ = tinyfd_inputBox("Novo Vértice", "Digite a coordenada Z:", "");
        if (!inputZ) {
            std::cout << "Operação cancelada." << std::endl;
            return;
        }
        float x, y, z;
        if (sscanf(inputX, "%f", &x) == 1 && sscanf(inputY, "%f", &y) == 1 && sscanf(inputZ, "%f", &z) == 1) {
            std::array<float, 3> newVertex = { x, y, z };
            vertices_.push_back(newVertex);
            vertexColors.push_back(Color{0.0f, 0.0f, 0.0f});
            updateVBOs();
            glutPostRedisplay();
            std::cout << "Novo vértice criado: (" << x << ", " << y << ", " << z << ")" << std::endl;
        } else {
            std::cout << "Entrada inválida para as coordenadas." << std::endl;
        }
    }

    void Object::createVertexAndLinkToSelected() {
        const char* inputX = tinyfd_inputBox("Novo Vértice", "Digite a coordenada X:", "");
        if (!inputX) {
            std::cout << "Operação cancelada." << std::endl;
            return;
        }
        const char* inputY = tinyfd_inputBox("Novo Vértice", "Digite a coordenada Y:", "");
        if (!inputY) {
            std::cout << "Operação cancelada." << std::endl;
            return;
        }
        const char* inputZ = tinyfd_inputBox("Novo Vértice", "Digite a coordenada Z:", "");
        if (!inputZ) {
            std::cout << "Operação cancelada." << std::endl;
            return;
        }
        float x, y, z;
        if (!(sscanf(inputX, "%f", &x) == 1 && sscanf(inputY, "%f", &y) == 1 && sscanf(inputZ, "%f", &z) == 1)) {
            std::cout << "Entrada inválida para as coordenadas." << std::endl;
            return;
        }

        std::array<float, 3> newVertex = { x, y, z };
        vertices_.push_back(newVertex);
        vertexColors.push_back(Color{0.0f, 0.0f, 0.0f});
        std::cout << "Novo vértice criado: (" << x << ", " << y << ", " << z << ")" << std::endl;
        size_t selCount = selectedVertices.size();
        if (selCount == 2 || selCount == 3) {
            std::vector<unsigned int> newFace;
            for (int idx : selectedVertices) {
                newFace.push_back(static_cast<unsigned int>(idx));
            }
            newFace.push_back(static_cast<unsigned int>(vertices_.size() - 1));
            faces_.push_back(newFace);
            faceColors.push_back(Color{60.0f/255.0f, 60.0f/255.0f, 60.0f/255.0f});
            std::cout << "Nova face criada com " << newFace.size() << " vértices." << std::endl;
            for (int idx : selectedVertices) {
                setVertexColor(idx, Color{0.0f, 0.0f, 0.0f});
            }
            selectedVertices.clear();
        }
        updateVBOs();
        glutPostRedisplay();
    }

    void Object::createVertexAndLinkToSelectedFaces() {
        const char* inputX = tinyfd_inputBox("Novo Vértice", "Digite a coordenada X:", "");
        if (!inputX) {
            std::cout << "Operação cancelada." << std::endl;
            return;
        }
        const char* inputY = tinyfd_inputBox("Novo Vértice", "Digite a coordenada Y:", "");
        if (!inputY) {
            std::cout << "Operação cancelada." << std::endl;
            return;
        }
        const char* inputZ = tinyfd_inputBox("Novo Vértice", "Digite a coordenada Z:", "");
        if (!inputZ) {
            std::cout << "Operação cancelada." << std::endl;
            return;
        }
        float x, y, z;
        if (sscanf(inputX, "%f", &x) != 1 || sscanf(inputY, "%f", &y) != 1 || sscanf(inputZ, "%f", &z) != 1) {
            std::cout << "Entrada inválida para as coordenadas." << std::endl;
            return;
        }
        std::array<float, 3> newVertex = { x, y, z };
        vertices_.push_back(newVertex);
        vertexColors.push_back(Color{0.0f, 0.0f, 0.0f});
        unsigned int newVertexIndex = vertices_.size() - 1;
        std::cout << "Novo vértice criado: (" << x << ", " << y << ", " << z << ")" << std::endl;
        std::vector<std::vector<unsigned int>> novasFaces;
        std::vector<Color> novasFacesCores;
        std::vector<std::vector<unsigned int>> facesRestantes;
        std::vector<Color> coresFacesRestantes;
        for (size_t i = 0; i < faces_.size(); ++i) {
            if (std::find(selectedFaces.begin(), selectedFaces.end(), static_cast<int>(i)) != selectedFaces.end()) {
                const auto& face = faces_[i];
                int n = face.size();
                for (int j = 0; j < n; ++j) {
                    std::vector<unsigned int> faceNova;
                    faceNova.push_back(face[j]);
                    faceNova.push_back(face[(j + 1) % n]);
                    faceNova.push_back(newVertexIndex);
                    novasFaces.push_back(faceNova);
                    novasFacesCores.push_back(Color{60.0f/255.0f, 60.0f/255.0f, 60.0f/255.0f});
                }
            } else {
                facesRestantes.push_back(faces_[i]);
                coresFacesRestantes.push_back(faceColors[i]);
            }
        }
        faces_.clear();
        faceColors.clear();
        faces_.insert(faces_.end(), facesRestantes.begin(), facesRestantes.end());
        faceColors.insert(faceColors.end(), coresFacesRestantes.begin(), coresFacesRestantes.end());
        faces_.insert(faces_.end(), novasFaces.begin(), novasFaces.end());
        faceColors.insert(faceColors.end(), novasFacesCores.begin(), novasFacesCores.end());
        selectedFaces.clear();
        edges_ = calculateEdges(faces_);
        updateVBOs();
        glutPostRedisplay();
        std::cout << "Face(s) subdividida(s) com o novo vértice." << std::endl;
    }

    // -----------------------
    // Edição de vertices
    // -----------------------

    void Object::editVertexCoordinates(int vertexIndex) {
        if (vertexIndex < 0 || vertexIndex >= static_cast<int>(vertices_.size())) {
            std::cout << "Índice de vértice inválido." << std::endl;
            return;
        }
        float currentX = vertices_[vertexIndex][0];
        float currentY = vertices_[vertexIndex][1];
        float currentZ = vertices_[vertexIndex][2];
        char defaultX[32], defaultY[32], defaultZ[32];
        snprintf(defaultX, sizeof(defaultX), "%.3f", currentX);
        snprintf(defaultY, sizeof(defaultY), "%.3f", currentY);
        snprintf(defaultZ, sizeof(defaultZ), "%.3f", currentZ);
        const char* inputX = tinyfd_inputBox("Editar Vértice", "Digite a nova coordenada X:", defaultX);
        if (!inputX) {
            std::cout << "Operação cancelada." << std::endl;
            return;
        }
        const char* inputY = tinyfd_inputBox("Editar Vértice", "Digite a nova coordenada Y:", defaultY);
        if (!inputY) {
            std::cout << "Operação cancelada." << std::endl;
            return;
        }
        const char* inputZ = tinyfd_inputBox("Editar Vértice", "Digite a nova coordenada Z:", defaultZ);
        if (!inputZ) {
            std::cout << "Operação cancelada." << std::endl;
            return;
        }
        float x, y, z;
        if (!(sscanf(inputX, "%f", &x) == 1 && sscanf(inputY, "%f", &y) == 1 && sscanf(inputZ, "%f", &z) == 1)) {
            std::cout << "Entrada inválida para as coordenadas." << std::endl;
            return;
        }
        vertices_[vertexIndex] = { x, y, z };
        std::cout << "Vértice " << vertexIndex << " editado: (" << x << ", " << y << ", " << z << ")" << std::endl;
        updateVBOs();
        glutPostRedisplay();
    }

    // -----------------------
    // Remoção de objetos
    // -----------------------

    void Object::deleteSelectedElements() {
    // =========================================================
    // 1. REMOÇÃO DE FACES
    // =========================================================
    if (!selectedFaces.empty()) {
        std::cout << "Removendo " << selectedFaces.size() << " faces..." << std::endl;

        std::unordered_set<int> facesToDelete(selectedFaces.begin(), selectedFaces.end());

        // Estruturas para a nova lista de faces
        std::vector<std::vector<unsigned int>> newFaces;
        std::vector<Color> newFaceColors;
        std::vector<unsigned int> newFaceCells;
        std::map<int, GLuint> newTextureMap;
        std::map<int, std::vector<Vec2>> newUvMap;

        // Mapeamento para corrigir o Picking
        std::vector<int> oldToNewIndex(faces_.size(), -1);
        int newIndex = 0;

        for (int oldIndex = 0; oldIndex < static_cast<int>(faces_.size()); ++oldIndex) {
            // Se a face NÃO for deletada, copiamos ela
            if (facesToDelete.find(oldIndex) == facesToDelete.end()) {

                // Copia geometria
                newFaces.push_back(faces_[oldIndex]);

                // Copia Cor
                if (oldIndex < static_cast<int>(faceColors.size())) {
                    newFaceColors.push_back(faceColors[oldIndex]);
                } else {
                    newFaceColors.push_back({0.8f, 0.8f, 0.8f});
                }

                // Copia Grupo
                if (oldIndex < static_cast<int>(face_cells_.size())) {
                    newFaceCells.push_back(face_cells_[oldIndex]);
                }

                // Copia Textura
                if (face_texture_map_.count(oldIndex)) {
                    newTextureMap[newIndex] = face_texture_map_[oldIndex];
                    newUvMap[newIndex] = face_uv_map_[oldIndex];
                }

                oldToNewIndex[oldIndex] = newIndex;
                newIndex++;
            }
        }

        // Atualiza Picking Map
        for (auto &pair : originalToCurrentIndex) {
            int currentIdx = pair.second;
            if (currentIdx >= 0 && currentIdx < static_cast<int>(oldToNewIndex.size())) {
                pair.second = oldToNewIndex[currentIdx];
            } else {
                pair.second = -1;
            }
        }

        // Aplica as alterações APENAS NAS FACES
        faces_ = std::move(newFaces);
        faceColors = std::move(newFaceColors);
        face_cells_ = std::move(newFaceCells);
        face_texture_map_ = std::move(newTextureMap);
        face_uv_map_ = std::move(newUvMap);

        selectedFaces.clear();

        // [REMOVIDO] O bloco que limpava "vértices órfãos" foi deletado daqui.
        // Agora os vértices permanecem intactos ao deletar faces.

        // Atualiza GPU e Topologia
        edges_ = calculateEdges(faces_);
        updateConnectivity();
        setupVBOs();

        std::cout << "Faces removidas com sucesso." << std::endl;
        return;
    }

    // =====================================
    // 2. REMOÇÃO DE VÉRTICES SELECIONADOS
    // =====================================
    if (!selectedVertices.empty()) {
        std::unordered_set<int> removed(selectedVertices.begin(), selectedVertices.end());

        std::vector<std::array<float, 3>> newVertices;
        std::vector<Color> newVertexColors;
        std::vector<int> indexMapping(vertices_.size(), -1);

        // 1. Reconstrói lista de vértices (sem os deletados)
        for (int i = 0; i < static_cast<int>(vertices_.size()); i++) {
            if (removed.find(i) == removed.end()) {
                indexMapping[i] = static_cast<int>(newVertices.size());
                newVertices.push_back(vertices_[i]);
                newVertexColors.push_back(vertexColors[i]);
            }
        }

        // 2. Reconstrói faces (removendo as que usavam os vértices deletados)
        std::vector<std::vector<unsigned int>> newFacesVec; // Renomeado para evitar conflito
        std::vector<Color> newFaceColors;
        std::vector<unsigned int> newFaceCells;
        std::map<int, GLuint> newTextureMap;
        std::map<int, std::vector<Vec2>> newUvMap;

        int newFaceIndex = 0;

        for (size_t i = 0; i < faces_.size(); ++i) {
            bool faceIsBroken = false;
            std::vector<unsigned int> updatedFace;

            for (unsigned int idx : faces_[i]) {
                if (removed.count(static_cast<int>(idx))) {
                    faceIsBroken = true;
                    break;
                }
                updatedFace.push_back(static_cast<unsigned int>(indexMapping[idx]));
            }

            if (!faceIsBroken && !updatedFace.empty()) {
                newFacesVec.push_back(updatedFace);

                if (i < faceColors.size()) newFaceColors.push_back(faceColors[i]);
                if (i < face_cells_.size()) newFaceCells.push_back(face_cells_[i]);

                if (face_texture_map_.count(i)) {
                    newTextureMap[newFaceIndex] = face_texture_map_[i];
                    newUvMap[newFaceIndex] = face_uv_map_[i];
                }

                newFaceIndex++;
            }
        }

        // Aplica as mudanças
        vertices_ = std::move(newVertices);
        vertexColors = std::move(newVertexColors);
        faces_ = std::move(newFacesVec);
        faceColors = std::move(newFaceColors);
        face_cells_ = std::move(newFaceCells);
        face_texture_map_ = std::move(newTextureMap);
        face_uv_map_ = std::move(newUvMap);

        edges_ = calculateEdges(faces_);
        updateConnectivity();
        setupVBOs();
        selectedVertices.clear();

        std::cout << "Vértices removidos. Malha atualizada." << std::endl;
    }
}


    // -----------------------
    // Metodos/estruturas auxiliares
    // -----------------------

    struct pair_hash {
        std::size_t operator()(const std::pair<unsigned int, unsigned int>& p) const {
            return std::hash<unsigned int>()(p.first) ^ (std::hash<unsigned int>()(p.second) << 1);
        }
    };


    int Object::getCurrentIndex(int originalIndex) const {
        auto it = originalToCurrentIndex.find(originalIndex);
        if (it != originalToCurrentIndex.end()) {
            return it->second;
        }
        // Caso não encontre, pode retornar -1 ou tratar o erro conforme sua lógica
        return -1;
    }

    // Função que compara duas faces considerando rotações cíclicas e ordem inversa
    bool facesSaoEquivalentes(const std::vector<unsigned int>& faceA, const std::vector<unsigned int>& faceB) {
        // Se as faces não possuem o mesmo número de vértices, elas não podem ser equivalentes
        if (faceA.size() != faceB.size())
            return false;

        int n = faceA.size();
        // Tenta todas as rotações possíveis
        for (int desloc = 0; desloc < n; ++desloc) {
            bool iguais = true;
            // Verifica rotação normal
            for (int i = 0; i < n; ++i) {
                if (faceA[(desloc + i) % n] != faceB[i]) {
                    iguais = false;
                    break;
                }
            }
            if (iguais)
                return true;

            // Verifica também a ordem inversa (rotacionando e invertendo)
            iguais = true;
            for (int i = 0; i < n; ++i) {
                if (faceA[(desloc - i + n) % n] != faceB[i]) {
                    iguais = false;
                    break;
                }
            }
            if (iguais)
                return true;
        }
        return false;
    }

} // namespace object