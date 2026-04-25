#pragma once

#include "../ecs/component.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace our {

    // Builds a coarse heightfield/occupancy grid from an OBJ track mesh.
    // The grid is used to keep cars on the surface and prevent leaving the drivable area.
    class TrackHeightfieldComponent : public Component {
    public:
        struct WallSegment {
            glm::vec2 a = glm::vec2(0.0f);
            glm::vec2 b = glm::vec2(0.0f);
        };

        enum class SurfaceType : uint8_t {
            Road = 0,
            Grass = 1,
            Wall = 2
        };

        static std::string getID() { return "Track Heightfield"; }

        // Grid parameters
        float cellSize = 0.5f;
        float minNormalY = 0.6f; // Only triangles whose normal.y >= this are considered drivable
        float hardWallNormalY = 0.0f; // if > 0, near-vertical triangles (|normal.y| <= value) are treated as walls

        // If true, cells that are not explicitly marked road/grass/wall are treated as grass
        // using the nearest drivable height. This prevents hard-stops in noisy mesh regions.
        bool treatUnknownAsGrass = true;
        int grassSearchCells = 2;

        // Hard-collision walls are generated as line segments along wall-cell boundaries.
        bool buildWallSegments = true;
        int wallCollisionDilateCells = 1;
        float wallCollisionMargin = 0.03f;

        // Texture/material name hints used for classifying surfaces.
        std::vector<std::string> roadTextureHints = {"road", "asphalt", "track", "line", "concrete", "lane"};
        std::vector<std::string> grassTextureHints = {"grass", "terrain", "ground", "turf", "dirt", "soil"};
        std::vector<std::string> wallTextureHints = {"wall", "barrier", "fence", "guard", "guardrail", "rail", "curb", "kerb", "tire", "tyre"};

        // Computed grid bounds
        float minX = 0.0f, minZ = 0.0f;
        int width = 0, height = 0;

        // Per-cell data
        std::vector<float> heights;         // y at cell center (max over drivable triangles), -inf if none
        std::vector<uint8_t> drivable;      // 1 if cell has a drivable height
        std::vector<uint8_t> surfaceType;   // SurfaceType enum values for drivable cells
        std::vector<uint8_t> wall;          // 1 if this cell is blocked by wall geometry/material
        std::vector<WallSegment> wallSegments;

        void deserialize(const nlohmann::json& data) override;

        // Samples the height at world (x,z). Returns false if not drivable.
        bool sample(float x, float z, float& outY) const;

        // Samples both height and surface class.
        // Returns false only if position is outside the grid or no valid height can be inferred.
        bool sampleSurface(float x, float z, float& outY, SurfaceType& outSurface) const;

        // Returns true if (x, z) lies within the heightfield XY bounds.
        bool containsXZ(float x, float z) const;

        // Hard-collision against generated wall segments in XZ plane.
        // Returns true if penetration was resolved and position was moved.
        bool resolveWallCollision(glm::vec2& position, float radius, float pushBack = 0.0f, int iterations = 3) const;

        // If position lies in wall/non-drivable area, project it to nearest non-wall drivable cell.
        bool projectToNearestDrivable(glm::vec2& position, int maxSearchCells = 32) const;

    private:
        void buildFromModel(const std::string& modelPath, const glm::mat4& localToWorld);

        static bool barycentric2D(
            const glm::vec2& a,
            const glm::vec2& b,
            const glm::vec2& c,
            const glm::vec2& p,
            float& w0,
            float& w1,
            float& w2
        );

        bool nearestDrivableHeight(int gx, int gz, float& outY) const;

        int index(int x, int z) const { return z * width + x; }
    };

}
