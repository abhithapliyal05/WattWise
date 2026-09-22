// main.cpp — the `wattwise` command-line driver (Module M4).
//
// Pipeline: lex -> parse -> semantic -> IR -> CFG -> energy analysis
//           [-> hot-set selection -> SR/CSE/LICM -> re-profile -> verify]
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include "wattwise/analysis.hpp"
#include "wattwise/energy_model.hpp"
#include "wattwise/interpreter.hpp"
#include "wattwise/irgen.hpp"
#include "wattwise/lexer.hpp"
#include "wattwise/optimize/passes.hpp"
#include "wattwise/parser.hpp"
#include "wattwise/report.hpp"
#include "wattwise/semantic.hpp"

using namespace ww;

namespace {

const char* kUsage = R"(usage: wattwise <file.cpp> [options]

Front-end / IR
  --tokens            print the token stream and stop
  --ast               print the annotated AST and stop
  --ir                print three-address code
  --cfg               print basic blocks, dominator-based loops
  --dot DIR           write one Graphviz CFG (energy heat-map) per function
Energy analysis
  --static            static estimate (default)
  --dynamic           dynamic profile (runs the IR interpreter)
  --both              both, plus static-vs-dynamic hotspot agreement
  --costs FILE        override the energy cost table
  --top N             rows in hotspot tables (default 8)
Optimisation
  --opt[=LIST]        run passes (sr,cse,licm | all; default all), re-profile,
                      and verify output is unchanged
  --opt-all           optimise everything, not only hot code
  --coverage X        hot set = lines covering X of energy (default 0.90)
  --ir-opt            print optimised IR
Execution
  --run               execute the (unoptimised) IR; print program output only
  --run-opt           execute the optimised IR; print program output only
Output
  --json FILE         machine-readable report
  --quiet             no report tables
Exit status: 0 ok, 1 compile error, 2 runtime error, 3 optimisation changed
program output, 64 usage error.
)";

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open '" + path + "'");
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void printCFG(const IRFunc& f, const CFG& g) {
    std::cout << "func " << f.name << ": " << g.blocks.size() << " blocks, " << g.loops.size() << " loop(s)\n";
    for (auto& b : g.blocks) {
        std::cout << "  B" << b.id << (b.label.empty() ? "" : " (" + b.label + ")") << "  quads [" << b.begin << "," << b.end << ")"
                  << "  depth " << g.depth[b.id] << (b.reachable ? "" : "  UNREACHABLE") << "  -> ";
        for (int s : b.succ) std::cout << "B" << s << " ";
        std::cout << "\n";
    }
    for (auto& L : g.loops) {
        std::cout << "  loop header B" << L.header << " depth " << L.depth << " blocks {";
        bool first = true;
        for (int x : L.blocks) { std::cout << (first ? "" : ",") << "B" << x; first = false; }
        std::cout << "} latches {";
        first = true;
        for (int x : L.latches) { std::cout << (first ? "" : ",") << "B" << x; first = false; }
        std::cout << "}\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << kUsage; return 64; }
    std::string file;
    bool tokens = false, ast = false, ir = false, cfg = false, irOpt = false, quiet = false;
    bool wantStatic = true, wantDynamic = false, doOpt = false, optAll = false, run = false, runOpt = false;
    std::string dotDir, jsonPath, costPath, passList = "all";
    int top = 8;
    double coverage = 0.90;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << "missing value for " << a << "\n"; std::exit(64); }
            return argv[++i];
        };
        if (a == "--tokens") tokens = true;
        else if (a == "--ast") ast = true;
        else if (a == "--ir") ir = true;
        else if (a == "--cfg") cfg = true;
        else if (a == "--dot") dotDir = next();
        else if (a == "--static") { wantStatic = true; wantDynamic = false; }
        else if (a == "--dynamic") { wantStatic = false; wantDynamic = true; }
        else if (a == "--both") { wantStatic = wantDynamic = true; }
        else if (a == "--costs") costPath = next();
        else if (a == "--top") top = std::stoi(next());
        else if (a == "--opt") doOpt = true;
        else if (a.rfind("--opt=", 0) == 0) { doOpt = true; passList = a.substr(6); }
        else if (a == "--opt-all") { doOpt = true; optAll = true; }
        else if (a == "--coverage") coverage = std::stod(next());
        else if (a == "--ir-opt") { irOpt = true; doOpt = true; }
        else if (a == "--run") run = true;
        else if (a == "--run-opt") { runOpt = true; doOpt = true; quiet = true; }
        else if (a == "--json") jsonPath = next();
        else if (a == "--quiet") quiet = true;
        else if (a == "-h" || a == "--help") { std::cout << kUsage; return 0; }
        else if (!a.empty() && a[0] == '-') { std::cerr << "unknown option " << a << "\n" << kUsage; return 64; }
        else file = a;
    }
    if (file.empty()) { std::cerr << kUsage; return 64; }

    CostModel model = CostModel::defaults();
    if (!costPath.empty()) {
        std::string err;
        if (!model.loadFile(costPath, err)) { std::cerr << "wattwise: " << err << "\n"; return 64; }
    }

    IRProgram prog;
    try {
        std::string src = readFile(file);
        auto toks = lex(src);
        if (tokens) {
            for (auto& t : toks) std::cout << t.line << ":" << t.col << "\t" << tokName(t.kind) << "\t" << t.text << "\n";
            return 0;
        }
        Program p = parse(toks);
        SemaResult sema = analyze(p);
        for (auto& w : sema.warnings) std::cerr << file << ":" << w.str() << "\n";
        if (ast) { std::cout << dumpAst(p); return 0; }
        prog = generateIR(p, sema);
    } catch (const CompileError& e) {
        for (auto& d : e.diags()) std::cerr << file << ":" << d.str() << "\n";
        std::cerr << "wattwise: " << e.diags().size() << " " << e.phase() << " error(s); compilation stopped\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "wattwise: " << e.what() << "\n";
        return 64;
    }

    if (run) {
        RunResult rr = interpret(prog);
        std::cout << rr.output << std::flush;
        if (!rr.ok) { std::cerr << "wattwise: " << rr.error << "\n"; return 2; }
        return rr.exitCode & 0xff;
    }

    CFGMap cfgs = buildAllCFGs(prog);
    if (ir) std::cout << prog.str();
    if (cfg) for (auto& f : prog.funcs) printCFG(f, cfgs.at(f.name));

    EnergyReport rs, rd;
    RunResult baseRun;
    bool needRun = wantDynamic || doOpt;
    if (needRun) {
        baseRun = interpret(prog);
        if (!baseRun.ok) { std::cout << baseRun.output; std::cerr << "wattwise: " << baseRun.error << "\n"; return 2; }
    }
    if (wantStatic) rs = analyzeStatic(prog, cfgs, model);
    if (wantDynamic) rd = analyzeDynamic(prog, cfgs, model, baseRun);
    const EnergyReport& primary = wantStatic ? rs : rd;

    if (!quiet) {
        if (wantStatic) std::cout << textReport(rs, file, top) << "\n";
        if (wantDynamic) std::cout << textReport(rd, file, top) << "\n";
        if (wantStatic && wantDynamic) {
            RankAgreement ra = compareRankings(rs, rd, 3);
            std::cout << "Hotspot agreement (static vs dynamic, top-" << ra.k << " blocks): overlap " << ra.overlap << "/" << ra.k
                      << ", #1 " << (ra.topMatches ? "matches" : "differs") << ", set " << (ra.setMatches ? "matches" : "differs") << "\n\n";
        }
    }
    if (!dotDir.empty()) {
        std::filesystem::create_directories(dotDir);
        for (auto& f : prog.funcs) {
            std::ofstream o(dotDir + "/" + f.name + ".dot");
            o << dotCFG(f, cfgs.at(f.name), primary);
        }
    }

    EnergyReport after;
    std::vector<PassStats> stats;
    if (doOpt) {
        HotSet hot = selectHot(primary, coverage);
        hot.all = optAll;
        IRProgram opt = prog;
        std::vector<std::string> passes;
        std::stringstream ss(passList);
        for (std::string s; std::getline(ss, s, ',');) if (!s.empty()) passes.push_back(s);
        try { stats = optimize(opt, hot, passes); }
        catch (const std::exception& e) { std::cerr << "wattwise: " << e.what() << "\n"; return 64; }
        CFGMap ocfgs = buildAllCFGs(opt);
        RunResult optRun = interpret(opt);

        if (runOpt) {
            std::cout << optRun.output << std::flush;
            if (!optRun.ok) { std::cerr << "wattwise: " << optRun.error << "\n"; return 2; }
            return optRun.exitCode & 0xff;
        }
        if (irOpt) std::cout << opt.str();
        after = wantStatic ? analyzeStatic(opt, ocfgs, model) : analyzeDynamic(opt, ocfgs, model, optRun);
        if (!quiet) std::cout << comparisonReport(primary, after, stats, hot) << "\n";

        // differential check: same output, same exit code
        bool same = optRun.ok && optRun.output == baseRun.output && optRun.exitCode == baseRun.exitCode;
        std::cout << "Verification: optimised program output " << (same ? "IDENTICAL" : "DIFFERS")
                  << " (" << baseRun.steps << " -> " << optRun.steps << " IR instructions executed)\n";
        if (!jsonPath.empty()) { std::ofstream o(jsonPath); o << jsonReport(primary, &after, &stats, file); }
        if (!same) return 3;
        return 0;
    }
    if (!jsonPath.empty()) { std::ofstream o(jsonPath); o << jsonReport(primary, nullptr, nullptr, file); }
    return 0;
}
