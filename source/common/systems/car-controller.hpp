#pragma once

#include "../application.hpp"
#include "../ecs/world.hpp"
#include "../components/car-controller.hpp"
#include "../components/ai-car.hpp"
#include "../components/track-heightfield.hpp"
#include "../components/multi-mesh-renderer.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace our
{

    // Updates entities with CarControllerComponent using keyboard input,
    // and constrains them to a TrackHeightfieldComponent.
    //
    // Controls:
    // - W/S: forward/reverse
    // - A/D: steering
    class CarControllerSystem
    {
        Application *app = nullptr;

        struct ControlInput
        {
            float throttle = 0.0f;
            float steer = 0.0f;
        };

        static glm::vec3 getForward(float yaw)
        {
            glm::mat4 rot = glm::yawPitchRoll(yaw, 0.0f, 0.0f);
            // Many imported car OBJs face +Z in their local space.
            return glm::vec3(rot * glm::vec4(0, 0, 1, 0));
        }

        static std::string toLowerCopy(const std::string &s)
        {
            std::string out;
            out.reserve(s.size());
            for (unsigned char ch : s)
                out.push_back((char)std::tolower(ch));
            return out;
        }

        static TrackHeightfieldComponent *findTrack(World *world)
        {
            if (world == nullptr)
                return nullptr;
            for (auto entity : world->getEntities())
            {
                if (auto *track = entity->getComponent<TrackHeightfieldComponent>())
                    return track;
            }
            return nullptr;
        }

        struct TrackSample
        {
            bool valid = false;
            float y = 0.0f;
            TrackHeightfieldComponent::SurfaceType surface = TrackHeightfieldComponent::SurfaceType::Road;
        };

        enum class MoveResult
        {
            MovedRoad,
            MovedGrass,
            BlockedWall,
            BlockedUnknown
        };

        static bool sampleTrack(const TrackHeightfieldComponent *track, float x, float z, TrackSample &out)
        {
            if (track == nullptr)
                return false;

            TrackHeightfieldComponent::SurfaceType surface;
            float y = 0.0f;
            if (!track->sampleSurface(x, z, y, surface))
                return false;

            out.valid = true;
            out.y = y;
            out.surface = surface;
            return true;
        }

        static bool snapToTrack(const TrackHeightfieldComponent *track, glm::vec3 &position, float clearance)
        {
            TrackSample s;
            if (!sampleTrack(track, position.x, position.z, s))
                return false;
            if (s.surface == TrackHeightfieldComponent::SurfaceType::Wall)
                return false;
            position.y = s.y + clearance;
            return true;
        }

        static float wrapAngle(float a)
        {
            const float pi = glm::pi<float>();
            const float twoPi = 2.0f * pi;
            while (a > pi)
                a -= twoPi;
            while (a < -pi)
                a += twoPi;
            return a;
        }

        static std::vector<glm::vec3> buildAICenterline(const TrackHeightfieldComponent *track)
        {
            std::vector<glm::vec3> out;
            if (track == nullptr || track->width <= 1 || track->height <= 1)
                return out;

            glm::vec2 center(0.0f);
            int centerCount = 0;

            for (int z = 0; z < track->height; z++)
            {
                for (int x = 0; x < track->width; x++)
                {
                    const int idx = z * track->width + x;
                    if (track->wall[(size_t)idx] != 0)
                        continue;
                    if (track->drivable[(size_t)idx] == 0)
                        continue;
                    if ((TrackHeightfieldComponent::SurfaceType)track->surfaceType[(size_t)idx] != TrackHeightfieldComponent::SurfaceType::Road)
                        continue;

                    center += glm::vec2(
                        track->minX + ((float)x + 0.5f) * track->cellSize,
                        track->minZ + ((float)z + 0.5f) * track->cellSize);
                    centerCount++;
                }
            }

            if (centerCount == 0)
            {
                for (int z = 0; z < track->height; z++)
                {
                    for (int x = 0; x < track->width; x++)
                    {
                        const int idx = z * track->width + x;
                        if (track->wall[(size_t)idx] != 0)
                            continue;
                        if (track->drivable[(size_t)idx] == 0)
                            continue;

                        center += glm::vec2(
                            track->minX + ((float)x + 0.5f) * track->cellSize,
                            track->minZ + ((float)z + 0.5f) * track->cellSize);
                        centerCount++;
                    }
                }
            }

            if (centerCount == 0)
                return out;
            center /= (float)centerCount;

            const float spanX = (float)track->width * track->cellSize;
            const float spanZ = (float)track->height * track->cellSize;
            const float maxRadius = 0.85f * std::sqrt(spanX * spanX + spanZ * spanZ);
            const float step = std::max(0.30f, track->cellSize * 0.8f);
            const int rayCount = 96;

            for (int i = 0; i < rayCount; i++)
            {
                const float a = glm::two_pi<float>() * ((float)i / (float)rayCount);
                const glm::vec2 dir(std::cos(a), std::sin(a));

                bool inside = false;
                float firstT = -1.0f;
                float lastT = -1.0f;

                for (float t = 0.0f; t <= maxRadius; t += step)
                {
                    const float x = center.x + dir.x * t;
                    const float z = center.y + dir.y * t;

                    if (!track->containsXZ(x, z))
                    {
                        if (inside)
                            break;
                        continue;
                    }

                    float y = 0.0f;
                    TrackHeightfieldComponent::SurfaceType s = TrackHeightfieldComponent::SurfaceType::Road;
                    const bool drivable = track->sampleSurface(x, z, y, s) && s != TrackHeightfieldComponent::SurfaceType::Wall;

                    if (drivable)
                    {
                        if (firstT < 0.0f)
                            firstT = t;
                        lastT = t;
                        inside = true;
                    }
                    else if (inside)
                    {
                        break;
                    }
                }

                if (firstT < 0.0f || lastT <= firstT + step)
                    continue;

                const float midT = 0.5f * (firstT + lastT);
                const glm::vec2 p2 = center + dir * midT;

                float y = 0.0f;
                TrackHeightfieldComponent::SurfaceType s = TrackHeightfieldComponent::SurfaceType::Road;
                if (!track->sampleSurface(p2.x, p2.y, y, s) || s == TrackHeightfieldComponent::SurfaceType::Wall)
                    continue;

                out.emplace_back(p2.x, y, p2.y);
            }

            std::vector<glm::vec3> filtered;
            filtered.reserve(out.size());
            const float minGap2 = std::max(0.3f, step * 0.7f);
            const float minGapSq = minGap2 * minGap2;
            for (const auto &p : out)
            {
                const glm::vec2 d(filtered.empty() ? glm::vec2(0.0f) : glm::vec2(filtered.back().x - p.x, filtered.back().z - p.z));
                if (filtered.empty() || glm::dot(d, d) > minGapSq)
                {
                    filtered.push_back(p);
                }
            }

            if (filtered.size() < 12)
            {
                filtered.clear();
            }

            return filtered;
        }

        static std::vector<glm::vec3> trimToClosedLap(std::vector<glm::vec3> points)
        {
            if (points.size() < 64)
                return points;

            // Keep only the earliest full lap by finding the first late sample that revisits the start area.
            const glm::vec2 start(points.front().x, points.front().z);
            size_t bestIndex = 0;
            float bestDist2 = std::numeric_limits<float>::infinity();

            const size_t minLapSamples = std::max<size_t>(120, points.size() / 5);
            for (size_t i = minLapSamples; i < points.size(); i++)
            {
                const glm::vec2 p(points[i].x, points[i].z);
                const glm::vec2 d = p - start;
                const float dist2 = glm::dot(d, d);
                if (dist2 < bestDist2)
                {
                    bestDist2 = dist2;
                    bestIndex = i;
                }
            }

            // Accept closing only if the endpoint is reasonably close to start.
            if (bestIndex > 0 && bestDist2 <= 36.0f)
            {
                points.resize(bestIndex + 1);
            }

            // Remove near duplicates.
            std::vector<glm::vec3> out;
            out.reserve(points.size());
            for (const auto &p : points)
            {
                if (out.empty())
                {
                    out.push_back(p);
                    continue;
                }
                const glm::vec2 d(out.back().x - p.x, out.back().z - p.z);
                if (glm::dot(d, d) >= 0.49f)
                    out.push_back(p);
            }

            return out;
        }

        static std::vector<glm::vec3> loadAIPathFromPlayerTrace()
        {
            namespace fs = std::filesystem;
            const std::vector<fs::path> candidates = {
                fs::path("logs") / "player_track_trace.csv",
                fs::path("..") / "logs" / "player_track_trace.csv",
                fs::path("logs") / "player_track_trace_live.csv",
                fs::path("..") / "logs" / "player_track_trace_live.csv"};

            fs::path chosen;
            for (const auto &c : candidates)
            {
                std::error_code ec;
                if (fs::exists(c, ec))
                {
                    chosen = c;
                    break;
                }
            }

            if (chosen.empty())
            {
                std::cout << "[CarControllerSystem] AI trace not found (checked logs/player_track_trace.csv and ../logs/player_track_trace.csv)" << std::endl;
                return {};
            }

            std::ifstream in(chosen);
            if (!in.is_open())
            {
                std::cout << "[CarControllerSystem] Failed opening AI trace at " << chosen.string() << std::endl;
                return {};
            }

            std::vector<glm::vec3> points;
            points.reserve(2048);

            std::string line;
            bool first = true;
            while (std::getline(in, line))
            {
                if (line.empty())
                    continue;
                if (first)
                {
                    first = false;
                    if (line.find("time_s") != std::string::npos)
                        continue;
                }

                std::stringstream ss(line);
                std::string token;
                float vals[6] = {0, 0, 0, 0, 0, 0};
                int idx = 0;
                while (std::getline(ss, token, ',') && idx < 6)
                {
                    try
                    {
                        vals[idx++] = std::stof(token);
                    }
                    catch (...)
                    {
                        idx = -1;
                        break;
                    }
                }
                if (idx < 6)
                    continue;

                points.emplace_back(vals[1], vals[2], vals[3]);
            }

            const size_t rawCount = points.size();
            points = trimToClosedLap(std::move(points));
            if (points.size() < 24)
            {
                std::cout << "[CarControllerSystem] AI trace too small after trim: raw=" << rawCount
                          << " trimmed=" << points.size() << std::endl;
                return {};
            }

            std::cout << "[CarControllerSystem] Loaded AI path from " << chosen.string() << " with "
                      << points.size() << " waypoints" << std::endl;
            return points;
        }

        static const std::vector<glm::vec3> &getAICenterline(const TrackHeightfieldComponent *track)
        {
            static const TrackHeightfieldComponent *cachedTrack = nullptr;
            static int cachedWidth = 0;
            static int cachedHeight = 0;
            static float cachedCellSize = 0.0f;
            static std::vector<glm::vec3> cachedPath;
            static const std::vector<glm::vec3> empty;
            static bool usingTracePath = false;
            static int traceRetryCounter = 0;

            if (track == nullptr)
                return empty;

            const bool shouldRebuild =
                cachedTrack != track ||
                cachedWidth != track->width ||
                cachedHeight != track->height ||
                std::abs(cachedCellSize - track->cellSize) > 1e-6f;

            if (shouldRebuild)
            {
                cachedTrack = track;
                cachedWidth = track->width;
                cachedHeight = track->height;
                cachedCellSize = track->cellSize;
                cachedPath = loadAIPathFromPlayerTrace();
                usingTracePath = !cachedPath.empty();
                if (!usingTracePath)
                {
                    cachedPath = buildAICenterline(track);
                    std::cout << "[CarControllerSystem] AI path source: generated centerline, waypoints="
                              << cachedPath.size() << std::endl;
                }
                else
                {
                    std::cout << "[CarControllerSystem] AI path source: player trace, waypoints="
                              << cachedPath.size() << std::endl;
                }
                traceRetryCounter = 0;
            }
            else if (!usingTracePath)
            {
                // If we started with fallback (e.g. trace file not ready yet), periodically retry loading trace.
                traceRetryCounter++;
                if (traceRetryCounter >= 180)
                {
                    auto tracePath = loadAIPathFromPlayerTrace();
                    if (!tracePath.empty())
                    {
                        cachedPath = std::move(tracePath);
                        usingTracePath = true;
                        std::cout << "[CarControllerSystem] Switched AI path source to player trace, waypoints="
                                  << cachedPath.size() << std::endl;
                    }
                    traceRetryCounter = 0;
                }
            }

            return cachedPath;
        }

        static int findNearestWaypoint(const std::vector<glm::vec3> &path, const glm::vec3 &position)
        {
            if (path.empty())
                return 0;
            int bestIndex = 0;
            float bestDist2 = std::numeric_limits<float>::infinity();
            for (int i = 0; i < (int)path.size(); i++)
            {
                const glm::vec2 d(path[i].x - position.x, path[i].z - position.z);
                const float dist2 = glm::dot(d, d);
                if (dist2 < bestDist2)
                {
                    bestDist2 = dist2;
                    bestIndex = i;
                }
            }
            return bestIndex;
        }

        static int findNearestWaypointInWindow(const std::vector<glm::vec3> &path, const glm::vec3 &position, int center, int radius)
        {
            if (path.empty())
                return 0;
            const int n = (int)path.size();
            if (n <= 1)
                return 0;

            int bestIndex = ((center % n) + n) % n;
            float bestDist2 = std::numeric_limits<float>::infinity();

            for (int o = -radius; o <= radius; o++)
            {
                const int i = (center + o + n * 8) % n;
                const glm::vec2 d(path[i].x - position.x, path[i].z - position.z);
                const float dist2 = glm::dot(d, d);
                if (dist2 < bestDist2)
                {
                    bestDist2 = dist2;
                    bestIndex = i;
                }
            }

            return bestIndex;
        }

        static ControlInput computeAIInput(
            const TrackHeightfieldComponent *track,
            const std::vector<glm::vec3> &centerline,
            const CarControllerComponent &car,
            AICarComponent &ai,
            const Transform &transform,
            TrackHeightfieldComponent::SurfaceType currentSurface)
        {
            ControlInput input;
            if (centerline.size() < 4)
                return input;

            const int n = (int)centerline.size();

            if (!ai._initialized)
            {
                ai._currentWaypoint = findNearestWaypoint(centerline, transform.position);
                ai._initialized = true;
            }
            else
            {
                ai._currentWaypoint = findNearestWaypointInWindow(centerline, transform.position, ai._currentWaypoint, 48);
            }

            ai._currentWaypoint = (ai._currentWaypoint % n + n) % n;

            // If the tracked waypoint drifts too far away (e.g. after collision/spawn), hard-reacquire globally.
            {
                const glm::vec2 dwp(
                    centerline[ai._currentWaypoint].x - transform.position.x,
                    centerline[ai._currentWaypoint].z - transform.position.z);
                if (glm::dot(dwp, dwp) > 400.0f)
                {
                    ai._currentWaypoint = findNearestWaypoint(centerline, transform.position);
                }
            }

            const float speedAbs = std::abs(car.speed);
            const float baseLookAhead = std::clamp(2.2f + speedAbs * 0.45f, 2.2f, 10.0f);
            const float lookAheadDistance = baseLookAhead + (float)ai.lookAheadPoints * 0.55f;

            int targetIndex = ai._currentWaypoint;
            float marched = 0.0f;
            for (int step = 0; step < n - 1; step++)
            {
                const int i0 = (targetIndex + n) % n;
                const int i1 = (i0 + 1) % n;
                const glm::vec2 a(centerline[i0].x, centerline[i0].z);
                const glm::vec2 b(centerline[i1].x, centerline[i1].z);
                marched += glm::length(b - a);
                targetIndex = i1;
                if (marched >= lookAheadDistance)
                    break;
            }

            const int prevIndex = (targetIndex - 1 + n) % n;
            const int nextIndex = (targetIndex + 1) % n;

            glm::vec3 target = centerline[targetIndex];

            glm::vec2 tangent(
                centerline[nextIndex].x - centerline[prevIndex].x,
                centerline[nextIndex].z - centerline[prevIndex].z);
            const float tangentLen = glm::length(tangent);
            if (tangentLen > 1e-5f)
            {
                tangent /= tangentLen;
                const glm::vec2 normal(-tangent.y, tangent.x);
                target.x += normal.x * ai.laneOffset;
                target.z += normal.y * ai.laneOffset;
            }

            if (track != nullptr)
            {
                TrackSample t;
                if (sampleTrack(track, target.x, target.z, t) && t.surface != TrackHeightfieldComponent::SurfaceType::Wall)
                {
                    target.y = t.y + car.groundClearance;
                }
            }

            const glm::vec2 toTarget(target.x - transform.position.x, target.z - transform.position.z);
            const float targetLen = glm::length(toTarget);
            if (targetLen < 1e-4f)
                return input;

            const float desiredYaw = std::atan2(toTarget.x, toTarget.y);
            const float yawError = wrapAngle(desiredYaw - transform.rotation.y);

            const float steerNorm = yawError / glm::radians(22.0f);
            input.steer = std::clamp(steerNorm * ai.steerResponsiveness, -1.0f, 1.0f);

            float targetSpeed = std::min(ai.desiredSpeed, car.maxSpeed * 0.95f);
            targetSpeed *= (1.0f - 0.62f * std::clamp(std::abs(input.steer), 0.0f, 1.0f));

            if (currentSurface == TrackHeightfieldComponent::SurfaceType::Grass)
            {
                targetSpeed *= 0.8f;
            }

            if (std::abs(yawError) > glm::radians(60.0f))
            {
                targetSpeed = std::min(targetSpeed, car.maxSpeed * 0.35f);
            }

            if (car.speed < targetSpeed - 0.4f)
            {
                input.throttle = 1.0f;
            }
            else if (car.speed > targetSpeed + 0.9f)
            {
                input.throttle = (std::abs(yawError) > glm::radians(95.0f)) ? -0.6f : 0.0f;
            }
            else
            {
                input.throttle = 0.15f;
            }

            if (std::abs(yawError) > glm::radians(105.0f) && car.speed > 2.5f)
            {
                input.throttle = -0.8f;
            }

            return input;
        }

        static MoveResult tryMove(
            const TrackHeightfieldComponent *track,
            glm::vec3 &position,
            const glm::vec3 &delta,
            float clearance,
            float collisionRadius,
            float wallPushback,
            int wallResolveIterations,
            float maxClimbHeight,
            TrackHeightfieldComponent::SurfaceType &outSurface)
        {
            auto tryCandidate = [&](glm::vec3 candidate) -> MoveResult
            {
                if (track != nullptr)
                {
                    glm::vec2 p(candidate.x, candidate.z);
                    track->resolveWallCollision(p, collisionRadius, wallPushback, wallResolveIterations);
                    candidate.x = p.x;
                    candidate.z = p.y;
                }

                TrackSample s;
                if (!sampleTrack(track, candidate.x, candidate.z, s))
                {
                    // If still inside the track bounds but no classified cell exists,
                    // treat this as soft grass (keep current vertical position).
                    if (track != nullptr && track->containsXZ(candidate.x, candidate.z))
                    {
                        candidate.y = position.y;
                        position.x = candidate.x;
                        position.z = candidate.z;
                        outSurface = TrackHeightfieldComponent::SurfaceType::Grass;
                        return MoveResult::MovedGrass;
                    }
                    return MoveResult::BlockedUnknown;
                }
                if (s.surface == TrackHeightfieldComponent::SurfaceType::Wall)
                {
                    return MoveResult::BlockedWall;
                }

                const float targetY = s.y + clearance;
                if (targetY > position.y + maxClimbHeight)
                {
                    return MoveResult::BlockedWall;
                }

                candidate.y = targetY;
                position = candidate;
                outSurface = s.surface;
                return (s.surface == TrackHeightfieldComponent::SurfaceType::Grass)
                           ? MoveResult::MovedGrass
                           : MoveResult::MovedRoad;
            };

            MoveResult result = tryCandidate(position + delta);
            if (result == MoveResult::MovedRoad || result == MoveResult::MovedGrass)
                return result;

            // Slide: try X-only then Z-only.
            result = tryCandidate(position + glm::vec3(delta.x, 0.0f, 0.0f));
            if (result == MoveResult::MovedRoad || result == MoveResult::MovedGrass)
                return result;

            result = tryCandidate(position + glm::vec3(0.0f, 0.0f, delta.z));
            if (result == MoveResult::MovedRoad || result == MoveResult::MovedGrass)
                return result;

            return result;
        }

        static void cacheFrontWheelParts(CarControllerComponent &car, const MultiMeshRendererComponent &multi)
        {
            if (car._cachedFrontWheelParts)
                return;

            struct Candidate
            {
                int idx;
                float score;
                glm::vec3 p;
            };
            std::vector<Candidate> candidates;
            candidates.reserve(multi.parts.size());

            const std::vector<std::string> wheelHints = {"wheel", "tire", "tyre", "rim"};

            for (int i = 0; i < (int)multi.parts.size(); i++)
            {
                const auto &part = multi.parts[i];
                const std::string fullNameLower = toLowerCopy(part.objectName + std::string(" ") + part.materialName);

                // Geometry heuristic: wheels are often round in two axes and thin in the third.
                const glm::vec3 s = glm::abs(part.aabbSize);
                const float a = s.x, b = s.y, c = s.z;
                const float maxDim = std::max(a, std::max(b, c));
                const float minDim = std::min(a, std::min(b, c));
                const float midDim = (a + b + c) - maxDim - minDim;
                if (maxDim <= 1e-6f || midDim <= 1e-6f)
                    continue;

                const float roundness = std::clamp(midDim / maxDim, 0.0f, 1.0f);
                const float thinness = std::clamp(minDim / midDim, 0.0f, 1.0f);
                if (roundness < 0.45f)
                    continue;

                float nameBonus = 0.0f;
                for (const auto &h : wheelHints)
                {
                    if (fullNameLower.find(h) != std::string::npos)
                    {
                        nameBonus = 1.0f;
                        break;
                    }
                }

                float score = 0.0f;
                score += 2.0f * roundness;
                score += 1.0f * (1.0f - thinness);
                score += nameBonus;
                // Prefer parts near ground.
                score += -0.1f * part.localTransform.position.y;

                candidates.push_back({i, score, part.localTransform.position});
            }

            if (candidates.size() < 2)
            {
                car._cachedFrontWheelParts = true;
                return;
            }

            // Pick up to 4 best wheel candidates.
            std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b)
                      { return a.score > b.score; });
            if (candidates.size() > 8)
                candidates.resize(8);

            // Compute center, then pick farthest points.
            glm::vec3 center(0.0f);
            for (const auto &c : candidates)
                center += c.p;
            center /= (float)candidates.size();

            std::vector<int> picked;
            picked.reserve(4);
            while (picked.size() < 4 && !candidates.empty())
            {
                int bestIdx = -1;
                float bestVal = -1e9f;
                for (const auto &c : candidates)
                {
                    if (std::find(picked.begin(), picked.end(), c.idx) != picked.end())
                        continue;
                    const float dist = glm::length(glm::vec2(c.p.x - center.x, c.p.z - center.z));
                    const float val = dist + 0.15f * c.score;
                    if (val > bestVal)
                    {
                        bestVal = val;
                        bestIdx = c.idx;
                    }
                }
                if (bestIdx < 0)
                    break;
                picked.push_back(bestIdx);
            }

            if (picked.size() < 2)
            {
                car._cachedFrontWheelParts = true;
                return;
            }

            // Front wheels: those with highest local Z (assuming car forward is +Z).
            std::vector<std::pair<float, int>> byZ;
            byZ.reserve(picked.size());
            for (int idx : picked)
            {
                byZ.push_back({multi.parts[idx].localTransform.position.z, idx});
            }
            std::sort(byZ.begin(), byZ.end(), [](auto a, auto b)
                      { return a.first > b.first; });

            car._frontWheelPartIndices.clear();
            car._frontWheelBaseYaw.clear();
            const int count = std::min(2, (int)byZ.size());
            for (int i = 0; i < count; i++)
            {
                const int idx = byZ[i].second;
                car._frontWheelPartIndices.push_back(idx);
                car._frontWheelBaseYaw.push_back(multi.parts[idx].localTransform.rotation.y);
            }

            car._cachedFrontWheelParts = true;
        }

    public:
        void enter(Application *application) { app = application; }

        void update(World *world, float deltaTime)
        {
            if (app == nullptr)
                return;

            auto *track = findTrack(world);

            auto &keyboard = app->getKeyboard();
            const auto &aiCenterline = getAICenterline(track);

            for (auto entity : world->getEntities())
            {
                auto *car = entity->getComponent<CarControllerComponent>();
                if (car == nullptr)
                    continue;

                auto &transform = entity->localTransform;

                TrackHeightfieldComponent::SurfaceType currentSurface = TrackHeightfieldComponent::SurfaceType::Road;

                // Keep the car vertically attached to the sampled surface at start of frame.
                TrackSample startSample;
                if (sampleTrack(track, transform.position.x, transform.position.z, startSample) &&
                    startSample.surface != TrackHeightfieldComponent::SurfaceType::Wall)
                {
                    transform.position.y = startSample.y + car->groundClearance;
                    currentSurface = startSample.surface;
                }
                else if (track != nullptr)
                {
                    bool recovered = false;

                    // First, try a local depenetration from wall segments only.
                    // This avoids large snap corrections when skimming walls or grass edges.
                    glm::vec2 nudged(transform.position.x, transform.position.z);
                    if (track->resolveWallCollision(
                            nudged,
                            std::max(0.05f, car->collisionRadius),
                            0.0f,
                            std::max(1, car->wallResolveIterations)))
                    {
                        transform.position.x = nudged.x;
                        transform.position.z = nudged.y;
                        if (sampleTrack(track, transform.position.x, transform.position.z, startSample) &&
                            startSample.surface != TrackHeightfieldComponent::SurfaceType::Wall)
                        {
                            transform.position.y = startSample.y + car->groundClearance;
                            currentSurface = startSample.surface;
                            recovered = true;
                        }
                    }

                    // If outside bounds, allow projection but keep it local to prevent teleporting.
                    if (!recovered && !track->containsXZ(transform.position.x, transform.position.z))
                    {
                        const glm::vec2 oldXZ(transform.position.x, transform.position.z);
                        glm::vec2 projected = oldXZ;
                        constexpr int kRecoverCells = 12;
                        if (track->projectToNearestDrivable(projected, kRecoverCells))
                        {
                            const float maxRecoverDistance = std::max(1.0f, car->collisionRadius * 10.0f);
                            if (glm::length(projected - oldXZ) <= maxRecoverDistance)
                            {
                                transform.position.x = projected.x;
                                transform.position.z = projected.y;
                                if (sampleTrack(track, transform.position.x, transform.position.z, startSample) &&
                                    startSample.surface != TrackHeightfieldComponent::SurfaceType::Wall)
                                {
                                    transform.position.y = startSample.y + car->groundClearance;
                                    currentSurface = startSample.surface;
                                    recovered = true;
                                }
                            }
                        }
                    }

                    // If still unresolved but still within track bounds, keep motion continuous.
                    if (!recovered && track->containsXZ(transform.position.x, transform.position.z))
                    {
                        currentSurface = TrackHeightfieldComponent::SurfaceType::Grass;
                    }
                }

                float throttle = 0.0f;
                float steer = 0.0f;
                if (auto *ai = entity->getComponent<AICarComponent>())
                {
                    const ControlInput aiInput = computeAIInput(track, aiCenterline, *car, *ai, transform, currentSurface);
                    throttle = aiInput.throttle;
                    steer = aiInput.steer;
                }
                else
                {
                    throttle = (keyboard.isPressed(GLFW_KEY_W) ? 1.0f : 0.0f) - (keyboard.isPressed(GLFW_KEY_S) ? 1.0f : 0.0f);
                    steer = (keyboard.isPressed(GLFW_KEY_A) ? 1.0f : 0.0f) - (keyboard.isPressed(GLFW_KEY_D) ? 1.0f : 0.0f);
                }
                const bool onGrass = (currentSurface == TrackHeightfieldComponent::SurfaceType::Grass);
                const float accelFactor = onGrass ? car->grassAccelFactor : 1.0f;

                // Update speed.
                if (throttle > 0.0f)
                {
                    car->speed += car->acceleration * accelFactor * deltaTime;
                }
                else if (throttle < 0.0f)
                {
                    car->speed -= car->brakeAcceleration * accelFactor * deltaTime;
                }
                else
                {
                    // Damping towards 0.
                    const float extraGrassDrag = onGrass ? (0.35f * car->grassDamping) : 0.0f;
                    const float damping = std::max(0.0f, 1.0f - (car->linearDamping + extraGrassDrag) * deltaTime);
                    car->speed *= damping;
                }

                // Clamp to base limits first to avoid runaway speed.
                car->speed = std::clamp(car->speed, -car->maxReverseSpeed, car->maxSpeed);

                // On grass, bleed excess speed smoothly toward a lower effective max.
                if (onGrass)
                {
                    const float grassForwardLimit = std::max(0.5f, car->maxSpeed * car->grassSpeedFactor);
                    const float grassReverseLimit = std::max(0.35f, car->maxReverseSpeed * car->grassSpeedFactor);
                    const float bleed = std::clamp(car->grassDamping * deltaTime, 0.0f, 1.0f);

                    if (car->speed > grassForwardLimit)
                    {
                        car->speed -= (car->speed - grassForwardLimit) * bleed;
                    }
                    else if (car->speed < -grassReverseLimit)
                    {
                        car->speed += ((-grassReverseLimit) - car->speed) * bleed;
                    }
                }

                // Turning. (Less turning when nearly stopped.)
                const float speedFactor = std::clamp(std::abs(car->speed) / std::max(1e-3f, car->maxSpeed), 0.0f, 1.0f);
                const float grassTurnFactor = onGrass ? car->grassTurnFactor : 1.0f;
                // If the car is basically stationary, don't rotate in place (only steer tires visually).
                if (std::abs(car->speed) > 0.05f)
                {
                    // Swap steering while reversing.
                    const float reversing = (car->speed < -0.1f) ? -1.0f : 1.0f;
                    transform.rotation.y += (steer * reversing) * car->turnSpeed * deltaTime * speedFactor * grassTurnFactor;
                }

                // Integrate position in XZ using sub-steps for stable collision near walls.
                const glm::vec3 forward = getForward(transform.rotation.y);
                const float totalDistance = std::abs(car->speed * deltaTime);
                const float subStepDistance = std::max(0.05f, car->collisionSubstepDistance);
                const int subSteps = std::clamp((int)std::ceil(totalDistance / subStepDistance), 1, 12);
                const glm::vec3 stepDelta = forward * ((car->speed * deltaTime) / (float)subSteps);

                for (int i = 0; i < subSteps; i++)
                {
                    TrackHeightfieldComponent::SurfaceType steppedSurface = currentSurface;
                    const MoveResult move = tryMove(
                        track,
                        transform.position,
                        glm::vec3(stepDelta.x, 0.0f, stepDelta.z),
                        car->groundClearance,
                        car->collisionRadius,
                        car->wallPushback,
                        car->wallResolveIterations,
                        car->maxClimbHeight,
                        steppedSurface);

                    if (move == MoveResult::MovedGrass || move == MoveResult::MovedRoad)
                    {
                        currentSurface = steppedSurface;
                        continue;
                    }

                    if (move == MoveResult::BlockedWall)
                    {
                        const float keep = std::clamp(1.0f - car->wallBounceDamping, 0.0f, 1.0f);
                        car->speed *= keep;
                    }
                    else
                    {
                        car->speed *= 0.9f;
                    }
                    break;
                }

                // Visual wheel steering angle (independent of movement).
                car->steeringAngle = steer * glm::radians(car->wheelSteerMaxAngle);

                // Front wheel steering animation (if MultiMeshRenderer exists).
                if (auto *multi = entity->getComponent<MultiMeshRendererComponent>())
                {
                    cacheFrontWheelParts(*car, *multi);
                    for (size_t i = 0; i < car->_frontWheelPartIndices.size(); i++)
                    {
                        const int idx = car->_frontWheelPartIndices[i];
                        if (idx < 0 || idx >= (int)multi->parts.size())
                            continue;
                        multi->parts[idx].localTransform.rotation.y = car->_frontWheelBaseYaw[i] + car->steeringAngle;
                    }
                }
            }
        }
    };

}
