#include "config.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
}

bool match(const std::string& s, size_t& i, const char* lit) {
    size_t j = 0;
    while (lit[j]) {
        if (i + j >= s.size() || s[i + j] != lit[j]) return false;
        ++j;
    }
    i += j;
    return true;
}

bool parse_string(const std::string& s, size_t& i, std::string& out) {
    skip_ws(s, i);
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size() && s[i] != '"') {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            out.push_back(s[i++]);
        } else {
            out.push_back(s[i++]);
        }
    }
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    return true;
}

bool parse_number(const std::string& s, size_t& i, double& out) {
    skip_ws(s, i);
    char* end = nullptr;
    out = std::strtod(s.c_str() + i, &end);
    if (end == s.c_str() + i) return false;
    i = static_cast<size_t>(end - s.c_str());
    return true;
}

bool parse_bool(const std::string& s, size_t& i, bool& out) {
    skip_ws(s, i);
    if (match(s, i, "true")) { out = true; return true; }
    if (match(s, i, "false")) { out = false; return true; }
    return false;
}

bool find_key(const std::string& s, const char* key, size_t& pos) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = s.find(pat);
    if (p == std::string::npos) return false;
    p += pat.size();
    skip_ws(s, p);
    if (p >= s.size() || s[p] != ':') return false;
    ++p;
    pos = p;
    return true;
}

} // namespace

AppConfig AppConfig::load(const std::string& path) {
    AppConfig c;
    const std::string s = read_file(path);
    if (s.empty()) {
        c.save(path);
        return c;
    }

    double num = 0;
    bool b = false;
    std::string str;

    auto grab_f = [&](const char* key, float& field) {
        size_t q = 0;
        if (!find_key(s, key, q)) return;
        if (parse_number(s, q, num)) field = static_cast<float>(num);
    };
    auto grab_i = [&](const char* key, int& field) {
        size_t q = 0;
        if (!find_key(s, key, q)) return;
        if (parse_number(s, q, num)) field = static_cast<int>(num);
    };
    auto grab_b = [&](const char* key, bool& field) {
        size_t q = 0;
        if (!find_key(s, key, q)) return;
        if (parse_bool(s, q, b)) field = b;
    };
    auto grab_s = [&](const char* key, std::string& field) {
        size_t q = 0;
        if (!find_key(s, key, q)) return;
        if (parse_string(s, q, str)) field = str;
    };

    grab_f("opacity", c.opacity);
    grab_f("ribbon_thickness", c.ribbon_thickness);
    grab_b("triangle_particles", c.triangle_particles);
    grab_b("not_when_cursor_hidden", c.not_when_cursor_hidden);
    grab_i("trail_lifetime_ms", c.trail_lifetime_ms);
    grab_f("sample_min_distance", c.sample_min_distance);
    grab_i("max_points", c.max_points);
    grab_i("fps_limit", c.fps_limit);
    grab_s("toggle_hotkey", c.toggle_hotkey);
    grab_s("quit_hotkey", c.quit_hotkey);
    grab_f("rainbow_hue_head", c.rainbow_hue_head);
    grab_f("rainbow_hue_span", c.rainbow_hue_span);
    grab_f("stroke_scale", c.stroke_scale);

    size_t q = 0;
    if (find_key(s, "blacklisted_processes", q)) {
        skip_ws(s, q);
        if (q < s.size() && s[q] == '[') {
            ++q;
            c.blacklisted_processes.clear();
            while (q < s.size()) {
                skip_ws(s, q);
                if (q < s.size() && s[q] == ']') break;
                std::string item;
                if (!parse_string(s, q, item)) break;
                c.blacklisted_processes.push_back(item);
                skip_ws(s, q);
                if (q < s.size() && s[q] == ',') { ++q; continue; }
                if (q < s.size() && s[q] == ']') break;
            }
        }
    }

    if (c.fps_limit < 30) c.fps_limit = 30;
    if (c.fps_limit > 240) c.fps_limit = 240;
    if (c.max_points < 8) c.max_points = 8;
    if (c.max_points > 256) c.max_points = 256;
    return c;
}

void AppConfig::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return;
    f << "{\n";
    f << "  \"opacity\": " << opacity << ",\n";
    f << "  \"ribbon_thickness\": " << ribbon_thickness << ",\n";
    f << "  \"triangle_particles\": " << (triangle_particles ? "true" : "false") << ",\n";
    f << "  \"not_when_cursor_hidden\": " << (not_when_cursor_hidden ? "true" : "false") << ",\n";
    f << "  \"blacklisted_processes\": [";
    for (size_t i = 0; i < blacklisted_processes.size(); ++i) {
        if (i) f << ", ";
        f << "\"" << blacklisted_processes[i] << "\"";
    }
    f << "],\n";
    f << "  \"trail_lifetime_ms\": " << trail_lifetime_ms << ",\n";
    f << "  \"sample_min_distance\": " << sample_min_distance << ",\n";
    f << "  \"max_points\": " << max_points << ",\n";
    f << "  \"fps_limit\": " << fps_limit << ",\n";
    f << "  \"toggle_hotkey\": \"" << toggle_hotkey << "\",\n";
    f << "  \"quit_hotkey\": \"" << quit_hotkey << "\",\n";
    f << "  \"rainbow_hue_head\": " << rainbow_hue_head << ",\n";
    f << "  \"rainbow_hue_span\": " << rainbow_hue_span << ",\n";
    f << "  \"stroke_scale\": " << stroke_scale << "\n";
    f << "}\n";
}

int hotkey_to_vk(const std::string& name) {
    std::string u;
    u.reserve(name.size());
    for (char ch : name) u.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    if (u == "F1") return 0x70;
    if (u == "F2") return 0x71;
    if (u == "F3") return 0x72;
    if (u == "F4") return 0x73;
    if (u == "F5") return 0x74;
    if (u == "F6") return 0x75;
    if (u == "F7") return 0x76;
    if (u == "F8") return 0x77;
    if (u == "F9") return 0x78;
    if (u == "F10") return 0x79;
    if (u == "F11") return 0x7A;
    if (u == "F12") return 0x7B;
    return 0x77;
}
