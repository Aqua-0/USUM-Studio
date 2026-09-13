#include "scene/player.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace studio {
namespace {
using P = SpatialPoint;
P add(P a, P b) {
    for (unsigned i = 0; i < 3; ++i)
        a[i] += b[i];
    return a;
}
P sub(P a, P b) {
    for (unsigned i = 0; i < 3; ++i)
        a[i] -= b[i];
    return a;
}
P mul(P a, float s) {
    for (auto &v : a)
        v *= s;
    return a;
}
float dot(P a, P b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
P unit(P a) {
    return mul(a, 1 / std::max(.00001f, std::sqrt(dot(a, a))));
}
float cross2(P a, P b) {
    return a[0] * b[2] - a[2] * b[0];
}
bool bary(P p, P a, P b, P c, P &w) {
    auto ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
    auto det = cross2(ab, ac);
    if (std::abs(det) < 1e-6f)
        return false;
    w[1] = cross2(ap, ac) / det;
    w[2] = cross2(ab, ap) / det;
    w[0] = 1 - w[1] - w[2];
    return *std::min_element(w.begin(), w.end()) >= -.0001f;
}
bool region_hit(const SpatialRegion &r, P p, float &y, float &ratio) {
    if (r.circle_radius > 0) {
        float d =
            std::hypot(p[0] - r.circle_center[0], p[2] - r.circle_center[2]) / r.circle_radius;
        if (d > 1)
            return false;
        y = r.circle_center[1];
        ratio =
            r.center_ratio >= 1 ? 0 : std::max(0.f, (d - r.center_ratio) / (1 - r.center_ratio));
        return true;
    }
    bool hit = false;
    float nearest = INFINITY;
    for (std::size_t i = 0; i + 2 < r.triangles.size(); i += 3) {
        auto &a = r.vertices[r.triangles[i]], &b = r.vertices[r.triangles[i + 1]],
             &c = r.vertices[r.triangles[i + 2]];
        P w;
        if (!bary(p, a.position, b.position, c.position, w))
            continue;
        float h = w[0] * a.position[1] + w[1] * b.position[1] + w[2] * c.position[1];
        if (std::abs(h - p[1]) < nearest) {
            nearest = std::abs(h - p[1]);
            y = h;
            ratio = w[0] * a.ratio + w[1] * b.ratio + w[2] * c.ratio;
            hit = true;
        }
    }
    return hit;
}
float ease(unsigned kind, float t) {
    if (!kind)
        return t;
    unsigned family = (kind - 1) / 3, mode = (kind - 1) % 3;
    auto in = [&](float x) {
        if (family == 0)
            return 1 - std::cos(x * 1.5707963268f);
        if (family <= 4)
            return std::pow(x, float(family + 1));
        if (family == 5)
            return x == 0 ? 0.f : std::pow(2.f, 10 * x - 10);
        if (family == 6)
            return 1 - std::sqrt(std::max(0.f, 1 - x * x));
        return x;
    };
    return mode == 0   ? in(t)
           : mode == 1 ? 1 - in(1 - t)
           : t < .5f   ? in(2 * t) * .5f
                       : 1 - in(2 - 2 * t) * .5f;
}
using Q = std::array<float, 4>;
Q rotation(P degrees) {
    Q q{0, 0, 0, 1};
    for (unsigned i = 0; i < 3; ++i) {
        Q b{};
        b[i] = std::sin(degrees[i] * .00872664626f);
        b[3] = std::cos(degrees[i] * .00872664626f);
        q = {b[3] * q[0] + b[0] * q[3] + b[1] * q[2] - b[2] * q[1],
             b[3] * q[1] - b[0] * q[2] + b[1] * q[3] + b[2] * q[0],
             b[3] * q[2] + b[0] * q[1] - b[1] * q[0] + b[2] * q[3],
             b[3] * q[3] - b[0] * q[0] - b[1] * q[1] - b[2] * q[2]};
    }
    return q;
}
Q blend(Q a, Q b, float t) {
    float d = 0;
    for (unsigned i = 0; i < 4; ++i)
        d += a[i] * b[i];
    if (d < 0) {
        for (auto &v : b)
            v = -v;
        d = -d;
    }
    float x = 1 - t, y = t;
    if (d < .9995f) {
        auto angle = std::acos(std::clamp(d, -1.f, 1.f));
        x = std::sin((1 - t) * angle) / std::sin(angle);
        y = std::sin(t * angle) / std::sin(angle);
    }
    Q q;
    float n = 0;
    for (unsigned i = 0; i < 4; ++i) {
        q[i] = a[i] * x + b[i] * y;
        n += q[i] * q[i];
    }
    for (auto &v : q)
        v /= std::sqrt(n);
    return q;
}
P rotate(P v, Q q) {
    P u{q[0], q[1], q[2]};
    P cross{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    return add(add(mul(u, 2 * dot(u, v)), mul(v, q[3] * q[3] - dot(u, u))), mul(cross, 2 * q[3]));
}
}
std::optional<float> PlayerController::ground(const SpatialScene &scene, P p, float rise,
                                              float fall) const {
    std::optional<float> best;
    for (auto &r : scene.regions)
        if (r.kind == SpatialKind::Ground || r.kind == SpatialKind::Placement)
            for (std::size_t i = 0; i + 2 < r.triangles.size(); i += 3) {
                auto a = r.vertices[r.triangles[i]].position,
                     b = r.vertices[r.triangles[i + 1]].position,
                     c = r.vertices[r.triangles[i + 2]].position;
                P w;
                if (!bary(p, a, b, c, w))
                    continue;
                float y = w[0] * a[1] + w[1] * b[1] + w[2] * c[1];
                if (y <= p[1] + rise && y >= p[1] - fall && (!best || y > *best))
                    best = y;
            }
    return best;
}
void PlayerController::collide(const SpatialScene &scene, P &p) const {
    for (unsigned pass = 0; pass < 4; ++pass) {
        bool changed = false;
        for (auto &r : scene.regions)
            if (r.kind == SpatialKind::Wall || r.kind == SpatialKind::WaterWall ||
                r.kind == SpatialKind::Placement)
                for (std::size_t i = 0; i + 2 < r.triangles.size(); i += 3) {
                    auto pa = r.vertices[r.triangles[i]].position,
                         pb = r.vertices[r.triangles[i + 1]].position,
                         pc = r.vertices[r.triangles[i + 2]].position;
                    if (p[0] + radius < std::min({pa[0], pb[0], pc[0]}) ||
                        p[0] - radius > std::max({pa[0], pb[0], pc[0]}) ||
                        p[2] + radius < std::min({pa[2], pb[2], pc[2]}) ||
                        p[2] - radius > std::max({pa[2], pb[2], pc[2]}))
                        continue;
                    std::vector<P> poly{pa, pb, pc};
                    float low = std::min({poly[0][1], poly[1][1], poly[2][1]}),
                          high = std::max({poly[0][1], poly[1][1], poly[2][1]});
                    if (high <= p[1] + 10 || low >= p[1] + height)
                        continue;
                    for (unsigned plane = 0; plane < 2; ++plane) {
                        std::vector<P> clipped;
                        float h = p[1] + (plane ? height : 10);
                        for (unsigned k = 0; k < poly.size(); ++k) {
                            auto a = poly[k], b = poly[(k + 1) % poly.size()];
                            bool ia = plane ? a[1] <= h : a[1] >= h,
                                 ib = plane ? b[1] <= h : b[1] >= h;
                            if (ia)
                                clipped.push_back(a);
                            if (ia != ib)
                                clipped.push_back(
                                    add(a, mul(sub(b, a), (h - a[1]) / (b[1] - a[1]))));
                        }
                        poly = std::move(clipped);
                    }
                    if (poly.size() < 2)
                        continue;
                    float best = radius * radius;
                    P nearest{};
                    bool found = false;
                    for (unsigned k = 0; k < poly.size(); ++k) {
                        auto a = poly[k], d = sub(poly[(k + 1) % poly.size()], a);
                        float n = d[0] * d[0] + d[2] * d[2];
                        if (n < 1e-8f)
                            continue;
                        float t =
                            std::clamp(((p[0] - a[0]) * d[0] + (p[2] - a[2]) * d[2]) / n, 0.f, 1.f);
                        auto q = add(a, mul(d, t));
                        float dist = (p[0] - q[0]) * (p[0] - q[0]) + (p[2] - q[2]) * (p[2] - q[2]);
                        if (dist < best) {
                            best = dist;
                            nearest = q;
                            found = true;
                        }
                    }
                    if (found && best > 1e-8f) {
                        auto distance = std::sqrt(best);
                        p[0] += (p[0] - nearest[0]) * (radius - distance) / distance;
                        p[2] += (p[2] - nearest[2]) * (radius - distance) / distance;
                        changed = true;
                    }
                }
        if (!changed)
            break;
    }
}
bool PlayerController::start(const SpatialScene &scene, P spawn, int initial_zone) {
    active = false;
    paused = false;
    entered_camera_ = resolved_camera_ = camera_setting = -1;
    support_scale = 1;
    support_time_ = 0;
    notice.clear();
    auto y = ground(scene, spawn, 30, 1000);
    if (!y) {
        notice = "No walkable surface below the start position.";
        return false;
    }
    position = spawn;
    position[1] = *y;
    collide(scene, position);
    y = ground(scene, position, 15, 35);
    if (!y) {
        notice = "The start is blocked beside the ground surface.";
        return false;
    }
    position[1] = *y;
    transition_time_ = transition_duration_ = 0;
    camera_region = -1;
    zone = initial_zone;
    motion = 0;
    seconds = 0;
    yaw = 0;
    active = true;
    update_camera(scene, 0, true);
    return true;
}
void PlayerController::step(const SpatialScene &scene, float sideways, float forward, bool running,
                            bool through_walls, float dt) {
    if (!active)
        return;
    if (paused) {
        update_camera(scene, 0, false);
        return;
    }
    dt = std::clamp(dt, 0.f, .1f);
    auto f = sub(camera.target, camera.eye);
    f[1] = 0;
    f = unit(f);
    P right{-f[2], 0, f[0]};
    auto direction = add(mul(right, sideways), mul(f, forward));
    auto length = std::sqrt(dot(direction, direction));
    auto before = position;
    float speed = running ? 360.f * run_multiplier : 180.f;
    if (length > 0) {
        direction = mul(direction, 1 / length);
        yaw = std::atan2(direction[0], direction[2]);
        unsigned steps = std::max(1u, unsigned(std::ceil(speed * dt / (radius * .25f))));
        for (unsigned i = 0; i < steps; ++i) {
            auto next = add(position, mul(direction, speed * dt / steps));
            if (!through_walls)
                collide(scene, next);
            auto y = ground(scene, next, 15, 35);
            if (y) {
                next[1] = *y;
                position = next;
            }
        }
    }
    unsigned next = std::hypot(position[0] - before[0], position[2] - before[2]) > .001f
                        ? (running ? 2 : 1)
                        : 0;
    if (next != motion) {
        motion = next;
        seconds = 0;
    }
    seconds += dt * (running ? run_multiplier : 1);
    update_camera(scene, dt, false);
}
void PlayerController::adjust_running_speed(float wheel) {
    run_multiplier = std::clamp(run_multiplier * std::exp(wheel * .15f), .1f, 16.f);
}
Matrix PlayerController::transform() const {
    float c = std::cos(yaw), s = std::sin(yaw);
    return {c, 0, s, position[0], 0, 1, 0, position[1], -s, 0, c, position[2], 0, 0, 0, 1};
}
void PlayerController::update_camera(const SpatialScene &scene, float dt, bool snap) {
    float nearest = INFINITY;
    for (auto &r : scene.regions)
        if (r.kind == SpatialKind::Zone && r.zone >= 0) {
            float y, t;
            if (region_hit(r, position, y, t) && std::abs(y - position[1]) < nearest) {
                nearest = std::abs(y - position[1]);
                zone = r.zone;
            }
        }
    const CameraSetting *fallback = nullptr;
    for (auto &z : scene.zones)
        if (z.zone == zone && z.camera < scene.defaults.size())
            fallback = &scene.defaults[z.camera];
    if (!fallback && !scene.defaults.empty())
        fallback = &scene.defaults.front();
    const CameraSetting *chosen = fallback;
    int priority = std::numeric_limits<int>::min(), selected = -1;
    float ratio = 0;
    nearest = INFINITY;
    for (unsigned i = 0; i < scene.regions.size(); ++i) {
        auto &r = scene.regions[i];
        if (r.kind != SpatialKind::Camera)
            continue;
        const CameraSetting *s = r.attribute == 65535                 ? fallback
                                 : r.attribute < scene.cameras.size() ? &scene.cameras[r.attribute]
                                                                      : nullptr;
        if (!s || s->type > 2 ||
            (s->work > 0 &&
             (camera_variables.contains(s->work) ? camera_variables.at(s->work) : 0) != s->value))
            continue;
        float y, t;
        if (!region_hit(r, position, y, t))
            continue;
        float distance = std::abs(y - position[1]);
        if (s->priority > priority || (s->priority == priority && distance < nearest)) {
            priority = s->priority;
            nearest = distance;
            selected = int(i);
            chosen = s;
            ratio = t;
        }
    }
    int entered = selected >= 0 ? int(scene.regions[selected].attribute) : 65535;
    int previous = camera_setting;
    if (snap || entered != entered_camera_) {
        int resolved = entered;
        for (auto rule : scene.replacements)
            if (int(rule[0]) == entered_camera_ && int(rule[1]) == entered) {
                resolved = int(rule[2]);
                break;
            }
        entered_camera_ = entered;
        resolved_camera_ = resolved;
    }
    camera_setting = resolved_camera_;
    if (camera_setting == 65535)
        chosen = fallback;
    if (camera_setting >= 0 && camera_setting < int(scene.cameras.size())) {
        auto &candidate = scene.cameras[camera_setting];
        if (candidate.type <= 2 &&
            (candidate.work <= 0 ||
             (camera_variables.contains(candidate.work) ? camera_variables.at(candidate.work)
                                                        : 0) == candidate.value))
            chosen = &candidate;
        else {
            chosen = fallback;
            camera_setting = 65535;
        }
    }
    if (camera_override >= 0 && camera_override < int(scene.cameras.size()) &&
        scene.cameras[camera_override].type <= 2) {
        chosen = &scene.cameras[camera_override];
        camera_setting = camera_override;
        ratio = camera_override_blend;
    }
    camera_ratio = ratio;
    CameraSupport support;
    if (chosen && chosen->type < 2) {
        support = chosen->support;
        const CameraSupport *defaults = nullptr;
        for (auto &z : scene.zones)
            if (z.zone == zone && z.support < scene.support_defaults.size())
                defaults = &scene.support_defaults[z.support].support;
        if (defaults) {
            if (support.zone_default)
                support = *defaults;
            else if (support.parameter_default) {
                support.maximum = defaults->maximum;
                support.entering = defaults->entering;
                support.leaving = defaults->leaving;
            }
        }
    }
    if (snap || support.type != support_data_.type || support.maximum != support_data_.maximum) {
        support_scale = 1;
        support_from_ = 1;
        support_time_ = 0;
        support_out_ = false;
    }
    support_data_ = support;
    if ((support.type == 2 || support.type == 3) && support.maximum > 0) {
        if (motion != 1) {
            bool out = motion == 2;
            if (out != support_out_) {
                support_out_ = out;
                support_from_ = support_scale;
                support_time_ = 0;
            }
            support_time_ += dt;
            auto timings = out ? support.leaving : support.entering;
            float elapsed = support_time_ - float(timings[0]) / 30;
            if (elapsed >= 0) {
                float t = timings[1] ? std::clamp(elapsed / (float(timings[1]) / 30), 0.f, 1.f) : 1;
                support_scale = support_from_ +
                                ((out ? support.maximum : 1) - support_from_) * ease(timings[2], t);
            }
        } else {
            support_time_ = 0;
            support_from_ = support_scale;
        }
    } else
        support_scale = 1;
    auto target_position = add(position, {0, 133, 0});
    PlayerCamera desired;
    desired.target = add(position, {0, 100, 0});
    desired.eye = add(desired.target, {0, 450, 600});
    if (chosen) {
        if (chosen->type < 2) {
            auto a = chosen->a, b = chosen->b;
            if (a.zone_default && fallback)
                a = fallback->a;
            if (b.zone_default && fallback)
                b = fallback->a;
            float t = chosen->type == 1 ? ease(chosen->easing, std::clamp(ratio, 0.f, 1.f)) : 0;
            desired.target = add(target_position, add(mul(a.offset, 1 - t), mul(b.offset, t)));
            auto q = blend(rotation(a.rotation), rotation(b.rotation), t);
            desired.eye = add(
                desired.target,
                rotate({0, 0, (a.distance + (b.distance - a.distance) * t) * support_scale}, q));
            desired.fov = std::clamp(a.fov + (b.fov - a.fov) * t, 5.f, 120.f);
        } else if (chosen->type == 2) {
            desired.eye = chosen->position;
            desired.target =
                chosen->player_target ? add(target_position, chosen->hold_offset) : chosen->target;
            desired.fov = std::clamp(chosen->hold_fov, 5.f, 120.f);
            auto f = unit(sub(desired.target, desired.eye));
            auto r = unit(P{-f[2], 0, f[0]});
            P up{r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
            desired.up = add(mul(up, std::cos(chosen->bank * .01745329252f)),
                             mul(r, std::sin(chosen->bank * .01745329252f)));
        }
    }
    if (!snap && (selected != camera_region || previous != camera_setting)) {
        transition_from_ = camera;
        transition_time_ = 0;
        transition_duration_ = chosen ? float(chosen->transition) / 30 : 0;
    }
    transition_time_ += dt;
    float amount = snap || transition_duration_ <= 0
                       ? 1
                       : std::clamp(transition_time_ / transition_duration_, 0.f, 1.f);
    camera = desired;
    if (amount < 1) {
        camera.eye = add(transition_from_.eye, mul(sub(desired.eye, transition_from_.eye), amount));
        camera.target =
            add(transition_from_.target, mul(sub(desired.target, transition_from_.target), amount));
        camera.fov = transition_from_.fov + (desired.fov - transition_from_.fov) * amount;
    }
    auto original = camera.target;
    for (auto &stop : scene.scroll_stops) {
        auto &p = camera.target;
        auto &h = stop.hit;
        if (!stop.outside) {
            if (p[0] < h[0][0] || p[0] > h[1][0] || p[2] < h[2][2] || p[2] > h[0][2])
                continue;
            auto &c = stop.clamp;
            if (c[0][0] <= c[1][0] && c[2][2] <= c[0][2]) {
                p[0] = std::clamp(p[0], c[0][0], c[1][0]);
                p[2] = std::clamp(p[2], c[2][2], c[0][2]);
            }
        } else {
            constexpr unsigned order[]{0, 2, 3, 1};
            bool hit = true;
            for (unsigned k = 0; k < 4; ++k)
                if (cross2(sub(h[order[(k + 1) % 4]], h[order[k]]), sub(p, h[order[k]])) < 0)
                    hit = false;
            if (hit) {
                auto d = sub(h[1], h[0]);
                float length = d[0] * d[0] + d[2] * d[2];
                if (length > 1e-8f) {
                    float t = ((p[0] - h[0][0]) * d[0] + (p[2] - h[0][2]) * d[2]) / length;
                    p[0] = h[0][0] + t * d[0];
                    p[2] = h[0][2] + t * d[2];
                }
            }
        }
    }
    camera.eye = add(camera.eye, sub(camera.target, original));
    camera_region = selected;
}
std::vector<std::vector<Matrix>> evaluate_scene_poses(const std::vector<SceneSkeleton> &rigs,
                                                      const PlayerController &player,
                                                      double seconds, float hour, bool enabled) {
    std::vector<std::vector<Matrix>> poses;
    poses.reserve(rigs.size());
    for (auto &rig : rigs) {
        if (rig.player < 0) {
            auto pose = evaluate_skeleton(rig, seconds, hour, enabled);
            if (rig.parent_skeleton >= 0) {
                auto parent =
                    pose_multiply(poses.at(rig.parent_skeleton).at(rig.parent_joint),
                                  rigs.at(rig.parent_skeleton).joints.at(rig.parent_joint).bind);
                parent = pose_multiply(parent, rig.attachment_transform);
                for (auto &m : pose)
                    m = pose_multiply(parent, m);
            }
            poses.push_back(std::move(pose));
            continue;
        }
        if (!player.active || rig.player != int(player.appearance)) {
            poses.emplace_back();
            continue;
        }
        auto animated = rig;
        animated.motion = rig.locomotion.at(player.motion);
        animated.tracks.clear();
        for (auto &joint : animated.joints) {
            auto t = std::find_if(animated.motion.tracks.begin(), animated.motion.tracks.end(),
                                  [&](const auto &track) {
                                      return track.name == joint.name;
                                  });
            animated.tracks.push_back(
                t == animated.motion.tracks.end() ? -1 : int(t - animated.motion.tracks.begin()));
        }
        auto pose = evaluate_skeleton(animated, player.seconds, hour, true);
        auto transform =
            rig.parent_skeleton >= 0
                ? pose_multiply(poses.at(rig.parent_skeleton).at(rig.parent_joint),
                                rigs.at(rig.parent_skeleton).joints.at(rig.parent_joint).bind)
                : player.transform();
        for (auto &m : pose)
            m = pose_multiply(transform, m);
        poses.push_back(std::move(pose));
    }
    return poses;
}

}
