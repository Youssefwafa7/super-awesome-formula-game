#include "track-heightfield.hpp"

#include "../asset-loader.hpp"
#include "../ecs/entity.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <unordered_map>

namespace our {

    namespace {

        enum class FaceSurface {
            Unknown,
            Road,
            Grass,
            Wall,
            Curb
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
            const std::vector<std::string>& wallHints,
            const std::vector<std::string>& curbHints
        ) {
            const std::string infoLower = toLowerCopy(shapeName + std::string(" ") + materialName + std::string(" ") + textureName);

            // Curb must be checked before wall, since curb names might also match wall hints.
            if(containsAnyHint(infoLower, curbHints)) return FaceSurface::Curb;
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

    static void AccumulateNodes(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform, std::vector<std::pair<aiMesh*, glm::mat4>>& outMeshes, std::vector<std::string>& outNames) {
        glm::mat4 nodeTransform(
            node->mTransformation.a1, node->mTransformation.b1, node->mTransformation.c1, node->mTransformation.d1,
            node->mTransformation.a2, node->mTransformation.b2, node->mTransformation.c2, node->mTransformation.d2,
            node->mTransformation.a3, node->mTransformation.b3, node->mTransformation.c3, node->mTransformation.d3,
            node->mTransformation.a4, node->mTransformation.b4, node->mTransformation.c4, node->mTransformation.d4
        );
        glm::mat4 globalTransform = parentTransform * nodeTransform;

        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            outMeshes.push_back({scene->mMeshes[node->mMeshes[i]], globalTransform});
            outNames.push_back(node->mName.C_Str());
        }

        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            AccumulateNodes(node->mChildren[i], scene, globalTransform, outMeshes, outNames);
        }
    }

    void TrackHeightfieldComponent::buildFromModel(const std::string& modelPath, const glm::mat4& localToWorld){
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(modelPath,
            aiProcess_Triangulate |
            aiProcess_CalcTangentSpace |
            aiProcess_GenSmoothNormals |
            aiProcess_JoinIdenticalVertices |
            aiProcess_OptimizeMeshes |
            aiProcess_ImproveCacheLocality |
            aiProcess_LimitBoneWeights |
            aiProcess_PopulateArmatureData |
            aiProcess_GlobalScale |
            aiProcess_GenUVCoords |
            aiProcess_TransformUVCoords |
            aiProcess_SortByPType);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            std::cerr << "[TrackHeightfieldComponent] Failed to load model: \"" << modelPath << "\" error: " << importer.GetErrorString() << std::endl;
            return;
        }

        std::vector<std::pair<aiMesh*, glm::mat4>> instances;
        std::vector<std::string> instanceNames;
        AccumulateNodes(scene->mRootNode, scene, localToWorld, instances, instanceNames);

        glm::vec3 mn(std::numeric_limits<float>::infinity());
        glm::vec3 mx(-std::numeric_limits<float>::infinity());

        for (const auto& inst : instances) {
            aiMesh* mesh = inst.first;
            const glm::mat4& xform = inst.second;
            for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
                glm::vec4 p(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z, 1.0f);
                glm::vec3 wp = glm::vec3(xform * p);
                mn = glm::min(mn, wp);
                mx = glm::max(mx, wp);
            }
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

        for(size_t i = 0; i < instances.size(); i++){
            aiMesh* mesh = instances[i].first;
            const glm::mat4& xform = instances[i].second;
            std::string shapeName = instanceNames[i];

            std::string materialName;
            std::string textureName;
            glm::vec3 materialDiffuse(1.0f);

            if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < scene->mNumMaterials) {
                aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
                aiString name;
                mat->Get(AI_MATKEY_NAME, name);
                materialName = name.C_Str();

                aiColor3D diffuseColor(1.f, 1.f, 1.f);
                mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor);
                materialDiffuse = glm::vec3(diffuseColor.r, diffuseColor.g, diffuseColor.b);

