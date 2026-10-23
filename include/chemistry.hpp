#ifndef __GCP_CHEMISTRY_HPP
#define __GCP_CHEMISTRY_HPP

#include <map>
#include <string>
#include <vector>

namespace gcp {

struct AtomCount {
    double C = 0.0;
    double H = 0.0;
    double O = 0.0;
    double N = 0.0;
};

struct ReactionStep {
    int step_index = 0;
    std::string equation;
    std::string description;
    double extent_mol = 0.0;
    double delta_h_kj = 0.0;
    double temp_before_k = 298.0;
    double temp_after_k = 298.0;
};

struct ChamberConfig {
    std::map<std::string, double> initial_moles;
    bool external_heat = false;
    bool electric_arc = false;
    bool sunlight = false;
};

struct ReactionResult {
    bool chamber_activated = false;
    bool photosynthesis_miracle = false;
    double initial_temp_k = 298.0;
    double peak_temp_k = 298.0;
    double final_temp_k = 298.0;
    double total_enthalpy_kj = 0.0;

    std::map<std::string, double> initial_moles;
    std::map<std::string, double> final_moles;
    std::vector<ReactionStep> steps;
    std::vector<std::string> diagnostic_notes;

    AtomCount atoms_in;
    AtomCount atoms_out;
    bool mass_conserved = true;

    // Effective pass weights derived from final equilibrium composition
    double pass_h2o = 0.0;
    double pass_c = 0.0;
    double pass_co = 0.0;
    double pass_co2 = 0.0;
    double pass_ch4 = 0.0;
    double pass_no2 = 0.0;
    double pass_h2co3 = 0.0;
};

// Parse a single CLI flag. Returns true if consumed exclusively by GCP.
bool parse_chemical_arg(const std::string& arg, ChamberConfig& cfg);

// Simulate multi-step stoichiometric reactions in a closed chamber starting in vacuum.
ReactionResult run_reaction_chamber(const ChamberConfig& cfg);

// Format GCC-style diagnostic log for terminal output (pure 7-bit ASCII).
std::string format_reaction_diagnostics(const ReactionResult& res, const std::string& prog_name);

// Format clean ASCII C/C++ comment header for generated .carbon.c/.cpp files.
std::string format_reaction_header_comment(const ReactionResult& res);

} // namespace gcp

#endif // __GCP_CHEMISTRY_HPP
