#pragma once

#include "../ecs/component.hpp"

#include <glm/glm.hpp>

#include <limits>
#include <string>
#include <vector>

namespace our {

    // Builds a coarse heightfield/occupancy grid from an OBJ track mesh.
    // The grid is used to keep cars on the surface and prevent leaving the drivable area.
    class TrackHeightfieldComponent : public Component {
    public:
        static std::string getID() { return "Track Heightfield"; }

        // Grid parameters
        float cellSize = 0.5f;
        float minNormalY = 0.6f; // Only triangles whose normal.y >= this are considered drivable

        // Computed grid bounds
        float minX = 0.0f, minZ = 0.0f;
        int width = 0, height = 0;

        // Per-cell data
        std::vector<float> heights;      // y at cell center (max over triangles), -inf if none
        std::vector<uint8_t> drivable;   // 1 if cell has a drivable height

        void deserialize(const nlohmann::json& data) override;

        // Samples the height at world (x,z). Returns false if not drivable.
        bool sample(float x, float z, float& outY) const;

    private:
        void buildFromOBJ(const std::string& objPath, const glm::mat4& localToWorld);

        static bool barycentric2D(
            const glm::vec2& a,
            const glm::vec2& b,
            const glm::vec2& c,
            const glm::vec2& p,
            float& w0,
            float& w1,
            float& w2
        );

        int index(int x, int z) const { return z * width + x; }
    };

}
