// report.cpp — renders EnergyReport as a table, JSON or DOT.
#include "wattwise/report.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace ww {
namespace {

std::string num(double v, int prec = 1) {
    std::ostringstream s;
    s << std::fixed << std::setprecision(prec) << v;
    std::string r = s.str();
    // thousands separators for readability
    size_t dot = r.find('.');
    std::string intPart = r.substr(0, dot), frac = dot == std::string::npos ? "" : r.substr(dot);
    bool neg = !intPart.empty() && intPart[0] == '-';
    if (neg) intPart.erase(0, 1);
    std::string out;
    for (size_t i = 0; i < intPart.size(); ++i) {
        if (i && (intPart.size() - i) % 3 == 0) out += ',';
        out += intPart[i];
    }
    return (neg ? "-" : "") + out + frac;
}
std::string pct(double part, double whole) { return whole > 0 ? num(100.0 * part / whole, 1) + "%" : "-"; }
std::string pad(const std::string& s, size_t w, bool right = false) {
    if (s.size() >= w) return s;
    return right ? std::string(w - s.size(), ' ') + s : s + std::string(w - s.size(), ' ');
}
std::string lines(int a, int b) { return a == 0 ? "-" : a == b ? std::to_string(a) : std::to_string(a) + "-" + std::to_string(b); }
std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o + "\"";
}

}  // namespace

std::string textReport(const EnergyReport& r, const std::string& file, int topN) {
    std::ostringstream o;
    o << "WattWise energy report — " << file << "  [" << (r.mode == "static" ? "static estimate" : "dynamic profile") << "]\n";
    o << "Total estimated energy: " << num(r.total) << " pJ (model units; relative, not absolute joules)\n\n";

    o << "Top basic blocks (hotspots)\n";
    o << "  " << pad("#", 3) << pad("function", 12) << pad("block", 8) << pad("lines", 9) << pad("depth", 6)
      << pad("executions", 14, true) << pad("pJ/exec", 10, true) << pad("energy pJ", 16, true) << pad("share", 8, true) << "\n";
    int k = 0;
    for (auto& b : r.blocks) {
        if (k >= topN || b.energy <= 0) break;
        ++k;
        o << "  " << pad(std::to_string(k), 3) << pad(b.func, 12) << pad(b.label, 8) << pad(lines(b.firstLine, b.lastLine), 9)
          << pad(std::to_string(b.loopDepth), 6) << pad(num(b.freq, 0), 14, true) << pad(num(b.costPerExec), 10, true)
          << pad(num(b.energy), 16, true) << pad(pct(b.energy, r.total), 8, true) << "\n";
    }
    if (!r.loops.empty()) {
        o << "\nLoops (inclusive energy)\n";
        o << "  " << pad("function", 12) << pad("header", 8) << pad("line", 6) << pad("depth", 6) << pad("trip", 10, true)
          << pad("energy pJ", 16, true) << pad("share", 8, true) << "   trip-count source\n";
        for (auto& L : r.loops)
            o << "  " << pad(L.func, 12) << pad(L.headerLabel, 8) << pad(std::to_string(L.headerLine), 6) << pad(std::to_string(L.depth), 6)
              << pad(num(L.trip, 1), 10, true) << pad(num(L.energy), 16, true) << pad(pct(L.energy, r.total), 8, true) << "   " << L.tripHow << "\n";
    }
    o << "\nFunctions (exclusive energy)\n";
    for (auto& f : r.funcs)
        o << "  " << pad(f.name, 14) << pad("calls " + num(f.invocations, 0), 18) << pad(num(f.energy), 16, true)
          << pad(pct(f.energy, r.total), 8, true) << "\n";
    o << "\nEnergy by operation class\n";
    std::vector<std::pair<double, std::string>> cats;
    for (auto& [c, e] : r.byCategory) cats.push_back({e, c});
    std::sort(cats.rbegin(), cats.rend());
    for (auto& [e, c] : cats) o << "  " << pad(c, 16) << pad(num(e), 16, true) << pad(pct(e, r.total), 8, true) << "\n";
    o << "\nHottest source lines\n";
    std::vector<std::pair<double, std::pair<std::string, int>>> ls;
    for (auto& [kk, e] : r.byLine) ls.push_back({e, kk});
    std::sort(ls.begin(), ls.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < ls.size() && (int)i < topN; ++i)
        o << "  " << pad(ls[i].second.first + ":" + std::to_string(ls[i].second.second), 16) << pad(num(ls[i].first), 16, true)
          << pad(pct(ls[i].first, r.total), 8, true) << "\n";
    return o.str();
}

std::string comparisonReport(const EnergyReport& before, const EnergyReport& after,
                             const std::vector<PassStats>& passes, const HotSet& hot) {
    std::ostringstream o;
    o << "Optimisation (" << (hot.all ? "all code" : "profile-directed: hot lines/loops only") << ", " << before.mode << " model)\n";
    for (auto& p : passes) {
        o << "  " << pad(p.pass, 20) << p.changes << " change(s)\n";
        for (auto& l : p.log) o << "      " << l << "\n";
    }
    o << "\n  " << pad("function", 14) << pad("before pJ", 16, true) << pad("after pJ", 16, true) << pad("delta", 10, true) << "\n";
    for (auto& fb : before.funcs) {
        double a = 0;
        for (auto& fa : after.funcs) if (fa.name == fb.name) a = fa.energy;
        o << "  " << pad(fb.name, 14) << pad(num(fb.energy), 16, true) << pad(num(a), 16, true)
          << pad(fb.energy > 0 ? num(100.0 * (a - fb.energy) / fb.energy, 1) + "%" : "-", 10, true) << "\n";
    }
    o << "  " << pad("TOTAL", 14) << pad(num(before.total), 16, true) << pad(num(after.total), 16, true)
      << pad(before.total > 0 ? num(100.0 * (after.total - before.total) / before.total, 1) + "%" : "-", 10, true) << "\n";
    return o.str();
}

