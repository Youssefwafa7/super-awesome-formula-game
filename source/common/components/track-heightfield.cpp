#include "track-heightfield.hpp"

#include "../asset-loader.hpp"
#include "../ecs/entity.hpp"

#include <tinyobj/tiny_obj_loader.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

namespace our {

    namespace {

        enum class FaceSurface {
            Unknown,
            Road,
            Grass,
            Wall
        };

        static std::string toLowerCopy(const std::string& s){
            std::string out;
            out.reserve(s.size());
            for(unsigned char ch : s) out.push_back((char)std::tolower(ch));
            return out;
        }

        static bool containsAnyHint(const std::string& haystackLower, const std::vector<std::string>& hints){
            for(const auto& hint : hints){
                if(hint.empty()) continue;
                if(haystackLower.find(toLowerCopy(hint)) != std::string::npos) return true;
            }
            return false;
        }

        static FaceSurface classifyByNameHints(
            const std::string& shapeName,
            const std::string& materialName,
            const std::string& textureName,
            const std::vector<std::string>& roadHints,
            const std::vector<std::string>& grassHints,
            const std::vector<std::string>& wallHints
        ) {
            const std::string infoLower = toLowerCopy(shapeName + std::string(" ") + materialName + std::string(" ") + textureName);

            if(containsAnyHint(infoLower, wallHints)) return FaceSurface::Wall;
            if(containsAnyHint(infoLower, grassHints)) return FaceSurface::Grass;
            if(containsAnyHint(infoLower, roadHints)) return FaceSurface::Road;
            return FaceSurface::Unknown;
        }

        static FaceSurface classifyFromColor(const glm::vec3& c){
            const float r = std::clamp(c.r, 0.0f, 1.0f);
            const float g = std::clamp(c.g, 0.0f, 1.0f);
            const float b = std::clamp(c.b, 0.0f, 1.0f);

            // Fallback: green-dominant diffuse colors are treated as grass.
            if(g > 0.18f && g > r * 1.08f && g > b * 1.08f) return FaceSurface::Grass;

            // Default unknown colors to road-like behavior.
            return FaceSurface::Road;
        }

    }

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

    bool TrackHeightfieldComponent::nearestDrivableHeight(int gx, int gz, float& outY) const {
        float bestDist2 = std::numeric_limits<float>::infinity();
        bool found = false;

        for(int r = 1; r <= grassSearchCells; r++){
            for(int dz = -r; dz <= r; dz++){
                for(int dx = -r; dx <= r; dx++){
                    const int x = gx + dx;
                    const int z = gz + dz;
                    if(x < 0 || x >= width || z < 0 || z >= height) continue;

                    const int idx = index(x, z);
                    if(drivable[(size_t)idx] == 0) continue;

                    const float y = heights[(size_t)idx];
                    if(!std::isfinite(y)) continue;

                    const float dist2 = (float)(dx * dx + dz * dz);
                    if(dist2 < bestDist2){
                        bestDist2 = dist2;
                        outY = y;
                        found = true;
                    }
                }
            }
            if(found) break;
        }

        return found;
    }

