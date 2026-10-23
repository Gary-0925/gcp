#include "chemistry.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace gcp {

namespace {

constexpr double EPS = 1e-6;

double get_mol(const std::map<std::string, double>& m, const std::string& sp) {
    auto it = m.find(sp);
    if (it == m.end()) return 0.0;
    return (it->second > EPS) ? it->second : 0.0;
}

void add_mol(std::map<std::string, double>& m, const std::string& sp, double delta) {
    double next = m[sp] + delta;
    if (next <= EPS) {
        m.erase(sp);
    } else {
        m[sp] = next;
    }
}

AtomCount count_atoms(const std::map<std::string, double>& moles) {
    AtomCount a;
    for (const auto& kv : moles) {
        const std::string& sp = kv.first;
        double n = kv.second;
        if (n <= EPS) continue;
        if (sp == "C") {
            a.C += n;
        } else if (sp == "CO") {
            a.C += n;
            a.O += n;
        } else if (sp == "CO2") {
            a.C += n;
            a.O += 2.0 * n;
        } else if (sp == "H2") {
            a.H += 2.0 * n;
        } else if (sp == "H2O") {
            a.H += 2.0 * n;
            a.O += n;
        } else if (sp == "CH4") {
            a.C += n;
            a.H += 4.0 * n;
        } else if (sp == "O2") {
            a.O += 2.0 * n;
        } else if (sp == "O3") {
            a.O += 3.0 * n;
        } else if (sp == "N2") {
            a.N += 2.0 * n;
        } else if (sp == "NO") {
            a.N += n;
            a.O += n;
        } else if (sp == "NO2") {
            a.N += n;
            a.O += 2.0 * n;
        } else if (sp == "H2CO3") {
            a.H += 2.0 * n;
            a.C += n;
            a.O += 3.0 * n;
        } else if (sp == "C6H12O6") {
            a.C += 6.0 * n;
            a.H += 12.0 * n;
            a.O += 6.0 * n;
        }
    }
    return a;
}

std::string fmt_num(double v) {
    std::ostringstream oss;
    if (std::fabs(v - std::round(v)) < 1e-4) {
        oss << static_cast<long long>(std::llround(v));
    } else {
        oss << std::fixed << std::setprecision(2) << v;
        std::string s = oss.str();
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    }
    return oss.str();
}

std::string fmt_composition(const std::map<std::string, double>& m) {
    if (m.empty()) return "0 mol";
    std::ostringstream oss;
    bool first = true;
    for (const auto& kv : m) {
        if (kv.second <= EPS) continue;
        if (!first) oss << " + ";
        first = false;
        oss << fmt_num(kv.second) << " mol " << kv.first;
    }
    return first ? "0 mol" : oss.str();
}

std::string fmt_atoms(const AtomCount& a) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    auto append = [&](const char* sym, double val) {
        if (val <= EPS) return;
        if (!first) oss << ", ";
        first = false;
        oss << sym << "=" << fmt_num(val);
    };
    append("C", a.C);
    append("H", a.H);
    append("O", a.O);
    append("N", a.N);
    if (first) oss << "none";
    oss << "}";
    return oss.str();
}

} // namespace