static void jsonBody(std::ostringstream& o, const EnergyReport& r) {
    o << "{\"mode\":" << jstr(r.mode) << ",\"total_pj\":" << r.total << ",\"blocks\":[";
    for (size_t i = 0; i < r.blocks.size(); ++i) {
        auto& b = r.blocks[i];
        o << (i ? "," : "") << "{\"func\":" << jstr(b.func) << ",\"block\":" << jstr(b.label) << ",\"first_line\":" << b.firstLine
          << ",\"last_line\":" << b.lastLine << ",\"depth\":" << b.loopDepth << ",\"executions\":" << b.freq
          << ",\"pj_per_exec\":" << b.costPerExec << ",\"energy_pj\":" << b.energy << "}";
    }
    o << "],\"loops\":[";
    for (size_t i = 0; i < r.loops.size(); ++i) {
        auto& L = r.loops[i];
        o << (i ? "," : "") << "{\"func\":" << jstr(L.func) << ",\"header\":" << jstr(L.headerLabel) << ",\"line\":" << L.headerLine
          << ",\"depth\":" << L.depth << ",\"trip\":" << L.trip << ",\"trip_source\":" << jstr(L.tripHow) << ",\"energy_pj\":" << L.energy << "}";
    }
    o << "],\"functions\":[";
    for (size_t i = 0; i < r.funcs.size(); ++i)
        o << (i ? "," : "") << "{\"name\":" << jstr(r.funcs[i].name) << ",\"invocations\":" << r.funcs[i].invocations
          << ",\"energy_pj\":" << r.funcs[i].energy << "}";
    o << "],\"by_category\":{";
    bool first = true;
    for (auto& [c, e] : r.byCategory) { o << (first ? "" : ",") << jstr(c) << ":" << e; first = false; }
    o << "},\"by_line\":[";
    first = true;
    for (auto& [k, e] : r.byLine) { o << (first ? "" : ",") << "{\"func\":" << jstr(k.first) << ",\"line\":" << k.second << ",\"energy_pj\":" << e << "}"; first = false; }
    o << "]}";
}

std::string jsonReport(const EnergyReport& r, const EnergyReport* after, const std::vector<PassStats>* passes, const std::string& file) {
    std::ostringstream o;
    o << "{\"file\":" << jstr(file) << ",\"units\":\"pJ (model)\",\"before\":";
    jsonBody(o, r);
    if (after) {
        o << ",\"after\":";
        jsonBody(o, *after);
        o << ",\"reduction_pct\":" << (r.total > 0 ? 100.0 * (r.total - after->total) / r.total : 0.0);
    }
    if (passes) {
        o << ",\"passes\":[";
        for (size_t i = 0; i < passes->size(); ++i) {
            auto& p = (*passes)[i];
            o << (i ? "," : "") << "{\"pass\":" << jstr(p.pass) << ",\"changes\":" << p.changes << ",\"log\":[";
            for (size_t j = 0; j < p.log.size(); ++j) o << (j ? "," : "") << jstr(p.log[j]);
            o << "]}";
        }
        o << "]";
    }
    o << "}\n";
    return o.str();
}

std::string dotCFG(const IRFunc& f, const CFG& g, const EnergyReport& r) {
    std::ostringstream o;
    o << "digraph \"" << f.name << "\" {\n  node [shape=box, fontname=\"monospace\", fontsize=10, style=filled];\n";
    o << "  label=\"" << f.name << " — block energy (" << r.mode << ")\"; labelloc=t;\n";
    for (auto& b : g.blocks) {
        double e = 0;
        for (auto& be : r.blocks) if (be.func == f.name && be.block == b.id) e = be.energy;
        double share = r.total > 0 ? e / r.total : 0;
        // white -> red heat by energy share
        int gb = (int)std::lround(255 * (1.0 - std::min(1.0, share * 2.0)));
        char color[16];
        std::snprintf(color, sizeof color, "#ff%02x%02x", gb, gb);
        std::string body;
        for (int i = b.begin; i < b.end; ++i) {
            std::string s = f.code[i].str();
            for (auto& ch : s) if (ch == '"') ch = '\'';
            body += s + "\\l";
        }
        o << "  b" << b.id << " [fillcolor=\"" << color << "\", label=\"" << (b.label.empty() ? "B" + std::to_string(b.id) : b.label)
          << "  " << num(e) << " pJ (" << pct(e, r.total) << ")\\l" << body << "\"];\n";
    }
    for (auto& b : g.blocks)
        for (int s : b.succ) {
            bool back = g.dominates(s, b.id);
            o << "  b" << b.id << " -> b" << s << (back ? " [color=blue, penwidth=2, label=\"back\"]" : "") << ";\n";
        }
    o << "}\n";
    return o.str();
}

}  // namespace ww
