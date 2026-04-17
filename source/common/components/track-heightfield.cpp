#include "track-heightfield.hpp"

#include "../asset-loader.hpp"
#include "../deserialize-utils.hpp"
#include "../ecs/entity.hpp"

#include <tinyobj/tiny_obj_loader.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace our {

    bool TrackHeightfieldComponent::barycentric2D(
        const glm::vec2& a,
        const glm::vec2& b,
        const glm::vec2& c,
        const glm::vec2& p,
        float& w0,
        float& w1,
        float& w2
    ) {
        // Compute barycentric coordinates in 2D (x,z) plane.
        const glm::vec2 v0 = b - a;
        const glm::vec2 v1 = c - a;
        const glm::vec2 v2 = p - a;

        const float d00 = glm::dot(v0, v0);
        const float d01 = glm::dot(v0, v1);
        const float d11 = glm::dot(v1, v1);
        const float d20 = glm::dot(v2, v0);
        const float d21 = glm::dot(v2, v1);

        const float denom = d00 * d11 - d01 * d01;
        if(std::abs(denom) < 1e-12f) return false;

        w1 = (d11 * d20 - d01 * d21) / denom;
        w2 = (d00 * d21 - d01 * d20) / denom;
        w0 = 1.0f - w1 - w2;

        // A small epsilon helps with edge cases.
        const float eps = -1e-4f;
        return (w0 >= eps && w1 >= eps && w2 >= eps);
    }

    void TrackHeightfieldComponent::buildFromOBJ(const std::string& objPath, const glm::mat4& localToWorld){
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string warn, err;

        // Use the OBJ's directory as the base for MTL resolution.
        std::string baseDir;
        {
            auto slash = objPath.find_last_of("/\\");
            baseDir = (slash == std::string::npos) ? std::string() : objPath.substr(0, slash + 1);
        }

        bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objPath.c_str(), baseDir.c_str());
        if(!warn.empty()) std::cout << "[TrackHeightfieldComponent] WARN: " << warn << std::endl;
        if(!ok){
            std::cerr << "[TrackHeightfieldComponent] Failed to load OBJ: \"" << objPath << "\" error: " << err << std::endl;
            return;
        }

        const size_t vertexCount = attrib.vertices.size() / 3;
        std::vector<glm::vec3> positions;
        positions.resize(vertexCount);

        glm::vec3 mn(std::numeric_limits<float>::infinity());
        glm::vec3 mx(-std::numeric_limits<float>::infinity());

        for(size_t i = 0; i < vertexCount; i++){
            glm::vec3 p(
                attrib.vertices[3 * i + 0],
                attrib.vertices[3 * i + 1],
                attrib.vertices[3 * i + 2]
            );
            glm::vec3 wp = glm::vec3(localToWorld * glm::vec4(p, 1.0f));
            positions[i] = wp;
            mn = glm::min(mn, wp);
            mx = glm::max(mx, wp);
        }

        minX = mn.x;
        minZ = mn.z;

        const float spanX = mx.x - mn.x;
        const float spanZ = mx.z - mn.z;

        width = std::max(1, (int)std::ceil(spanX / cellSize) + 1);
        height = std::max(1, (int)std::ceil(spanZ / cellSize) + 1);

        heights.assign((size_t)width * (size_t)height, -std::numeric_limits<float>::infinity());
        drivable.assign((size_t)width * (size_t)height, 0);

        // Rasterize upward-facing triangles into the grid.
        for(const auto& shape : shapes){
            size_t indexOffset = 0;
            for(size_t face = 0; face < shape.mesh.num_face_vertices.size(); face++){
                const int fv = shape.mesh.num_face_vertices[face];
                if(fv < 3){
                    indexOffset += fv;
                    continue;
                }

                // We only handle triangles. If not triangulated, we just fan-triangulate.
                const tinyobj::index_t i0 = shape.mesh.indices[indexOffset + 0];
                for(int k = 1; k + 1 < fv; k++){
                    const tinyobj::index_t i1 = shape.mesh.indices[indexOffset + k];
                    const tinyobj::index_t i2 = shape.mesh.indices[indexOffset + k + 1];

                    if(i0.vertex_index < 0 || i1.vertex_index < 0 || i2.vertex_index < 0) continue;

                    const glm::vec3 a = positions[(size_t)i0.vertex_index];
                    const glm::vec3 b = positions[(size_t)i1.vertex_index];
                    const glm::vec3 c = positions[(size_t)i2.vertex_index];

                    glm::vec3 n = glm::cross(b - a, c - a);
                    const float nlen = glm::length(n);
                    if(nlen < 1e-8f) continue;
                    n /= nlen;

                    if(n.y < minNormalY) continue;

                    const float triMinX = std::min({a.x, b.x, c.x});
                    const float triMaxX = std::max({a.x, b.x, c.x});
                    const float triMinZ = std::min({a.z, b.z, c.z});
                    const float triMaxZ = std::max({a.z, b.z, c.z});

                    int x0 = (int)std::floor((triMinX - minX) / cellSize);
                    int x1 = (int)std::floor((triMaxX - minX) / cellSize);
                    int z0 = (int)std::floor((triMinZ - minZ) / cellSize);
                    int z1 = (int)std::floor((triMaxZ - minZ) / cellSize);

                    x0 = std::clamp(x0, 0, width - 1);
                    x1 = std::clamp(x1, 0, width - 1);
                    z0 = std::clamp(z0, 0, height - 1);
                    z1 = std::clamp(z1, 0, height - 1);

                    const glm::vec2 a2(a.x, a.z);
                    const glm::vec2 b2(b.x, b.z);
                    const glm::vec2 c2(c.x, c.z);

                    for(int z = z0; z <= z1; z++){
                        for(int x = x0; x <= x1; x++){
                            const float cx = minX + (x + 0.5f) * cellSize;
                            const float cz = minZ + (z + 0.5f) * cellSize;
                            const glm::vec2 p2(cx, cz);

                            float w0, w1, w2;
                            if(!barycentric2D(a2, b2, c2, p2, w0, w1, w2)) continue;

                            const float y = w0 * a.y + w1 * b.y + w2 * c.y;
                            const int idx = index(x, z);
                            if(y > heights[(size_t)idx]){
                                heights[(size_t)idx] = y;
                                drivable[(size_t)idx] = 1;
                            }
                        }
                    }
                }

                indexOffset += fv;
            }
        }

        std::cout << "[TrackHeightfieldComponent] Built heightfield: "
                  << width << "x" << height << " cellSize=" << cellSize
                  << " minNormalY=" << minNormalY << std::endl;
    }

    void TrackHeightfieldComponent::deserialize(const nlohmann::json& data){
        if(!data.is_object()) return;

        cellSize = data.value("cellSize", cellSize);
        minNormalY = data.value("minNormalY", minNormalY);

        std::string objPath = data.value("obj", "");
        if(objPath.empty()){
            const std::string meshAssetName = data.value("mesh", "");
            if(meshAssetName.empty()){
                std::cerr << "[TrackHeightfieldComponent] Missing 'obj' path (or 'mesh' asset name)" << std::endl;
                return;
            }
            const std::string* resolved = getMeshAssetPath(meshAssetName);
            if(resolved == nullptr || resolved->empty()){
                std::cerr << "[TrackHeightfieldComponent] Unknown mesh asset (no source path): \"" << meshAssetName << "\"" << std::endl;
                return;
            }
            objPath = *resolved;
        }

        auto* owner = getOwner();
        if(owner == nullptr) return;

        const glm::mat4 localToWorld = owner->getLocalToWorldMatrix();
        buildFromOBJ(objPath, localToWorld);
    }

    bool TrackHeightfieldComponent::sample(float x, float z, float& outY) const {
        if(width <= 0 || height <= 0) return false;

        const int gx = (int)std::floor((x - minX) / cellSize);
        const int gz = (int)std::floor((z - minZ) / cellSize);

        if(gx < 0 || gx >= width || gz < 0 || gz >= height) return false;

        const int idx = index(gx, gz);
        if(drivable[(size_t)idx] == 0) return false;

        outY = heights[(size_t)idx];
        return std::isfinite(outY);
    }

}
