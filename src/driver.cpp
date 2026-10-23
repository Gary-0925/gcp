#include "driver.hpp"
#include "ast_engine.hpp"
#include "chemistry.hpp"
#include "runtime.hpp"
#include "config.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace gcp {

namespace {

bool read_file_bytes(const std::string& path, std::string& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    out = ss.str();
    return true;
}

bool write_file_bytes(const std::string& path, const std::string& content) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(ofs);
}

bool is_c_source(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    return path.substr(dot) == ".c";
}

bool is_cpp_source(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot);
    return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".C" || ext == ".c++";
}

int run_subprocess(const std::vector<std::string>& args) {
    if (args.empty()) return 1;
#ifdef _WIN32
    std::vector<std::string> quoted;
    quoted.reserve(args.size());
    for (const auto& s : args) {
        if (s.find_first_of(" \t\"") != std::string::npos) {
            std::string q = "\"";
            for (char c : s) {
                if (c == '"') q += "\\\"";
                else q += c;
            }
            q += "\"";
            quoted.push_back(q);
        } else {
            quoted.push_back(s);
        }
    }
    std::vector<const char*> cargs;
    cargs.reserve(quoted.size() + 1);
    for (const auto& s : quoted) cargs.push_back(s.c_str());
    cargs.push_back(nullptr);
    int rc = _spawnvp(_P_WAIT, args[0].c_str(), const_cast<char* const*>(cargs.data()));
    if (rc < 0) {
        std::perror(("gcp: failed to execute backend compiler '" + args[0] + "'").c_str());
        return 127;
    }
    return rc;
#else
    std::vector<char*> cargs;
    cargs.reserve(args.size() + 1);
    for (const auto& s : args) {
        cargs.push_back(const_cast<char*>(s.c_str()));
    }
    cargs.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        std::perror("gcp: fork failed");
        return 1;
    }
    if (pid == 0) {
        execvp(cargs[0], cargs.data());
        std::perror(("gcp: execvp failed for " + args[0]).c_str());
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
#endif
}

bool is_cxx_driver_name(const std::string& invoked) {
    std::string stem = fs::path(invoked).stem().string();
    return (stem.find("--") != std::string::npos) || (stem.find("++") != std::string::npos);
}

void print_help(const char* prog, bool cpp_driver) {
    std::cout
        << "Usage: " << prog << " [chemical-flags] [gcc/g++ options] file...\n"
        << (cpp_driver
            ? "g-- (Green--) " + version + " - Atmospheric Anti-Optimizing C++ Compiler Driver\n\n"
            : "GCP (Greenhouse Code Producer) " + version + " - Atmospheric Anti-Optimizing C/C++ Compiler\n\n")
        << "Reactant & Gas Injection Options (-<SPECIES> or -<SPECIES>=<mol>):\n"
        << "  -C[=mol]           Solid Carbon soot: unbuffered byte-by-byte memcpy scalar copies\n"
        << "  -CO[=mol], -CO1    Carbon Monoxide: heap-boxed local ints (malloc / std::shared_ptr),\n"
        << "                     deep-cloned const& parameters, std::move deep copies,\n"
        << "                     vector push_back + shrink_to_fit reallocation, and stack spills\n"
        << "  -CO2[=mol]         Carbon Dioxide: stringly-typed snprintf/strtoll arithmetic (C),\n"
        << "                     OOP virtual IArithmeticStrategy + dynamic_cast + stringstream (C++),\n"
        << "                     linear-scan indexing, and O(N^2) Shlemiel char* string scanning\n"
        << "  -H2O[=mol]         Water Vapor: expand ++/--/+=, split switch jump tables into\n"
        << "                     non-else sequential if checks, and structural AST padding\n"
        << "  -H2[=mol]          Hydrogen Gas: clean fuel yielding H2O upon combustion with O2\n"
        << "  -CH4[=mol]         Methane: exception-driven control flow (try/throw/catch in C++)\n"
        << "                     and non-local setjmp/longjmp control flow (in C); burns in O2!\n"
        << "  -N2[=mol]          Nitrogen Gas: inert buffer gas; undergoes thermal fixation to NO/NO2\n"
        << "                     when combined with O2 and -farc / -flightning\n"
        << "  -NO2[=mol]         Nitrogen Dioxide: outlined cold branch misprediction wrappers\n\n"
        << "Oxygen & Environmental Catalyst Options:\n"
        << "  -O0                Inert no-op\n"
        << "  -O1                Inject 0.5 mol O2  (partial oxidation: 2 C + O2 -> 2 CO)\n"
        << "  -O2                Inject 1.0 mol O2  (full combustion: C -> CO2, CH4 -> CO2 + 2 H2O)\n"
        << "  -O3                Inject 1.0 mol O3  (ozone hyper-oxidation -> 1.5 mol O2)\n"
        << "  -Ofast             Inject 5.0 mol O2  (pressurized oxygen blast)\n"
        << "  -fheat             Thermal catalyst (enables C + CO2 -> 2 CO; reverses H2CO3 -> H2O + CO2)\n"
        << "  -farc, -flightning High-voltage arc catalyst (N2 + O2 -> 2 NO; 3 O2 -> 2 O3)\n"
        << "  -fsunlight         Photochemical photosynthesis (6 CO2 + 6 H2O -> C6H12O6 + 6 O2 -> -O3)\n\n"
        << "Driver Inspection & Control Options:\n"
        << "  --report           Simulate the reaction chamber and print stoichiometry without compiling\n"
        << "  --convert          Emit transformed *.carbon.c / *.carbon.cpp next to source files\n"
        << "  --retain           Retain generated carbonized source files after compilation\n"
        << "  -V, --verbose      Display balanced chemical equations, diagnostics, and AST pass stats\n"
        << "  --backend=<path>   Explicitly set underlying compiler binary (default: gcc or g++)\n"
        << "  -v, --version      Display compiler version information\n"
        << "  -h, --help         Display this help message\n";
}

} // namespace