    bool TrackHeightfieldComponent::resolveWallCollision(glm::vec2& position, float radius, float pushBack, int iterations) const {
        if(radius <= 0.0f || wallSegments.empty() || iterations <= 0) return false;

        const float r = std::max(0.0f, radius + wallCollisionMargin);
        const float r2 = r * r;
        bool collidedAny = false;

        for(int iter = 0; iter < iterations; iter++){
            bool collidedThisIter = false;

            for(const auto& seg : wallSegments){
                const float minX = std::min(seg.a.x, seg.b.x) - r;
                const float maxX = std::max(seg.a.x, seg.b.x) + r;
                const float minZ = std::min(seg.a.y, seg.b.y) - r;
                const float maxZ = std::max(seg.a.y, seg.b.y) + r;
                if(position.x < minX || position.x > maxX || position.y < minZ || position.y > maxZ) continue;

                const glm::vec2 ab = seg.b - seg.a;
                const float abLen2 = glm::dot(ab, ab);
                float t = 0.0f;
                if(abLen2 > 1e-8f){
                    t = glm::clamp(glm::dot(position - seg.a, ab) / abLen2, 0.0f, 1.0f);
                }
                const glm::vec2 closest = seg.a + ab * t;

                glm::vec2 delta = position - closest;
                float dist2 = glm::dot(delta, delta);
                if(dist2 >= r2) continue;

                float dist = std::sqrt(std::max(1e-8f, dist2));
                glm::vec2 normal;
                if(dist > 1e-4f){
                    normal = delta / dist;
                } else {
                    if(std::abs(ab.x) > std::abs(ab.y)){
                        normal = glm::vec2(0.0f, (position.y >= closest.y) ? 1.0f : -1.0f);
                    } else {
                        normal = glm::vec2((position.x >= closest.x) ? 1.0f : -1.0f, 0.0f);
                    }
                    dist = 0.0f;
                }

                const float penetration = r - dist;
                if(penetration > 0.0f){
                    position += normal * penetration;
                    collidedThisIter = true;
                    collidedAny = true;
                }
            }

            if(!collidedThisIter) break;
        }

        if(collidedAny && pushBack > 0.0f){
            float bestDist2 = std::numeric_limits<float>::infinity();
            glm::vec2 bestNormal(0.0f, 0.0f);

            for(const auto& seg : wallSegments){
                const glm::vec2 ab = seg.b - seg.a;
                const float abLen2 = glm::dot(ab, ab);
                float t = 0.0f;
                if(abLen2 > 1e-8f){
                    t = glm::clamp(glm::dot(position - seg.a, ab) / abLen2, 0.0f, 1.0f);
                }
                const glm::vec2 closest = seg.a + ab * t;
                const glm::vec2 d = position - closest;
                const float d2 = glm::dot(d, d);
                if(d2 < bestDist2){
                    bestDist2 = d2;
                    if(d2 > 1e-8f) bestNormal = glm::normalize(d);
                }
            }

            if(glm::dot(bestNormal, bestNormal) > 0.0f){
                position += bestNormal * pushBack;
            }
        }

        return collidedAny;
    }

