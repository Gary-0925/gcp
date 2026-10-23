#ifndef __GCP_RUNTIME_HPP
#define __GCP_RUNTIME_HPP

#include "chemistry.hpp"
#include <string>

namespace gcp {

// Emit clean, self-contained C11 or C++17 atmospheric runtime definitions
// tailored to the exact chemical species produced in `res`.
std::string emit_atmospheric_runtime(const ReactionResult& res, bool is_cpp);

} // namespace gcp

#endif // __GCP_RUNTIME_HPP