                aiString texPath;
                if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
                    textureName = texPath.C_Str();
                } else if (mat->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS) {
                    textureName = texPath.C_Str();
                }
            }

            const FaceSurface hintSurface = classifyByNameHints(
                shapeName,
                materialName,
                textureName,
                roadTextureHints,
                grassTextureHints,
                wallTextureHints,
                curbTextureHints
            );

            for(unsigned int f = 0; f < mesh->mNumFaces; f++){
                aiFace face = mesh->mFaces[f];
                if (face.mNumIndices < 3) continue;

                for(unsigned int k = 1; k + 1 < face.mNumIndices; k++){
                    unsigned int i0 = face.mIndices[0];
                    unsigned int i1 = face.mIndices[k];
                    unsigned int i2 = face.mIndices[k + 1];

                    glm::vec4 p0(mesh->mVertices[i0].x, mesh->mVertices[i0].y, mesh->mVertices[i0].z, 1.0f);
                    glm::vec4 p1(mesh->mVertices[i1].x, mesh->mVertices[i1].y, mesh->mVertices[i1].z, 1.0f);
                    glm::vec4 p2(mesh->mVertices[i2].x, mesh->mVertices[i2].y, mesh->mVertices[i2].z, 1.0f);

                    glm::vec3 a = glm::vec3(xform * p0);
                    glm::vec3 b = glm::vec3(xform * p1);
                    glm::vec3 c = glm::vec3(xform * p2);

                    if(hintSurface == FaceSurface::Wall){
                        markWallTriangle(a, b, c);
                        continue;
                    }

                    // Curb: treated as drivable (like grass), not as a wall.
                    FaceSurface effectiveHint = hintSurface;
                    if(effectiveHint == FaceSurface::Curb){
                        effectiveHint = FaceSurface::Grass;
                    }

                    glm::vec3 n = glm::cross(b - a, c - a);
                    const float nlen = glm::length(n);
                    if(nlen < 1e-8f) continue;
                    n /= nlen;

                    if(hardWallNormalY > 0.0f && effectiveHint == FaceSurface::Unknown && std::abs(n.y) <= hardWallNormalY){
                        markWallTriangle(a, b, c);
                        continue;
                    }

                    if(n.y < minNormalY) continue;

                    // Store this drivable triangle for precise surface-normal queries.
                    {
                        DrivableTri tri;
                        tri.v0 = a; tri.v1 = b; tri.v2 = c;
                        tri.normal = n;
                        FaceSurface triSurf = effectiveHint;
                        if(triSurf == FaceSurface::Unknown) triSurf = classifyFromColor(materialDiffuse);
                        if(triSurf == FaceSurface::Unknown) triSurf = FaceSurface::Road;
                        tri.surface = (triSurf == FaceSurface::Grass)
                            ? (uint8_t)SurfaceType::Grass
                            : (uint8_t)SurfaceType::Road;
                        drivableTris.push_back(tri);
                    }

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

                            FaceSurface cellSurface = effectiveHint;
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

        // Build the per-triangle spatial grid for sampleDrivableSurface().
        if(!drivableTris.empty()){
            triGridCellSize = std::max(cellSize * 4.0f, 1.0f);
            triGridMinX = mn.x;
            triGridMinZ = mn.z;
            const float spanTX = mx.x - mn.x;
            const float spanTZ = mx.z - mn.z;
            triGridW = std::max(1, (int)std::ceil(spanTX / triGridCellSize) + 1);
            triGridH = std::max(1, (int)std::ceil(spanTZ / triGridCellSize) + 1);
            triGrid.resize((size_t)triGridW * (size_t)triGridH);

            for(uint32_t ti = 0; ti < (uint32_t)drivableTris.size(); ti++){
                const auto& tri = drivableTris[ti];
                const float tMinX = std::min({tri.v0.x, tri.v1.x, tri.v2.x});
                const float tMaxX = std::max({tri.v0.x, tri.v1.x, tri.v2.x});
                const float tMinZ = std::min({tri.v0.z, tri.v1.z, tri.v2.z});
                const float tMaxZ = std::max({tri.v0.z, tri.v1.z, tri.v2.z});

                int gx0 = std::clamp((int)std::floor((tMinX - triGridMinX) / triGridCellSize), 0, triGridW - 1);
                int gx1 = std::clamp((int)std::floor((tMaxX - triGridMinX) / triGridCellSize), 0, triGridW - 1);
                int gz0 = std::clamp((int)std::floor((tMinZ - triGridMinZ) / triGridCellSize), 0, triGridH - 1);
                int gz1 = std::clamp((int)std::floor((tMaxZ - triGridMinZ) / triGridCellSize), 0, triGridH - 1);

                for(int gz = gz0; gz <= gz1; gz++){
                    for(int gx = gx0; gx <= gx1; gx++){
                        triGrid[(size_t)(gz * triGridW + gx)].push_back(ti);
                    }
                }
            }
        }

        std::cout << "[TrackHeightfieldComponent] Built heightfield: "
                  << width << "x" << height << " cellSize=" << cellSize
                  << " minNormalY=" << minNormalY
                  << " drivableCells=" << drivableCount
                  << " wallCells=" << wallCount
                  << " wallSegments=" << wallSegments.size()
                  << " drivableTris=" << drivableTris.size()
                  << " triGrid=" << triGridW << "x" << triGridH
                  << std::endl;
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
        if(data.contains("curbTextureHints") && data["curbTextureHints"].is_array()){
            curbTextureHints = data["curbTextureHints"].get<std::vector<std::string>>();
        }

        std::string modelPath = data.value("model", "");
        if(modelPath.empty()){
            modelPath = data.value("obj", "");
        }
        if(modelPath.empty()){
            const std::string meshAssetName = data.value("mesh", "");
            if(meshAssetName.empty()){
                std::cerr << "[TrackHeightfieldComponent] Missing 'model' or 'obj' path (or 'mesh' asset name)" << std::endl;
                return;
            }
            const std::string* resolved = getMeshAssetPath(meshAssetName);
            if(resolved == nullptr || resolved->empty()){
                std::cerr << "[TrackHeightfieldComponent] Unknown mesh asset (no source path): \"" << meshAssetName << "\"" << std::endl;
                return;
            }
            modelPath = *resolved;
        }

        auto* owner = getOwner();
        if(owner == nullptr) return;

        const glm::mat4 localToWorld = owner->getLocalToWorldMatrix();
        buildFromModel(modelPath, localToWorld);
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

    bool TrackHeightfieldComponent::sampleDrivableSurface(float x, float z, float& outY, glm::vec3& outNormal) const {
        if(triGridW <= 0 || triGridH <= 0 || drivableTris.empty()) return false;

        const int gx = (int)std::floor((x - triGridMinX) / triGridCellSize);
        const int gz = (int)std::floor((z - triGridMinZ) / triGridCellSize);
        if(gx < 0 || gx >= triGridW || gz < 0 || gz >= triGridH) return false;

        const auto& cell = triGrid[(size_t)(gz * triGridW + gx)];
        if(cell.empty()) return false;

        const glm::vec2 p2(x, z);
        float bestY = -std::numeric_limits<float>::infinity();
        glm::vec3 bestNormal(0.0f, 1.0f, 0.0f);
        bool found = false;

        for(uint32_t ti : cell){
            const auto& tri = drivableTris[ti];
            const glm::vec2 a2(tri.v0.x, tri.v0.z);
            const glm::vec2 b2(tri.v1.x, tri.v1.z);
            const glm::vec2 c2(tri.v2.x, tri.v2.z);

            float w0, w1, w2;
            if(!barycentric2D(a2, b2, c2, p2, w0, w1, w2)) continue;

            const float y = w0 * tri.v0.y + w1 * tri.v1.y + w2 * tri.v2.y;
            if(y > bestY){
                bestY = y;
                bestNormal = tri.normal;
                found = true;
            }
        }

        if(!found) return false;

        outY = bestY;
        outNormal = bestNormal;
        return true;
    }

}
