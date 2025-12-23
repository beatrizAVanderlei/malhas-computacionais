#ifndef OBJECT_H
#define OBJECT_H

#include <vector>
#include <array>
#include <string>
#include <map>
#include <unordered_map>
#include <GL/glew.h>
#include <set>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

// Estruturas auxiliares para Textura e UV
struct Vec2 { float u, v; };

struct RawTextureData {
    int width, height;
    std::vector<unsigned char> pixels;
};

namespace object {

    // Alias para cor
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

        // --- Métodos de Renderização ---
        void draw(const ColorsMap& colors, bool vertexOnlyMode, bool faceOnlyMode);
        void drawTexturedFaces();
        void setShaderProgram(GLuint program) { shaderProgram_ = program; }
        void updateVBOs();

        // --- Métodos de Picking ---
        int pickFace(int mouseX, int mouseY, const int viewport[4]) const;
        int pickVertex(int mouseX, int mouseY, const int viewport[4]) const;

        // --- Métodos de Edição e Seleção ---
        void setFaceColor(int faceIndex, const Color& color);
        void setVertexColor(int vertexIndex, const Color& color);
        void clearSelection();
        void clearColors();
        void deleteSelectedElements();

        void selectAdjacentVertices(int vertexIndex);
        void selectVerticesFromFace(int faceIndex);
        void selectFacesFromVertex(int vertexIndex);
        void selectNeighborFacesFromFace(int faceIndex);
        void selectCellFromSelectedFace(int faceOriginalIndex);
        void selectFacesByGroup(int faceIndex);

        void createFaceFromSelectedVertices();
        void createVertexFromDialog();
        void createVertexAndLinkToSelected();
        void createVertexAndLinkToSelectedFaces();
        void editVertexCoordinates(int vertexIndex);
        void updateConnectivity();

        // --- Métodos de Textura ---
        void applyTextureToSelectedFaces(const std::string& filepath);

        // --- Getters ---
        const std::vector<std::array<float, 3>>& getVertices() const { return vertices_; }
        const std::vector<std::vector<unsigned int>>& getFaces() const { return faces_; }
        const std::vector<std::pair<unsigned int, unsigned int>>& getEdges() const { return edges_; }
        const std::vector<unsigned int>& getFaceCells() const { return face_cells_; }

        int getCurrentIndex(int originalIndex) const;

        std::vector<int>& getSelectedFaces() { return selectedFaces; }
        std::vector<int>& getSelectedVertices() { return selectedVertices; }
        int getSelectedFace() const { return selectedFace; }

        const std::vector<std::vector<int>>& getFaceAdjacency() const { return faceAdjacencyMapping; }

        // [CORREÇÃO] Apenas declaração aqui para evitar redefinição
        const std::map<GLuint, RawTextureData>& getTextureCache() const;
        const std::map<int, GLuint>& getFaceTextureMap() const;
        const std::map<int, std::vector<Vec2>>& getFaceUvMap() const;

        std::vector<std::pair<unsigned int, unsigned int>> calculateEdges(const std::vector<std::vector<unsigned int>>& faces);
        std::vector<std::array<unsigned int, 3>> triangulateFaces(const std::vector<std::vector<unsigned int>>& faces) const;
        void setTransparentMaterialForSelectedFaces(bool enable, float ior);
        bool isFaceTransparent(int faceIndex) const;
        void resetSelectedFacesToDefault();

    private:
        void setupVBOs();
        void drawFacesVBO(const Color& defaultColor, bool vertexOnlyMode);
        void drawEdgesVBO(const Color& color);
        void drawVerticesVBO(const Color& defaultColor);

        std::vector<std::vector<int>> computeVertexToFaces() const;
        std::vector<std::vector<int>> computeFaceAdjacency() const;
        GLuint loadTexture(const std::string& filepath);

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

        unsigned int vbo_vertices_ = 0;
        unsigned int ibo_faces_ = 0;
        unsigned int ibo_edges_ = 0;
        GLuint shaderProgram_ = 0;

        std::vector<float> vertex_array_;
        std::vector<unsigned int> face_index_array_;
        std::vector<unsigned int> edge_index_array_;

        mutable std::unordered_map<int, int> faceTriangleMap;
        std::unordered_map<int, int> originalToCurrentIndex;
        // [CORREÇÃO] Variável que faltava
        std::vector<std::vector<unsigned int>> facesOriginais;

        std::vector<int> selectedFaces;
        std::vector<int> selectedVertices;
        int selectedFace;
        int selectedVertex;

        std::vector<std::vector<int>> vertexToFacesMapping;
        std::vector<std::vector<int>> faceAdjacencyMapping;

        std::map<int, GLuint> face_texture_map_;
        std::map<int, std::vector<Vec2>> face_uv_map_;
        std::map<GLuint, RawTextureData> texture_cache_cpu_;
        std::set<int> transparent_faces_;
    };
}
#endif