bool parse_chemical_arg(const std::string& arg, ChamberConfig& cfg) {
    if (arg.empty() || arg[0] != '-') return false;

    // Environmental catalysts
    if (arg == "-fheat" || arg == "-fignite") {
        cfg.external_heat = true;
        return true;
    }
    if (arg == "-farc" || arg == "-flightning") {
        cfg.electric_arc = true;
        return true;
    }
    if (arg == "-fsunlight" || arg == "-fphotosynthesis") {
        cfg.sunlight = true;
        return true;
    }

    // Standard GCC -O* flags:
    // -O / -O1 / -O2 / -O3 supply oxygen if and only if a GCP flag activates the chamber.
    if (arg == "-O0") {
        return false;
    }
    if (arg == "-O" || arg == "-O1") {
        add_mol(cfg.initial_moles, "O2", 0.5);
        return false;
    }
    if (arg == "-O2") {
        add_mol(cfg.initial_moles, "O2", 1.0);
        return false;
    }
    if (arg == "-O3") {
        add_mol(cfg.initial_moles, "O3", 1.0);
        return false;
    }
    if (arg == "-Ofast") {
        add_mol(cfg.initial_moles, "O2", 5.0);
        cfg.external_heat = true;
        return false;
    }

    // Never match lowercase `-c` (GCC compile-only flag)
    if (arg == "-c") return false;

    std::string body = arg.substr(1);
    double coeff = 1.0;
    bool explicit_mol = false;

    auto eq_pos = body.find('=');
    if (eq_pos != std::string::npos) {
        std::string rhs = body.substr(eq_pos + 1);
        if (rhs.empty()) return false;
        char* endp = nullptr;
        coeff = std::strtod(rhs.c_str(), &endp);
        if (!endp || *endp != '\0' || coeff < 0.0) return false;
        body = body.substr(0, eq_pos);
        explicit_mol = true;
    } else {
        size_t idx = 0;
        while (idx < body.size() && ((body[idx] >= '0' && body[idx] <= '9') || body[idx] == '.')) {
            ++idx;
        }
        if (idx > 0 && idx < body.size()) {
            coeff = std::atof(body.substr(0, idx).c_str());
            if (coeff <= 0.0) return false;
            body = body.substr(idx);
            explicit_mol = true;
        }
    }

    if (body == "C") {
        add_mol(cfg.initial_moles, "C", coeff);
        return true;
    }
    if (body == "CO" || body == "CO1") {
        add_mol(cfg.initial_moles, "CO", coeff);
        return true;
    }
    if (body == "CO2") {
        add_mol(cfg.initial_moles, "CO2", coeff);
        return true;
    }
    if (body == "H2O") {
        add_mol(cfg.initial_moles, "H2O", coeff);
        return true;
    }
    if (body == "H2") {
        add_mol(cfg.initial_moles, "H2", coeff);
        return true;
    }
    if (body == "CH4") {
        add_mol(cfg.initial_moles, "CH4", coeff);
        return true;
    }
    if (body == "N2") {
        add_mol(cfg.initial_moles, "N2", coeff);
        return true;
    }
    if (body == "NO") {
        add_mol(cfg.initial_moles, "NO", coeff);
        return true;
    }
    if (body == "NO2") {
        add_mol(cfg.initial_moles, "NO2", coeff);
        return true;
    }
    if (body == "H2CO3") {
        add_mol(cfg.initial_moles, "H2CO3", coeff);
        return true;
    }
    if (explicit_mol && (body == "O2" || body == "O3")) {
        add_mol(cfg.initial_moles, body, coeff);
        return true;
    }

    return false;
}

