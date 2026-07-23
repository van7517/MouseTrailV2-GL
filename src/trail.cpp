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
    hsv_to_rgb(hue, 0.38f, 0.98f, r, g, b);
}

struct Sample {
    float x, y, p;
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

std::vector<Sample> resample(const std::vector<TrailPoint>& pts, float spacing) {
    std::vector<Sample> out;
    if (pts.size() < 2) {
        for (const auto& p : pts) out.push_back({p.x, p.y, 1.0f});
        return out;
    }

    std::vector<std::pair<float, float>> dense;
    const int n = static_cast<int>(pts.size());
    for (int i = 0; i < n - 1; ++i) {
        const TrailPoint& p0 = pts[std::max(0, i - 1)];
        const TrailPoint& p1 = pts[i];
        const TrailPoint& p2 = pts[i + 1];
        const TrailPoint& p3 = pts[std::min(n - 1, i + 2)];
        const float dist = std::hypot(p2.x - p1.x, p2.y - p1.y);
        const int steps = std::max(1, static_cast<int>(dist / spacing));
        for (int s = 0; s < steps; ++s) {
            float x, y;
            catmull(p0, p1, p2, p3, static_cast<float>(s) / static_cast<float>(steps), x, y);
            dense.emplace_back(x, y);
        }
    }
    dense.emplace_back(pts.back().x, pts.back().y);

    if (dense.size() < 2) {
        if (!dense.empty()) out.push_back({dense[0].first, dense[0].second, 1.0f});
        return out;
    }

    std::vector<float> seg(dense.size(), 0.0f);
    for (size_t i = 1; i < dense.size(); ++i) {
        seg[i] = seg[i - 1] + std::hypot(dense[i].first - dense[i - 1].first,
                                         dense[i].second - dense[i - 1].second);
    }
    const float total = seg.back() > 0.0f ? seg.back() : 1.0f;
    out.reserve(dense.size());
    for (size_t i = 0; i < dense.size(); ++i) {
        out.push_back({dense[i].first, dense[i].second, seg[i] / total});
    }
    return out;
}

float smooth01(float t) {
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline void color_premul(float r, float g, float b, float a) {
    a = clampf(a, 0.0f, 1.0f);
    glColor4f(r * a, g * a, b * a, a);
}

void draw_stroke_strip(const std::vector<Sample>& samples,
                       float origin_x, float origin_y, float screen_h,
                       float half_w, float base_a,
                       float hue_head, float hue_span,
                       float alpha_mul) {
    if (samples.size() < 2 || half_w <= 0.01f) return;

    auto to_gl = [&](float x, float y, float& gx, float& gy) {
        gx = x - origin_x;
        gy = screen_h - (y - origin_y);
    };

    glBegin(GL_TRIANGLE_STRIP);
    for (size_t i = 0; i < samples.size(); ++i) {
        float dx, dy;
        if (i + 1 < samples.size()) {
            dx = samples[i + 1].x - samples[i].x;
            dy = samples[i + 1].y - samples[i].y;
        } else {
            dx = samples[i].x - samples[i - 1].x;
            dy = samples[i].y - samples[i - 1].y;
        }
        float len = std::hypot(dx, dy);
        if (len < 1e-4f) {
            dx = 1.0f;
            dy = 0.0f;
            len = 1.0f;
        }
        const float nx = -dy / len;
        const float ny = dx / len;

        const float p = clampf(samples[i].p, 0.0f, 1.0f);
        const float width_env = smooth01(std::pow(p, 0.50f));
        const float alpha_env = smooth01(std::pow(p, 0.40f));

        const float hue = hue_head - (1.0f - p) * hue_span;
        float r, g, b;
        pastel_rainbow(hue, r, g, b);

        float w = half_w * (0.80f + 0.30f * p) * width_env;
        if (w > 0.0f && w < 0.30f) w = 0.30f * width_env;

        const float alpha = base_a * alpha_mul * (0.60f + 0.40f * alpha_env);

        float cx, cy;
        to_gl(samples[i].x, samples[i].y, cx, cy);
        color_premul(r, g, b, alpha);
        glVertex2f(cx + nx * w, cy + (-ny) * w);
        glVertex2f(cx - nx * w, cy - (-ny) * w);
    }
    glEnd();
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
            p.x = x;
            p.y = y;
            p.vx = std::cos(ang) * sp;
            p.vy = std::sin(ang) * sp - 30.0f;
            p.birth = now;
            p.life = 0.22f + static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 0.2f;
            p.size = 2.0f + static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 2.0f;
            p.hue = cfg.rainbow_hue_head + (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 0.1f;
            p.rot = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) * 6.2831853f;
            p.spin = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 16.0f;
            particles.push_back(p);
            if (particles.size() > 40) {
                particles.erase(particles.begin(), particles.end() - 40);
            }
        }
    } else {
        points.back() = {x, y, now, speed};
    }

    if (static_cast<int>(points.size()) > cfg.max_points) {
        points.erase(points.begin(), points.end() - cfg.max_points);
    }
}

void TrailSystem::update(double now, float dt, const AppConfig& cfg) {
    const double life = cfg.trail_lifetime_ms / 1000.0 * 1.40;
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
        auto samples = resample(points, 1.6f);
        if (samples.size() >= 2) {
            const double life = std::max(0.08, cfg.trail_lifetime_ms / 1000.0);
            const double head_t = points.back().t;
            const double tail_t = points.front().t;

            for (auto& s : samples) {
                const double age_t = tail_t + (head_t - tail_t) * static_cast<double>(s.p);
                const float age = static_cast<float>((now - age_t) / life);
                const float remain = 1.0f - clampf(age, 0.0f, 1.0f);
                const float thin = smooth01(std::pow(std::max(remain, 0.0f), 0.50f));
                s.p = clampf(0.20f + 0.80f * s.p, 0.0f, 1.0f) * thin;
            }

            const float base_a = clampf(cfg.opacity, 0.05f, 1.0f);
            const float half = std::max(0.65f, cfg.ribbon_thickness * cfg.stroke_scale * 0.17f);

            draw_stroke_strip(samples, origin_x, origin_y, screen_h, half * 2.0f, base_a,
                              cfg.rainbow_hue_head, cfg.rainbow_hue_span, 0.20f);
            draw_stroke_strip(samples, origin_x, origin_y, screen_h, half * 1.15f, base_a,
                              cfg.rainbow_hue_head, cfg.rainbow_hue_span, 0.50f);
            draw_stroke_strip(samples, origin_x, origin_y, screen_h, half, base_a,
                              cfg.rainbow_hue_head, cfg.rainbow_hue_span, 0.92f);
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
