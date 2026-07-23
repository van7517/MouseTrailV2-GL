#pragma once
#include "config.hpp"
#include <vector>

struct TrailPoint {
    float x = 0;
    float y = 0;
    double t = 0;
    float speed = 0;
};

struct Particle {
    float x = 0, y = 0;
    float vx = 0, vy = 0;
    double birth = 0;
    float life = 0.3f;
    float size = 4.0f;
    float hue = 0;
    float rot = 0;
    float spin = 0;
};

struct TrailSystem {
    std::vector<TrailPoint> points;
    std::vector<Particle> particles;

    void clear();
    void push(float x, float y, double now, float speed, const AppConfig& cfg);
    void update(double now, float dt, const AppConfig& cfg);
    void draw(float origin_x, float origin_y, float screen_h, double now, const AppConfig& cfg) const;
};