ReactionResult run_reaction_chamber(const ChamberConfig& cfg) {
    ReactionResult res;
    res.chamber_activated = true;
    res.initial_moles = cfg.initial_moles;
    res.atoms_in = count_atoms(res.initial_moles);

    std::map<std::string, double> m = cfg.initial_moles;
    double temp_k = 298.0; // Standard ambient temperature (298 K)
    if (cfg.external_heat) temp_k = 1150.0;
    if (cfg.electric_arc) temp_k = std::max(temp_k, 2300.0);
    res.initial_temp_k = temp_k;
    res.peak_temp_k = temp_k;

    int step_id = 0;
    auto record = [&](const std::string& eq,
                      const std::string& desc,
                      double extent,
                      double dh_per_mol,
                      double dt_per_mol) {
        ReactionStep st;
        st.step_index = ++step_id;
        st.equation = eq;
        st.description = desc;
        st.extent_mol = extent;
        st.delta_h_kj = dh_per_mol * extent;
        st.temp_before_k = temp_k;
        temp_k = std::max(290.0, temp_k + dt_per_mol * extent);
        st.temp_after_k = temp_k;
        res.peak_temp_k = std::max(res.peak_temp_k, temp_k);
        res.total_enthalpy_kj += st.delta_h_kj;
        res.steps.push_back(st);
    };

    bool syngas_formed = false;

    for (int iter = 0; iter < 32; ++iter) {
        // 1. Ozone decomposition: 2 O3 -> 3 O2
        double n_o3 = get_mol(m, "O3");
        if (n_o3 > EPS) {
            double x = n_o3 / 2.0;
            add_mol(m, "O3", -2.0 * x);
            add_mol(m, "O2", +3.0 * x);
            record("2 O3 -> 3 O2", "spontaneous ozone decomposition", x, -285.0, +420.0);
            continue;
        }

        // 2. Photosynthesis (when -fsunlight is active): 6 CO2 + 6 H2O -> C6H12O6 + 6 O2
        if (cfg.sunlight) {
            double n_co2 = get_mol(m, "CO2");
            double n_h2o = get_mol(m, "H2O");
            if (n_co2 > EPS && n_h2o > EPS) {
                double x = std::min(n_co2, n_h2o) / 6.0;
                add_mol(m, "CO2", -6.0 * x);
                add_mol(m, "H2O", -6.0 * x);
                add_mol(m, "C6H12O6", +1.0 * x);
                add_mol(m, "O2", +6.0 * x);
                record("6 CO2 + 6 H2O -> C6H12O6 + 6 O2",
                       "photochemical carbon fixation (photosynthesis)",
                       x, +2803.0, -300.0);
                continue;
            }
        }

        // 3. Methane (CH4) stoichiometric oxidation
        double n_ch4 = get_mol(m, "CH4");
        double n_o2 = get_mol(m, "O2");
        if (n_ch4 > EPS && n_o2 > EPS && !cfg.sunlight) {
            double ratio = n_o2 / n_ch4;
            if (ratio >= 2.0 - 1e-4) {
                double x = n_ch4;
                add_mol(m, "CH4", -1.0 * x);
                add_mol(m, "O2", -2.0 * x);
                add_mol(m, "CO2", +1.0 * x);
                add_mol(m, "H2O", +2.0 * x);
                record("CH4 + 2 O2 -> CO2 + 2 H2O",
                       "complete stoichiometric methane combustion",
                       x, -890.0, +1080.0);
                continue;
            } else if (ratio >= 1.5 - 1e-4) {
                double x = std::min(n_ch4 / 2.0, n_o2 / 3.0);
                add_mol(m, "CH4", -2.0 * x);
                add_mol(m, "O2", -3.0 * x);
                add_mol(m, "CO", +2.0 * x);
                add_mol(m, "H2O", +4.0 * x);
                record("2 CH4 + 3 O2 -> 2 CO + 4 H2O",
                       "oxygen-limited incomplete methane combustion",
                       x, -1219.0, +1150.0);
                continue;
            } else {
                double x = std::min(n_ch4, n_o2);
                add_mol(m, "CH4", -1.0 * x);
                add_mol(m, "O2", -1.0 * x);
                add_mol(m, "C", +1.0 * x);
                add_mol(m, "H2O", +2.0 * x);
                record("CH4 + O2 -> C + 2 H2O",
                       "fuel-rich methane pyrolysis with solid carbon deposition",
                       x, -497.0, +620.0);
                continue;
            }
        }

        // 4. Hydrogen oxidation: 2 H2 + O2 -> 2 H2O
        double n_h2 = get_mol(m, "H2");
        n_o2 = get_mol(m, "O2");
        if (n_h2 > EPS && n_o2 > EPS && !cfg.sunlight) {
            double x = std::min(n_h2 / 2.0, n_o2);
            add_mol(m, "H2", -2.0 * x);
            add_mol(m, "O2", -1.0 * x);
            add_mol(m, "H2O", +2.0 * x);
            record("2 H2 + O2 -> 2 H2O", "exothermic hydrogen-oxygen combustion", x, -572.0, +680.0);
            continue;
        }

        // 5. Solid Carbon oxidation: C + O2 -> CO2 or 2 C + O2 -> 2 CO
        double n_c = get_mol(m, "C");
        n_o2 = get_mol(m, "O2");
        if (n_c > EPS && n_o2 > EPS && !cfg.sunlight) {
            if (n_o2 >= n_c - 1e-4) {
                double x = n_c;
                add_mol(m, "C", -1.0 * x);
                add_mol(m, "O2", -1.0 * x);
                add_mol(m, "CO2", +1.0 * x);
                record("C + O2 -> CO2", "complete carbon oxidation", x, -393.5, +720.0);
                continue;
            } else {
                double x = std::min(n_c / 2.0, n_o2);
                add_mol(m, "C", -2.0 * x);
                add_mol(m, "O2", -1.0 * x);
                add_mol(m, "CO", +2.0 * x);
                record("2 C + O2 -> 2 CO", "oxygen-starved incomplete carbon oxidation", x, -221.0, +460.0);
                continue;
            }
        }

        // 6. Carbon Monoxide oxidation: 2 CO + O2 -> 2 CO2
        double n_co = get_mol(m, "CO");
        n_o2 = get_mol(m, "O2");
        if (n_co > EPS && n_o2 > EPS && !cfg.sunlight) {
            double x = std::min(n_co / 2.0, n_o2);
            add_mol(m, "CO", -2.0 * x);
            add_mol(m, "O2", -1.0 * x);
            add_mol(m, "CO2", +2.0 * x);
            record("2 CO + O2 -> 2 CO2", "carbon monoxide secondary oxidation", x, -566.0, +640.0);
            continue;
        }

        // 7. Nitric Oxide oxidation: 2 NO + O2 -> 2 NO2
        double n_no = get_mol(m, "NO");
        n_o2 = get_mol(m, "O2");
        if (n_no > EPS && n_o2 > EPS && !cfg.sunlight) {
            double x = std::min(n_no / 2.0, n_o2);
            add_mol(m, "NO", -2.0 * x);
            add_mol(m, "O2", -1.0 * x);
            add_mol(m, "NO2", +2.0 * x);
            record("2 NO + O2 -> 2 NO2", "spontaneous nitric oxide oxidation", x, -114.0, +120.0);
            continue;
        }

        // 8. Thermal / Electric-Arc Nitrogen Fixation (T >= 2000 K): N2 + O2 -> 2 NO
        double n_n2 = get_mol(m, "N2");
        n_o2 = get_mol(m, "O2");
        if (n_n2 > EPS && n_o2 > EPS && (temp_k >= 2000.0 || cfg.electric_arc) && !cfg.sunlight) {
            double x = std::min(n_n2, n_o2);
            add_mol(m, "N2", -1.0 * x);
            add_mol(m, "O2", -1.0 * x);
            add_mol(m, "NO", +2.0 * x);
            record("N2 + O2 -> 2 NO", "high-temperature thermal nitrogen fixation (Zeldovich mechanism)", x, +180.0, -90.0);
            continue;
        }

        // 9. High-Temperature Steam-Carbon Gasification (T >= 1073 K): C + H2O -> CO + H2
        n_c = get_mol(m, "C");
        double n_h2o = get_mol(m, "H2O");
        if (n_c > EPS && n_h2o > EPS && temp_k >= 1073.0) {
            double x = std::min(n_c, n_h2o);
            add_mol(m, "C", -1.0 * x);
            add_mol(m, "H2O", -1.0 * x);
            add_mol(m, "CO", +1.0 * x);
            add_mol(m, "H2", +1.0 * x);
            syngas_formed = true;
            record("C + H2O -> CO + H2", "endothermic steam-carbon syngas reaction", x, +131.0, -160.0);
            continue;
        }

        // 10. High-Temperature Boudouard Reaction (T >= 973 K): C + CO2 -> 2 CO
        n_c = get_mol(m, "C");
        double n_co2 = get_mol(m, "CO2");
        if (n_c > EPS && n_co2 > EPS && temp_k >= 973.0) {
            double x = std::min(n_c, n_co2);
            add_mol(m, "C", -1.0 * x);
            add_mol(m, "CO2", -1.0 * x);
            add_mol(m, "CO", +2.0 * x);
            syngas_formed = true;
            record("C + CO2 -> 2 CO", "endothermic Boudouard carbon-dioxide reduction", x, +172.0, -180.0);
            continue;
        }

        // 11. Water-Gas Shift Reaction (623 K <= T < 973 K): CO + H2O -> CO2 + H2
        n_co = get_mol(m, "CO");
        n_h2o = get_mol(m, "H2O");
        if (n_co > EPS && n_h2o > EPS && temp_k >= 623.0 && temp_k < 973.0 && !syngas_formed) {
            double x = std::min(n_co, n_h2o);
            add_mol(m, "CO", -1.0 * x);
            add_mol(m, "H2O", -1.0 * x);
            add_mol(m, "CO2", +1.0 * x);
            add_mol(m, "H2", +1.0 * x);
            record("CO + H2O -> CO2 + H2", "water-gas shift reaction", x, -41.0, +85.0);
            continue;
        }

        // 12. Carbonic Acid Equilibrium: CO2 + H2O <-> H2CO3
        n_co2 = get_mol(m, "CO2");
        n_h2o = get_mol(m, "H2O");
        if (n_co2 > EPS && n_h2o > EPS && !cfg.sunlight) {
            double x = std::min(n_co2, n_h2o);
            add_mol(m, "CO2", -1.0 * x);
            add_mol(m, "H2O", -1.0 * x);
            add_mol(m, "H2CO3", +1.0 * x);
            record("CO2 + H2O <-> H2CO3", "aqueous carbonic acid hydration equilibrium", x, -20.0, +15.0);
            continue;
        }

        break;
    }

    if (get_mol(m, "N2") > EPS && get_mol(m, "O2") > EPS && temp_k < 2000.0 && !cfg.electric_arc) {
        std::ostringstream oss;
        oss << "N2 remained inert in O2 at " << fmt_num(temp_k)
            << " K (requires T >= 2000 K from combustion or '-farc' for N2 + O2 -> 2 NO)";
        res.diagnostic_notes.push_back(oss.str());
    }
    if (get_mol(m, "C") > EPS && get_mol(m, "CO2") > EPS && temp_k < 973.0) {
        std::ostringstream oss;
        oss << "C and CO2 did not react at " << fmt_num(temp_k)
            << " K (requires T >= 973 K or '-fheat' for Boudouard reaction C + CO2 -> 2 CO)";
        res.diagnostic_notes.push_back(oss.str());
    }
    if (get_mol(m, "C") > EPS && get_mol(m, "H2O") > EPS && temp_k < 1073.0) {
        std::ostringstream oss;
        oss << "C and H2O did not react at " << fmt_num(temp_k)
            << " K (requires T >= 1073 K or '-fheat' for steam gasification C + H2O -> CO + H2)";
        res.diagnostic_notes.push_back(oss.str());
    }

    res.final_temp_k = temp_k;
    res.final_moles = m;
    res.atoms_out = count_atoms(res.final_moles);
    res.mass_conserved =
        std::fabs(res.atoms_in.C - res.atoms_out.C) < 1e-3 &&
        std::fabs(res.atoms_in.H - res.atoms_out.H) < 1e-3 &&
        std::fabs(res.atoms_in.O - res.atoms_out.O) < 1e-3 &&
        std::fabs(res.atoms_in.N - res.atoms_out.N) < 1e-3;

    // Map final species to compiler de-optimization passes
    res.pass_h2o   = get_mol(m, "H2O") + get_mol(m, "H2CO3");
    res.pass_c     = get_mol(m, "C");
    res.pass_co    = get_mol(m, "CO") + 0.5 * get_mol(m, "NO") + 0.25 * get_mol(m, "H2");
    res.pass_co2   = get_mol(m, "CO2") + get_mol(m, "H2CO3");
    res.pass_ch4   = get_mol(m, "CH4");
    res.pass_no2   = get_mol(m, "NO2");
    res.pass_h2co3 = get_mol(m, "H2CO3");

    double total_deopt = res.pass_h2o + res.pass_c + res.pass_co +
                         res.pass_co2 + res.pass_ch4 + res.pass_no2;
    if (get_mol(m, "C6H12O6") > EPS && total_deopt <= EPS) {
        res.photosynthesis_miracle = true;
    }

    return res;
}

