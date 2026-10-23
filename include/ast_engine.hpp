#ifndef __GCP_AST_ENGINE_HPP
#define __GCP_AST_ENGINE_HPP

#include "chemistry.hpp"
#include <string>

namespace gcp {

struct CarbonStats {
    size_t functions_processed = 0;
    size_t hydrated_nodes = 0;
    size_t shattered_switches = 0;
    size_t memcpy_assignments = 0;
    size_t heap_boxed_vars = 0;
    size_t anti_move_or_vector_sabotages = 0;
    size_t stringly_or_oop_ops = 0;
    size_t linear_scans = 0;
    size_t nonlocal_jumps = 0;
    size_t mispredicted_branches = 0;
};

struct TransformOutput {
    std::string code;
    CarbonStats stats;
    bool had_parse_errors = false;
};

TransformOutput carbonize_source(const std::string& source,
                                 const ReactionResult& chem,
                                 bool is_cpp);

} // namespace gcp

#endif // __GCP_AST_ENGINE_HPP