    bool TrackHeightfieldComponent::projectToNearestDrivable(glm::vec2& position, int maxSearchCells) const {
        if(width <= 0 || height <= 0) return false;

        const int gx = (int)std::floor((position.x - minX) / cellSize);
        const int gz = (int)std::floor((position.y - minZ) / cellSize);
        if(gx < 0 || gx >= width || gz < 0 || gz >= height) return false;

        auto isGoodCell = [&](int x, int z){
            if(x < 0 || x >= width || z < 0 || z >= height) return false;
            const int idx = index(x, z);
            if(wall[(size_t)idx] != 0) return false;
            if(drivable[(size_t)idx] == 0) return false;
            return std::isfinite(heights[(size_t)idx]);
        };

        if(isGoodCell(gx, gz)) return true;

        const int radiusLimit = std::max(1, maxSearchCells);
        float bestDist2 = std::numeric_limits<float>::infinity();
        int bestX = -1;
        int bestZ = -1;

        for(int r = 1; r <= radiusLimit; r++){
            for(int dz = -r; dz <= r; dz++){
                for(int dx = -r; dx <= r; dx++){
                    const int x = gx + dx;
                    const int z = gz + dz;
                    if(!isGoodCell(x, z)) continue;

                    const float d2 = (float)(dx * dx + dz * dz);
                    if(d2 < bestDist2){
                        bestDist2 = d2;
                        bestX = x;
                        bestZ = z;
                    }
                }
            }
            if(bestX >= 0) break;
        }

        if(bestX < 0) return false;

        position.x = minX + ((float)bestX + 0.5f) * cellSize;
        position.y = minZ + ((float)bestZ + 0.5f) * cellSize;
        return true;
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
        surfaceType.assign((size_t)width * (size_t)height, (uint8_t)SurfaceType::Road);
        wall.assign((size_t)width * (size_t)height, 0);

        auto markWallTriangle = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c){
            const glm::vec2 a2(a.x, a.z);
            const glm::vec2 b2(b.x, b.z);
            const glm::vec2 c2(c.x, c.z);

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

            const float area2 = std::abs((b2.x - a2.x) * (c2.y - a2.y) - (b2.y - a2.y) * (c2.x - a2.x));

            // Degenerate projected triangles are common for vertical walls.
            // Rasterize as a thin line along the longest projected edge.
            // This avoids very wide false wall regions from bbox filling.
            if(area2 < 1e-6f){
                const glm::vec2 p[3] = {a2, b2, c2};

                int i0 = 0, i1 = 1;
                float bestLen2 = glm::dot(p[1] - p[0], p[1] - p[0]);
                const float l12 = glm::dot(p[2] - p[1], p[2] - p[1]);
                const float l20 = glm::dot(p[0] - p[2], p[0] - p[2]);
                if(l12 > bestLen2){ bestLen2 = l12; i0 = 1; i1 = 2; }
                if(l20 > bestLen2){ bestLen2 = l20; i0 = 2; i1 = 0; }

                const glm::vec2 s0 = p[i0];
                const glm::vec2 s1 = p[i1];
                const glm::vec2 seg = s1 - s0;

                // Keep walls approximately one cell thick in the degenerate case.
                const float lineRadius = std::max(0.5f * cellSize, 1e-4f);
                const float lineRadius2 = lineRadius * lineRadius;

                if(bestLen2 < 1e-10f){
                    const int gx = (int)std::floor((s0.x - minX) / cellSize);
                    const int gz = (int)std::floor((s0.y - minZ) / cellSize);
                    if(gx >= 0 && gx < width && gz >= 0 && gz < height){
                        wall[(size_t)index(gx, gz)] = 1;
                    }
                    return;
                }

                const float minLX = std::min(s0.x, s1.x) - lineRadius;
                const float maxLX = std::max(s0.x, s1.x) + lineRadius;
                const float minLZ = std::min(s0.y, s1.y) - lineRadius;
                const float maxLZ = std::max(s0.y, s1.y) + lineRadius;

                int lx0 = (int)std::floor((minLX - minX) / cellSize);
                int lx1 = (int)std::floor((maxLX - minX) / cellSize);
                int lz0 = (int)std::floor((minLZ - minZ) / cellSize);
                int lz1 = (int)std::floor((maxLZ - minZ) / cellSize);

                lx0 = std::clamp(lx0, 0, width - 1);
                lx1 = std::clamp(lx1, 0, width - 1);
                lz0 = std::clamp(lz0, 0, height - 1);
                lz1 = std::clamp(lz1, 0, height - 1);

                for(int z = lz0; z <= lz1; z++){
                    for(int x = lx0; x <= lx1; x++){
                        const float cx = minX + (x + 0.5f) * cellSize;
                        const float cz = minZ + (z + 0.5f) * cellSize;
                        const glm::vec2 cp(cx, cz);

                        const float t = glm::clamp(glm::dot(cp - s0, seg) / bestLen2, 0.0f, 1.0f);
                        const glm::vec2 closest = s0 + seg * t;
                        const glm::vec2 d = cp - closest;
                        if(glm::dot(d, d) <= lineRadius2){
                            wall[(size_t)index(x, z)] = 1;
                        }
                    }
                }
                return;
            }

            for(int z = z0; z <= z1; z++){
                for(int x = x0; x <= x1; x++){
                    const float cx = minX + (x + 0.5f) * cellSize;
                    const float cz = minZ + (z + 0.5f) * cellSize;
                    const glm::vec2 p2(cx, cz);

                    float w0, w1, w2;
                    if(!barycentric2D(a2, b2, c2, p2, w0, w1, w2)) continue;
                    wall[(size_t)index(x, z)] = 1;
                }
            }
        };