std::string format_reaction_diagnostics(const ReactionResult& res, const std::string& prog_name) {
    std::ostringstream oss;
    oss << prog_name << ": note: atmospheric reaction chamber initialized (T = "
        << fmt_num(res.initial_temp_k) << " K, baseline: vacuum)\n";
    oss << prog_name << ": note: initial reactants: " << fmt_composition(res.initial_moles) << "\n";

    if (res.steps.empty()) {
        oss << prog_name << ": note: no chemical reactions occurred at "
            << fmt_num(res.initial_temp_k) << " K\n";
    } else {
        for (const auto& st : res.steps) {
            oss << prog_name << ": note: reaction [" << st.step_index << "]: "
                << st.equation << "  (dH = " << (st.delta_h_kj >= 0.0 ? "+" : "")
                << fmt_num(st.delta_h_kj) << " kJ, T: " << fmt_num(st.temp_before_k)
                << " K -> " << fmt_num(st.temp_after_k) << " K; " << st.description << ")\n";
        }
    }

    for (const auto& n : res.diagnostic_notes) {
        oss << prog_name << ": note: " << n << "\n";
    }

    oss << prog_name << ": note: equilibrium products: " << fmt_composition(res.final_moles)
        << "  [conservation check: in " << fmt_atoms(res.atoms_in)
        << " == out " << fmt_atoms(res.atoms_out)
        << (res.mass_conserved ? ", verified]" : ", FAILED]") << "\n";

    if (res.photosynthesis_miracle) {
        oss << prog_name << ": note: photosynthesis cleared all greenhouse gases; "
            << "forwarding directly to backend with '-O3'\n";
    } else {
        std::vector<std::string> passes;
        if (res.pass_h2o > EPS) passes.push_back("-H2O (hydration bloat)");
        if (res.pass_c > EPS)   passes.push_back("-C (unbuffered memcpy)");
        if (res.pass_co > EPS)  passes.push_back("-CO (heap-boxed locals / anti-move)");
        if (res.pass_co2 > EPS) passes.push_back("-CO2 (stringly-typed / virtual RTTI math + linear scan)");
        if (res.pass_ch4 > EPS) passes.push_back("-CH4 (throw-catch / setjmp-longjmp control flow)");
        if (res.pass_no2 > EPS) passes.push_back("-NO2 (outlined cold mispredicted branches)");

        if (passes.empty()) {
            oss << prog_name << ": note: inert atmosphere only (e.g. pure N2); compiling with '-O0'\n";
        } else {
            oss << prog_name << ": warning: active de-optimization passes: ";
            for (size_t i = 0; i < passes.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << passes[i];
            }
            oss << " [-Wgreenhouse-effect]\n";
        }
    }
    return oss.str();
}

std::string format_reaction_header_comment(const ReactionResult& res) {
    std::ostringstream oss;
    oss << "/*\n"
        << " * Generated by GCP (Greenhouse Code Producer) / g-- (Green--)\n"
        << " * Reaction Chamber Summary:\n"
        << " *   Reactants   : " << fmt_composition(res.initial_moles) << "\n";
    for (const auto& st : res.steps) {
        oss << " *   Reaction #" << st.step_index << " : " << st.equation
            << " (dH = " << (st.delta_h_kj >= 0.0 ? "+" : "") << fmt_num(st.delta_h_kj)
            << " kJ, T -> " << fmt_num(st.temp_after_k) << " K)\n";
    }
    oss << " *   Equilibrium : " << fmt_composition(res.final_moles) << "\n"
        << " *   Mass Check  : " << fmt_atoms(res.atoms_in) << " -> " << fmt_atoms(res.atoms_out) << "\n"
        << " */\n\n";
    return oss.str();
}

} // namespace gcp
