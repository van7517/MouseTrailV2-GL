#include "trail.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

namespace {

float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b) {
    h = h - std::floor(h);
    const float i = std::floor(h * 6.0f);
    const float f = h * 6.0f - i;
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);
    switch (static_cast<int>(i) % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
}

void pastel_rainbow(float hue, float& r, float& g, float& b) {
    hsv_to_rgb(hue, 0.42f, 0.97f, r, g, b);
}

struct Sample {
    float x = 0, y = 0;
    float arc = 0;   // 0 tail .. 1 head along path
    float remain = 1; // 1 fresh .. 0 fully aged out (per-point lifetime)
};

void catmull(const TrailPoint& p0, const TrailPoint& p1, const TrailPoint& p2, const TrailPoint& p3,
             float u, float& x, float& y) {
    const float u2 = u * u;
    const float u3 = u2 * u;
    x = 0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * u +
                (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * u2 +
                (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * u3);
    y = 0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * u +
                (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * u2 +
                (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * u3);
}

float smooth01(float t) {
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline void color_premul(float r, float g, float b, float a) {
    a = clampf(a, 0.0f, 1.0f);
    glColor4f(r * a, g * a, b * a, a);
}

// Build long continuous samples with arc param + per-point remaining life.
std::vector<Sample> build_samples(const std::vector<TrailPoint>& pts, double now, double life, float spacing) {
    std::vector<Sample> out;
    if (pts.size() < 2) return out;

    struct Dense {
        float x, y;
        double t;
    };
    std::vector<Dense> dense;
    const int n = static_cast<int>(pts.size());
    for (int i = 0; i < n - 1; ++i) {
        const TrailPoint& p0 = pts[std::max(0, i - 1)];
        const TrailPoint& p1 = pts[i];
        const TrailPoint& p2 = pts[i + 1];
        const TrailPoint& p3 = pts[std::min(n - 1, i + 2)];
        const float dist = std::hypot(p2.x - p1.x, p2.y - p1.y);
        const int steps = std::max(1, static_cast<int>(dist / spacing));
        for (int s = 0; s < steps; ++s) {
            const float u = static_cast<float>(s) / static_cast<float>(steps);
            float x, y;
            catmull(p0, p1, p2, p3, u, x, y);
            const double t = p1.t + (p2.t - p1.t) * static_cast<double>(u);
            dense.push_back({x, y, t});
        }
    }
    dense.push_back({pts.back().x, pts.back().y, pts.back().t});
    if (dense.size() < 2) return out;

    std::vector<float> seg(dense.size(), 0.0f);
    for (size_t i = 1; i < dense.size(); ++i) {
        seg[i] = seg[i - 1] + std::hypot(dense[i].x - dense[i - 1].x, dense[i].y - dense[i - 1].y);
    }
    const float total = seg.back() > 0.0f ? seg.back() : 1.0f;

    out.reserve(dense.size());
    for (size_t i = 0; i < dense.size(); ++i) {
        Sample s;
        s.x = dense[i].x;
        s.y = dense[i].y;
        s.arc = seg[i] / total; // 0 = oldest end, 1 = cursor end
        const float age = static_cast<float>((now - dense[i].t) / life);
        // remain: 1 at birth, 0 at expire. Ease so mid-life stays thick longer.
        s.remain = smooth01(1.0f - clampf(age, 0.0f, 1.0f));
        out.push_back(s);
    }
    return out;
}

// Soft filled ribbon: center solid, edges alpha=0 (no separate "border" pass).
// This is a long continuous bar that thins by WIDTH, not by chopping length.
void draw_soft_bar(const std::vector<Sample>& samples,
                   float origin_x, float origin_y, float screen_h,
                   float base_half_w, float base_a,
                   float hue_head, float hue_span) {
    if (samples.size() < 2) return;

    auto to_gl = [&](float x, float y, float& gx, float& gy) {
        gx = x - origin_x;
        gy = screen_h - (y - origin_y);
    };

    // Each segment: two triangles forming a quad with soft left/right edges.
    for (size_t i = 0; i + 1 < samples.size(); ++i) {
        const Sample& a = samples[i];
        const Sample& b = samples[i + 1];

        float dx = b.x - a.x;
        float dy = b.y - a.y;
        float len = std::hypot(dx, dy);
        if (len < 1e-4f) continue;
        const float nx = -dy / len;
        const float ny = dx / len;

        // Shape: long bar tapered by arc (head thicker) AND by remain (ages thin the bar).
        // Key: remain thins the stroke in place; we do not rely on deleting tail points for the look.
        auto width_at = [&](const Sample& s) {
            // Geometric taper along the long bar: tail thin, head thicker
            const float geo = 0.20f + 0.80f * smooth01(s.arc);
            // Age thinning: whole segment shrinks in width as it ages (鍙樼粏娑堝け)
            const float age_w = smooth01(std::pow(std::max(s.remain, 0.0f), 0.85f));
            return base_half_w * geo * age_w;
        };
        auto alpha_at = [&](const Sample& s) {
            // Alpha stays relatively stable; primary disappear is width->0
            const float age_a = 0.55f + 0.45f * smooth01(s.remain);
            return base_a * age_a;
        };

        const float wa = width_at(a);
        const float wb = width_at(b);
        if (wa < 0.05f && wb < 0.05f) continue;

        float ax, ay, bx, by;
        to_gl(a.x, a.y, ax, ay);
        to_gl(b.x, b.y, bx, by);
        const float gnx = nx;
        const float gny = -ny;

        float ra, ga, ba, rb, gb, bb;
        pastel_rainbow(hue_head - (1.0f - a.arc) * hue_span, ra, ga, ba);
        pastel_rainbow(hue_head - (1.0f - b.arc) * hue_span, rb, gb, bb);
        const float aa = alpha_at(a);
        const float ab = alpha_at(b);

        // 4 corners: outer edge alpha 0, creates soft edge without border layer
        // aL aR
        // bL bR
        const float aLx = ax + gnx * wa, aLy = ay + gny * wa;
        const float aRx = ax - gnx * wa, aRy = ay - gny * wa;
        const float bLx = bx + gnx * wb, bLy = by + gny * wb;
        const float bRx = bx - gnx * wb, bRy = by - gny * wb;

        // Center line points (full alpha)
        const float aCx = ax, aCy = ay;
        const float bCx = bx, bCy = by;

        // Left half soft: edge(0) - center(full)
        glBegin(GL_TRIANGLE_STRIP);
        color_premul(ra, ga, ba, 0.0f); glVertex2f(aLx, aLy);
        color_premul(ra, ga, ba, aa);   glVertex2f(aCx, aCy);
        color_premul(rb, gb, bb, 0.0f); glVertex2f(bLx, bLy);
        color_premul(rb, gb, bb, ab);   glVertex2f(bCx, bCy);
        glEnd();

        // Right half soft: center(full) - edge(0)
        glBegin(GL_TRIANGLE_STRIP);
        color_premul(ra, ga, ba, aa);   glVertex2f(aCx, aCy);
        color_premul(ra, ga, ba, 0.0f); glVertex2f(aRx, aRy);
        color_premul(rb, gb, bb, ab);   glVertex2f(bCx, bCy);
        color_premul(rb, gb, bb, 0.0f); glVertex2f(bRx, bRy);
        glEnd();
    }
}

} // namespace

void TrailSystem::clear() {
    points.clear();
    particles.clear();
}

void TrailSystem::push(float x, float y, double now, float speed, const AppConfig& cfg) {
    bool add = true;
    if (!points.empty()) {
        if (std::hypot(x - points.back().x, y - points.back().y) < cfg.sample_min_distance) {
            add = false;
        }
    }

    if (add) {
        points.push_back({x, y, now, speed});
        if (cfg.triangle_particles && speed > 160.0f) {
            const float ang = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 6.2831853f;
            const float sp = 40.0f + static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 80.0f + speed * 0.03f;
            Particle p;
            p.x = x; p.y = y;
            p.vx = std::cos(ang) * sp;
            p.vy = std::sin(ang) * sp - 30.0f;
            p.birth = now;
            p.life = 0.22f + static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 0.2f;
            p.size = 2.0f + static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 2.0f;
            p.hue = cfg.rainbow_hue_head + (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 0.1f;
            p.rot = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 6.2831853f;
            p.spin = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 16.0f;
            particles.push_back(p);
            if (particles.size() > 40) particles.erase(particles.begin(), particles.end() - 40);
        }
    } else {
        points.back() = {x, y, now, speed};
    }

    if (static_cast<int>(points.size()) > cfg.max_points) {
        points.erase(points.begin(), points.end() - cfg.max_points);
    }
}

void TrailSystem::update(double now, float dt, const AppConfig& cfg) {
    // Keep points until fully thinned (remain~0). Slightly longer than lifetime
    // so width can reach zero before the bar is chopped short.
    const double life = cfg.trail_lifetime_ms / 1000.0 * 1.25;
    points.erase(std::remove_if(points.begin(), points.end(),
                                [&](const TrailPoint& p) { return now - p.t > life; }),
                 points.end());

    std::vector<Particle> alive;
    alive.reserve(particles.size());
    for (auto p : particles) {
        if (now - p.birth >= p.life) continue;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.vy += 240.0f * dt;
        p.vx *= 0.985f;
        p.rot += p.spin * dt;
        alive.push_back(p);
    }
    particles.swap(alive);
}

void TrailSystem::draw(float origin_x, float origin_y, float screen_h, double now, const AppConfig& cfg) const {
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_POINT_SMOOTH);

    if (points.size() >= 2) {
        const double life = std::max(0.08, cfg.trail_lifetime_ms / 1000.0);
        const auto samples = build_samples(points, now, life, 1.5f);
        if (samples.size() >= 2) {
            const float base_a = clampf(cfg.opacity, 0.05f, 1.0f);
            // half-width; stroke_scale 3.0 from user config means thicker bar
            const float half = std::max(0.8f, cfg.ribbon_thickness * cfg.stroke_scale * 0.22f);
            draw_soft_bar(samples, origin_x, origin_y, screen_h, half, base_a,
                          cfg.rainbow_hue_head, cfg.rainbow_hue_span);
        }
    }

    if (cfg.triangle_particles) {
        const float base_a = clampf(cfg.opacity, 0.05f, 1.0f);
        auto to_gl = [&](float x, float y, float& gx, float& gy) {
            gx = x - origin_x;
            gy = screen_h - (y - origin_y);
        };
        for (const auto& p : particles) {
            const float fade = clampf(1.0f - static_cast<float>((now - p.birth) / p.life), 0.0f, 1.0f);
            if (fade <= 0.0f) continue;
            float r, g, b;
            pastel_rainbow(p.hue, r, g, b);
            color_premul(r, g, b, base_a * fade * 0.55f);
            const float s = p.size * fade;
            float cx, cy;
            to_gl(p.x, p.y, cx, cy);
            glBegin(GL_TRIANGLES);
            for (int k = 0; k < 3; ++k) {
                const float ang = p.rot + 6.2831853f * static_cast<float>(k) / 3.0f;
                glVertex2f(cx + std::cos(ang) * s, cy + std::sin(ang) * s);
            }
            glEnd();
        }
    }
}