        // Rasterize triangles into road/grass heights and wall occupancy.
        for(const auto& shape : shapes){
            size_t indexOffset = 0;
            for(size_t face = 0; face < shape.mesh.num_face_vertices.size(); face++){
                const int fv = shape.mesh.num_face_vertices[face];
                if(fv < 3){
                    indexOffset += fv;
                    continue;
                }

                const int materialID = (face < shape.mesh.material_ids.size()) ? shape.mesh.material_ids[face] : -1;
                std::string materialName;
                std::string textureName;
                glm::vec3 materialDiffuse(1.0f);

                if(materialID >= 0 && materialID < (int)materials.size()){
                    const auto& mat = materials[(size_t)materialID];
                    materialName = mat.name;
                    textureName = mat.diffuse_texname;
                    materialDiffuse = glm::vec3(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]);
                }

                const FaceSurface hintSurface = classifyByNameHints(
                    shape.name,
                    materialName,
                    textureName,
                    roadTextureHints,
                    grassTextureHints,
                    wallTextureHints
                );

                // We only handle triangles. If not triangulated, we just fan-triangulate.
                const tinyobj::index_t i0 = shape.mesh.indices[indexOffset + 0];
                for(int k = 1; k + 1 < fv; k++){
                    const tinyobj::index_t i1 = shape.mesh.indices[indexOffset + k];
                    const tinyobj::index_t i2 = shape.mesh.indices[indexOffset + k + 1];

                    if(i0.vertex_index < 0 || i1.vertex_index < 0 || i2.vertex_index < 0) continue;

                    const glm::vec3 a = positions[(size_t)i0.vertex_index];
                    const glm::vec3 b = positions[(size_t)i1.vertex_index];
                    const glm::vec3 c = positions[(size_t)i2.vertex_index];

                    if(hintSurface == FaceSurface::Wall){
                        markWallTriangle(a, b, c);
                        continue;
                    }

                    glm::vec3 n = glm::cross(b - a, c - a);
                    const float nlen = glm::length(n);
                    if(nlen < 1e-8f) continue;
                    n /= nlen;

                    // Optional geometry-driven hard wall detection for meshes that don't have wall-like names.
                    // Only consider near-vertical triangles when this feature is enabled.
                    if(hardWallNormalY > 0.0f && hintSurface == FaceSurface::Unknown && std::abs(n.y) <= hardWallNormalY){
                        markWallTriangle(a, b, c);
                        continue;
                    }

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

                            FaceSurface cellSurface = hintSurface;
                            if(cellSurface == FaceSurface::Unknown){
                                cellSurface = classifyFromColor(materialDiffuse);
                            }

                            if(cellSurface == FaceSurface::Wall){
                                wall[(size_t)index(x, z)] = 1;
                                continue;
                            }

                            if(cellSurface == FaceSurface::Unknown){
                                cellSurface = FaceSurface::Road;
                            }

                            const float y = w0 * a.y + w1 * b.y + w2 * c.y;
                            const int idx = index(x, z);
                            if(y > heights[(size_t)idx]){
                                heights[(size_t)idx] = y;
                                drivable[(size_t)idx] = 1;
                                surfaceType[(size_t)idx] = (cellSurface == FaceSurface::Grass)
                                    ? (uint8_t)SurfaceType::Grass
                                    : (uint8_t)SurfaceType::Road;
                            }
                        }
                    }
                }

                indexOffset += fv;
            }
        }

        wallSegments.clear();
        if(buildWallSegments && !wall.empty()){
            std::vector<uint8_t> collisionMask = wall;
            if(wallCollisionDilateCells > 0){
                std::vector<uint8_t> dilated = collisionMask;
                for(int z = 0; z < height; z++){
                    for(int x = 0; x < width; x++){
                        if(collisionMask[(size_t)index(x, z)] == 0) continue;
                        for(int dz = -wallCollisionDilateCells; dz <= wallCollisionDilateCells; dz++){
                            for(int dx = -wallCollisionDilateCells; dx <= wallCollisionDilateCells; dx++){
                                const int nx = x + dx;
                                const int nz = z + dz;
                                if(nx < 0 || nx >= width || nz < 0 || nz >= height) continue;
                                dilated[(size_t)index(nx, nz)] = 1;
                            }
                        }
                    }
                }
                collisionMask.swap(dilated);
            }

            auto isWall = [&](int x, int z){
                if(x < 0 || x >= width || z < 0 || z >= height) return false;
                return collisionMask[(size_t)index(x, z)] != 0;
            };

            for(int z = 0; z < height; z++){
                for(int x = 0; x < width; x++){
                    if(!isWall(x, z)) continue;

                    const float x0 = minX + (float)x * cellSize;
                    const float x1 = x0 + cellSize;
                    const float z0 = minZ + (float)z * cellSize;
                    const float z1 = z0 + cellSize;

                    if(!isWall(x - 1, z)) wallSegments.push_back({glm::vec2(x0, z0), glm::vec2(x0, z1)});
                    if(!isWall(x + 1, z)) wallSegments.push_back({glm::vec2(x1, z0), glm::vec2(x1, z1)});
                    if(!isWall(x, z - 1)) wallSegments.push_back({glm::vec2(x0, z0), glm::vec2(x1, z0)});
                    if(!isWall(x, z + 1)) wallSegments.push_back({glm::vec2(x0, z1), glm::vec2(x1, z1)});
                }
            }
        }

        size_t drivableCount = 0;
        size_t wallCount = 0;
        for(size_t i = 0; i < drivable.size(); i++){
            if(drivable[i]) drivableCount++;
            if(wall[i]) wallCount++;
        }

        std::cout << "[TrackHeightfieldComponent] Built heightfield: "
                  << width << "x" << height << " cellSize=" << cellSize
                  << " minNormalY=" << minNormalY
                  << " drivableCells=" << drivableCount
                  << " wallCells=" << wallCount
                  << " wallSegments=" << wallSegments.size() << std::endl;
    }

    void TrackHeightfieldComponent::deserialize(const nlohmann::json& data){
        if(!data.is_object()) return;

        cellSize = data.value("cellSize", cellSize);
        minNormalY = data.value("minNormalY", minNormalY);
        hardWallNormalY = std::clamp(data.value("hardWallNormalY", hardWallNormalY), 0.0f, 0.5f);
        treatUnknownAsGrass = data.value("treatUnknownAsGrass", treatUnknownAsGrass);
        grassSearchCells = std::max(0, data.value("grassSearchCells", grassSearchCells));
        buildWallSegments = data.value("buildWallSegments", buildWallSegments);
        wallCollisionDilateCells = std::max(0, data.value("wallCollisionDilateCells", wallCollisionDilateCells));
        wallCollisionMargin = std::max(0.0f, data.value("wallCollisionMargin", wallCollisionMargin));

        if(data.contains("roadTextureHints") && data["roadTextureHints"].is_array()){
            roadTextureHints = data["roadTextureHints"].get<std::vector<std::string>>();
        }
        if(data.contains("grassTextureHints") && data["grassTextureHints"].is_array()){
            grassTextureHints = data["grassTextureHints"].get<std::vector<std::string>>();
        }
        if(data.contains("wallTextureHints") && data["wallTextureHints"].is_array()){
            wallTextureHints = data["wallTextureHints"].get<std::vector<std::string>>();
        }

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

    bool TrackHeightfieldComponent::containsXZ(float x, float z) const {
        if(width <= 0 || height <= 0) return false;

        const float maxX = minX + (float)width * cellSize;
        const float maxZ = minZ + (float)height * cellSize;
        return x >= minX && x < maxX && z >= minZ && z < maxZ;
    }

    bool TrackHeightfieldComponent::sampleSurface(float x, float z, float& outY, SurfaceType& outSurface) const {
        if(width <= 0 || height <= 0) return false;

        const int gx = (int)std::floor((x - minX) / cellSize);
        const int gz = (int)std::floor((z - minZ) / cellSize);

        if(gx < 0 || gx >= width || gz < 0 || gz >= height) return false;

        const int idx = index(gx, gz);

        if(wall[(size_t)idx] != 0){
            outSurface = SurfaceType::Wall;
            if(drivable[(size_t)idx] != 0 && std::isfinite(heights[(size_t)idx])){
                outY = heights[(size_t)idx];
            } else {
                outY = 0.0f;
            }
            return true;
        }

        if(drivable[(size_t)idx] != 0){
            outY = heights[(size_t)idx];
            outSurface = (SurfaceType)surfaceType[(size_t)idx];
            return std::isfinite(outY);
        }

        if(treatUnknownAsGrass){
            float y;
            if(nearestDrivableHeight(gx, gz, y)){
                outY = y;
                outSurface = SurfaceType::Grass;
                return true;
            }
        }

        return false;
    }

    bool TrackHeightfieldComponent::sample(float x, float z, float& outY) const {
        SurfaceType surface;
        if(!sampleSurface(x, z, outY, surface)) return false;
        if(surface == SurfaceType::Wall) return false;
        return std::isfinite(outY);
    }

}