int run_compiler_driver(int argc, char** argv) {
    std::string invoked = (argc > 0 && argv[0]) ? argv[0] : "gcp";
    bool default_cpp_mode = is_cxx_driver_name(invoked);

    ChamberConfig chamber_cfg;
    bool verbose = false;
    bool report_only = false;
    bool convert_only = false;
    bool retain_temps = false;
    bool compile_only = false;
    std::string explicit_backend;
    std::string output_path;
    std::string user_opt_flag;

    std::vector<std::string> passthrough_flags;
    std::vector<std::string> source_inputs;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help" || arg == "-help") {
            print_help(invoked.c_str(), default_cpp_mode);
            return 0;
        }
        if (arg == "-v" || arg == "--version") {
            if (default_cpp_mode) {
                std::cout << "g-- (Green--) " + version + "\n"
                          << "C++ Frontend of GCP (Greenhouse Code Producer)\n"
                          << "Anti-Optimizing C/C++ Compiler\n";
            } else {
                std::cout << "gcp (Greenhouse Code Producer) " + version + "\n"
                          << "Anti-Optimizing C/C++ Compiler\n";
            }
            return 0;
        }
        if (arg == "--report") {
            report_only = true;
            continue;
        }
        if (arg == "--convert" || arg == "--convent") {
            convert_only = true;
            continue;
        }
        if (arg == "--retain") {
            retain_temps = true;
            continue;
        }
        if (arg == "-V" || arg == "--verbose") {
            verbose = true;
            continue;
        }
        if (arg.rfind("--backend=", 0) == 0) {
            explicit_backend = arg.substr(10);
            continue;
        }
        if (arg == "--backend" && i + 1 < argc) {
            explicit_backend = argv[++i];
            continue;
        }

        if (parse_chemical_arg(arg, chamber_cfg)) {
            continue;
        }

        if (arg == "-O0" || arg == "-O" || arg == "-O1" ||
            arg == "-O2" || arg == "-O3" || arg == "-Os" || arg == "-Ofast") {
            user_opt_flag = arg;
            continue;
        }

        if (arg == "-c" || arg == "-S" || arg == "-E") {
            compile_only = true;
            passthrough_flags.push_back(arg);
            continue;
        }
        if (arg == "-o" && i + 1 < argc) {
            output_path = argv[++i];
            continue;
        }
        if (arg.rfind("-o", 0) == 0 && arg.size() > 2) {
            output_path = arg.substr(2);
            continue;
        }
        if ((arg == "-I" || arg == "-L" || arg == "-D" || arg == "-U" ||
             arg == "-include" || arg == "-isystem" || arg == "-x" ||
             arg == "-MF" || arg == "-MT" || arg == "-MQ") && i + 1 < argc) {
            passthrough_flags.push_back(arg);
            passthrough_flags.push_back(argv[++i]);
            continue;
        }

        if (!arg.empty() && arg[0] != '-' && (is_c_source(arg) || is_cpp_source(arg))) {
            source_inputs.push_back(arg);
            continue;
        }

        passthrough_flags.push_back(arg);
    }

    ReactionResult chem = run_reaction_chamber(chamber_cfg);

    if (report_only) {
        std::cout << format_reaction_diagnostics(chem, invoked);
        std::cout << format_reaction_header_comment(chem);
        return 0;
    }

    bool has_anti_opt = (chem.pass_c > 1e-6 || chem.pass_co > 1e-6 || chem.pass_co2 > 1e-6 ||
                         chem.pass_h2o > 1e-6 || chem.pass_ch4 > 1e-6 || chem.pass_no2 > 1e-6);

    if (verbose || has_anti_opt || chem.photosynthesis_miracle) {
        std::string diag_str = format_reaction_diagnostics(chem, invoked);
        if (!diag_str.empty()) {
            std::cerr << diag_str;
        }
    }

    if (verbose && source_inputs.empty() && passthrough_flags.empty()) {
        std::cerr << (default_cpp_mode
            ? "g-- (Green--) version " + version + "\n"
            : "GCP (Greenhouse Code Producer) version "+ version + "\n");
        return 0;
    }

    if (source_inputs.empty() && passthrough_flags.empty()) {
        std::cerr << invoked << ": fatal error: no input files\ncompilation terminated.\n";
        return 1;
    }

    bool any_cpp = default_cpp_mode;
    for (const auto& s : source_inputs) {
        if (is_cpp_source(s)) any_cpp = true;
    }
    std::string backend = explicit_backend.empty() ? (any_cpp ? "g++" : "gcc") : explicit_backend;

    std::string effective_opt = user_opt_flag;
    if (has_anti_opt) {
        effective_opt = "-O0";
    } else if (chem.photosynthesis_miracle) {
        effective_opt = "-O3";
    }

    std::vector<std::string> temp_files;
    std::vector<std::string> compiled_inputs;

    for (const auto& src_path : source_inputs) {
        bool file_cpp = is_cpp_source(src_path) || default_cpp_mode;
        std::string raw_code;
        if (!read_file_bytes(src_path, raw_code)) {
            std::cerr << invoked << ": error: " << src_path << ": No such file or directory\n";
            return 1;
        }

        if (!has_anti_opt && !convert_only && !retain_temps) {
            if (verbose) {
                std::cerr << "[GCP] Zero-emission pass-through for " << src_path
                          << " (backend optimization: "
                          << (effective_opt.empty() ? "default (-O0)" : effective_opt) << ")\n";
            }
            compiled_inputs.push_back(src_path);
            continue;
        }

        TransformOutput trans = carbonize_source(raw_code, chem, file_cpp);
        if (verbose) {
            std::cerr << "[GCP] Carbonized " << src_path << " (" << (file_cpp ? "C++" : "C") << ")"
                      << " | functions=" << trans.stats.functions_processed
                      << " h2o_bloat=" << trans.stats.hydrated_nodes
                      << " split_switches=" << trans.stats.shattered_switches
                      << " memcpy_copies=" << trans.stats.memcpy_assignments
                      << " heap_boxed_ints=" << trans.stats.heap_boxed_vars
                      << " cpp_sabotages=" << trans.stats.anti_move_or_vector_sabotages
                      << " stringly_ops=" << trans.stats.stringly_or_oop_ops
                      << " linear_scans=" << trans.stats.linear_scans
                      << " nonlocal_jumps=" << trans.stats.nonlocal_jumps
                      << " cold_branches=" << trans.stats.mispredicted_branches << "\n";
        }

        fs::path p(src_path);
        std::string ext = file_cpp ? ".carbon.cpp" : ".carbon.c";
        std::string out_src_path;
        if (convert_only || retain_temps) {
            out_src_path = (p.parent_path() / (p.stem().string() + ext)).string();
        } else {
            static unsigned counter = 0;
#ifdef _WIN32
            int pid_val = _getpid();
#else
            int pid_val = static_cast<int>(getpid());
#endif
            std::string tmp_name = "gcp_" + std::to_string(pid_val) + "_" +
                                   std::to_string(++counter) + "_" + p.stem().string() + ext;
            out_src_path = (fs::temp_directory_path() / tmp_name).string();
            temp_files.push_back(out_src_path);
        }

        if (!write_file_bytes(out_src_path, trans.code)) {
            std::cerr << invoked << ": error: failed to write intermediate source " << out_src_path << "\n";
            return 1;
        }
        compiled_inputs.push_back(out_src_path);
    }

    if (convert_only) {
        return 0;
    }

    std::vector<std::string> cmd;
    cmd.push_back(backend);
    if (!effective_opt.empty()) {
        cmd.push_back(effective_opt);
    }

    if (has_anti_opt) {
        cmd.push_back("-fno-inline");
        cmd.push_back("-fno-optimize-sibling-calls");
        cmd.push_back("-fno-tree-vectorize");
        if (chem.pass_c > 1e-6) {
            cmd.push_back("-fno-builtin");
        }
        cmd.push_back("-Wno-unused-value");
        cmd.push_back("-Wno-unused-variable");
        cmd.push_back("-Wno-unused-but-set-variable");
    }

    for (const auto& f : passthrough_flags) cmd.push_back(f);
    for (const auto& in_f : compiled_inputs) cmd.push_back(in_f);

    if (!output_path.empty()) {
        cmd.push_back("-o");
        cmd.push_back(output_path);
    } else if (compile_only && source_inputs.size() == 1 && compiled_inputs.size() == 1) {
        fs::path orig_p(source_inputs[0]);
        cmd.push_back("-o");
        cmd.push_back(orig_p.stem().string() + ".o");
    }

    if (verbose) {
        std::cerr << "[GCP Backend] ";
        for (const auto& s : cmd) std::cerr << s << ' ';
        std::cerr << "\n";
    }

    int rc = run_subprocess(cmd);

    if (!retain_temps) {
        for (const auto& tf : temp_files) {
            std::error_code ec;
            fs::remove(tf, ec);
        }
    }
    return rc;
}

} // namespace gcp
