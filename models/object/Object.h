#ifndef OBJECT_H
#define OBJECT_H

#include <array>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <GL/glew.h>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

namespace object {

    using Color = std::array<float, 3>;
    using ColorsMap = std::map<std::string, Color>;

    class Object {
        public:

            Object(const std::array<float, 3>& position,
                const std::vector<std::array<float, 3>>& vertices,
                const std::vector<std::vector<unsigned int>>& faces,
                const std::vector<unsigned int>& face_cells,
                const std::string& filename,
                int detection_size,
                bool initGl);
            ~Object();

            // Métodos de Renderização
            void draw(const ColorsMap& colors, bool vertexOnlyMode, bool faceOnlyMode);
            void setShaderProgram(GLuint program) { shaderProgram_ = program; }
            void updateVBOs();

            // Métodos de Picking
            int pickFace(int mouseX, int mouseY, const int viewport[4]) const;
            int pickVertex(int mouseX, int mouseY, const int viewport[4]) const;

            // Métodos de Edição
            void setFaceColor(int faceIndex, const Color& color);
            void setVertexColor(int vertexIndex, const Color& color);
            void clearSelection();
            void deleteSelectedElements();
            void selectAdjacentVertices(int vertexIndex);
            void selectVerticesFromFace(int faceIndex);
            void selectFacesFromVertex(int vertexIndex);
            void selectNeighborFacesFromFace(int faceIndex);
            void createFaceFromSelectedVertices();
            void createVertexFromDialog();
            void createVertexAndLinkToSelected();
            void createVertexAndLinkToSelectedFaces();
            void editVertexCoordinates(int vertexIndex);
            void selectCellFromSelectedFace(int faceOriginalIndex);
            void updateConnectivity();
            void selectFacesByGroup(int faceIndex);
            const std::vector<unsigned int>& getFaceCells() const { return face_cells_; }
            int getCurrentIndex(int originalIndex) const;

            // Funções utilitárias
            std::vector<std::pair<unsigned int, unsigned int>> calculateEdges(const std::vector<std::vector<unsigned int>>& faces);
            std::vector<std::array<unsigned int, 3>> triangulateFaces(const std::vector<std::vector<unsigned int>>& faces) const;

            // Getters para seleção e dados
            std::vector<int>& getSelectedFaces() { return selectedFaces; }
            std::vector<int>& getSelectedVertices() { return selectedVertices; }
            int getSelectedFace() const { return selectedFace; }
            const std::vector<std::array<float, 3>>& getVertices() const { return vertices_; }
            const std::vector<std::vector<unsigned int>>& getFaces() const { return faces_; }
            const std::vector<std::pair<unsigned int, unsigned int>>& getEdges() const { return edges_; }
            const std::vector<std::vector<int>>& getFaceAdjacency() const {
                return faceAdjacencyMapping;
            }

            void clearColors() {
                faceColors.clear();
                vertexColors.clear();
            }

        private:
            // Funções auxiliares de renderização
            void setupVBOs();
            void drawFacesVBO(const Color& defaultColor, bool vertexOnlyMode);
            void drawEdgesVBO(const Color& color);
            void drawVerticesVBO(const Color& defaultColor);

            // Dados do objeto
            std::string filename_;
            std::array<float, 3> position_;
            float scale_;
            std::vector<std::array<float, 3>> vertices_;
            std::vector<std::vector<unsigned int>> faces_;
            std::vector<unsigned int> face_cells_;
            int detection_size_;

            std::vector<Color> vertexColors;
            std::vector<Color> faceColors;
            std::vector<std::pair<unsigned int, unsigned int>> edges_;

            // IDs dos buffers na GPU
            unsigned int vbo_vertices_;
            unsigned int ibo_faces_;
            unsigned int ibo_edges_;

            // Dados para os buffers (arrays "achatados")
            std::vector<float> vertex_array_;
            std::vector<unsigned int> face_index_array_;
            std::vector<unsigned int> edge_index_array_;

            // Mapeamento de triângulos para faces originais (usado no picking)
            mutable std::unordered_map<int, int> faceTriangleMap;

            // Listas de seleção
            std::vector<int> selectedFaces;
            std::vector<int> selectedVertices;
            int selectedFace;
            int selectedVertex;

            std::vector<std::vector<int>> vertexToFacesMapping;
            std::vector<std::vector<int>> faceAdjacencyMapping;

            std::vector<std::vector<int>> computeVertexToFaces() const;
            std::vector<std::vector<int>> computeFaceAdjacency() const;

            std::vector<std::vector<unsigned int>> facesOriginais;
            std::unordered_map<int, int> originalToCurrentIndex;

            // Shader program usado para o desenho
            GLuint shaderProgram_ = 0;
    };
}

#endif
