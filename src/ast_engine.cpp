#include "ast_engine.hpp"
#include "runtime.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "tree_sitter/api.h"

extern "C" {
const TSLanguage* tree_sitter_c(void);
const TSLanguage* tree_sitter_cpp(void);
}

namespace gcp {

namespace {

constexpr double EPS = 1e-6;

std::string node_text(TSNode n, const std::string& src) {
    uint32_t s = ts_node_start_byte(n);
    uint32_t e = ts_node_end_byte(n);
    if (e <= s || e > src.size()) return "";
    return src.substr(s, e - s);
}

bool is_all_caps_identifier(const std::string& s) {
    if (s.size() < 2) return false;
    bool has_upper = false;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') {
            has_upper = true;
        } else if (!((c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return has_upper;
}

bool contains_word(const std::string& text, const char* word) {
    size_t wlen = std::strlen(word);
    size_t pos = 0;
    while ((pos = text.find(word, pos)) != std::string::npos) {
        bool left_ok = (pos == 0) ||
                       !(std::isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '_');
        bool right_ok = (pos + wlen >= text.size()) ||
                        !(std::isalnum(static_cast<unsigned char>(text[pos + wlen])) || text[pos + wlen] == '_');
        if (left_ok && right_ok) return true;
        pos += wlen;
    }
    return false;
}

bool is_simple_nonzero_int_literal(const std::string& raw) {
    if (raw.empty() || raw[0] < '0' || raw[0] > '9') return false;
    bool is_hex = (raw.size() > 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X'));
    for (char c : raw) {
        if (c == '\'' || c == '.' || c == 'p' || c == 'P') return false;
        if (!is_hex && (c == 'e' || c == 'E')) return false;
    }
    size_t suf = raw.size();
    while (suf > 0 && (raw[suf - 1] == 'u' || raw[suf - 1] == 'U' || raw[suf - 1] == 'l' || raw[suf - 1] == 'L')) {
        --suf;
    }
    std::string digits = raw.substr(0, suf);
    if (digits.empty()) return false;
    char* endp = nullptr;
    unsigned long long val = std::strtoull(digits.c_str(), &endp, 0);
    return (endp && *endp == '\0' && val > 0ULL);
}

// Check whether a declaration is a plain single `int var = expr;` candidate for heap-boxing.
bool extract_boxable_int_decl(TSNode decl, const std::string& src, std::string& out_name, TSNode& out_val) {
    if (std::strcmp(ts_node_type(decl), "declaration") != 0) return false;
    std::string full = node_text(decl, src);
    if (contains_word(full, "static") || contains_word(full, "const") ||
        contains_word(full, "constexpr") || contains_word(full, "extern") ||
        contains_word(full, "typedef") || contains_word(full, "unsigned") ||
        contains_word(full, "long") || contains_word(full, "short")) {
        return false;
    }
    TSNode type_node = ts_node_child_by_field_name(decl, "type", 4);
    if (ts_node_is_null(type_node) || std::strcmp(ts_node_type(type_node), "primitive_type") != 0) {
        return false;
    }
    if (node_text(type_node, src) != "int") return false;

    // Count declarator fields: must be strictly 1 init_declarator `ident = expr`
    int decl_count = 0;
    TSNode init_decl = TSNode{};
    uint32_t cnt = ts_node_child_count(decl);
    for (uint32_t i = 0; i < cnt; ++i) {
        const char* fn = ts_node_field_name_for_child(decl, i);
        if (fn && std::strcmp(fn, "declarator") == 0) {
            ++decl_count;
            init_decl = ts_node_child(decl, i);
        }
    }
    if (decl_count != 1 || std::strcmp(ts_node_type(init_decl), "init_declarator") != 0) {
        return false;
    }
    TSNode var_node = ts_node_child_by_field_name(init_decl, "declarator", 10);
    TSNode val_node = ts_node_child_by_field_name(init_decl, "value", 5);
    if (ts_node_is_null(var_node) || ts_node_is_null(val_node)) return false;
    if (std::strcmp(ts_node_type(var_node), "identifier") != 0) return false;
    if (std::strcmp(ts_node_type(val_node), "initializer_list") == 0) return false;

    out_name = node_text(var_node, src);
    out_val = val_node;
    return !out_name.empty();
}

struct BoxedScope {
    std::string name;
    uint32_t decl_start = 0;
    uint32_t use_from = 0;
    uint32_t use_until = 0;
};

struct FunctionScanInfo {
    std::vector<BoxedScope> boxed_vars;
    std::vector<std::string> const_ref_params;
};

void collect_all_identifiers(TSNode n, const std::string& src, std::set<std::string>& out) {
    if (std::strcmp(ts_node_type(n), "identifier") == 0) {
        out.insert(node_text(n, src));
    }
    uint32_t cnt = ts_node_child_count(n);
    for (uint32_t i = 0; i < cnt; ++i) {
        collect_all_identifiers(ts_node_child(n, i), src, out);
    }
}

void scan_fn_tree(TSNode n,
                  const std::string& src,
                  uint32_t current_scope_end,
                  bool in_params,
                  std::set<std::string>& forbidden_names,
                  std::vector<BoxedScope>& candidates,
                  std::map<std::string, int>& total_decl_counts) {
    const char* tp = ts_node_type(n);

    if (std::strcmp(tp, "parameter_list") == 0) {
        in_params = true;
    }
    if (std::strcmp(tp, "lambda_capture_specifier") == 0) {
        collect_all_identifiers(n, src, forbidden_names);
        return;
    }
    if (std::strcmp(tp, "call_expression") == 0) {
        TSNode fn = ts_node_child_by_field_name(n, "function", 8);
        if (!ts_node_is_null(fn) && is_all_caps_identifier(node_text(fn, src))) {
            collect_all_identifiers(n, src, forbidden_names);
            return;
        }
    }
    if (in_params && std::strcmp(tp, "identifier") == 0) {
        forbidden_names.insert(node_text(n, src));
    }
    if (std::strcmp(tp, "field_identifier") == 0 || std::strcmp(tp, "type_identifier") == 0) {
        forbidden_names.insert(node_text(n, src));
    }
    if (std::strcmp(tp, "qualified_identifier") == 0 || std::strcmp(tp, "scoped_identifier") == 0) {
        uint32_t cnt = ts_node_child_count(n);
        for (uint32_t i = 0; i < cnt; ++i) {
            forbidden_names.insert(node_text(ts_node_child(n, i), src));
        }
        return;
    }
    if (std::strcmp(tp, "compound_statement") == 0 || std::strcmp(tp, "for_statement") == 0) {
        current_scope_end = ts_node_end_byte(n);
    }

    if (!in_params && std::strcmp(tp, "declaration") == 0) {
        std::string vname;
        TSNode vval;
        if (extract_boxable_int_decl(n, src, vname, vval)) {
            // Ensure `vname` is not referenced inside its own initializer `int x = x;`
            std::set<std::string> init_ids;
            collect_all_identifiers(vval, src, init_ids);
            if (init_ids.count(vname)) {
                forbidden_names.insert(vname);
            } else {
                BoxedScope bs;
                bs.name = vname;
                bs.decl_start = ts_node_start_byte(n);
                bs.use_from = ts_node_end_byte(n);
                bs.use_until = current_scope_end;
                candidates.push_back(bs);
                total_decl_counts[vname]++;
            }
        } else {
            // Any declarator in a non-boxed declaration forbids colliding names in the function
            uint32_t cnt = ts_node_child_count(n);
            for (uint32_t i = 0; i < cnt; ++i) {
                const char* fn = ts_node_field_name_for_child(n, i);
                if (fn && std::strcmp(fn, "declarator") == 0) {
                    collect_all_identifiers(ts_node_child(n, i), src, forbidden_names);
                }
            }
        }
    }

    uint32_t cnt = ts_node_child_count(n);
    for (uint32_t i = 0; i < cnt; ++i) {
        scan_fn_tree(ts_node_child(n, i), src, current_scope_end, in_params,
                     forbidden_names, candidates, total_decl_counts);
    }
}

void extract_cpp_ref_params(TSNode fn_node, const std::string& src, std::vector<std::string>& out) {
    TSNode decl = ts_node_child_by_field_name(fn_node, "declarator", 10);
    while (!ts_node_is_null(decl) && std::strcmp(ts_node_type(decl), "function_declarator") != 0) {
        decl = ts_node_child_by_field_name(decl, "declarator", 10);
    }
    if (ts_node_is_null(decl)) return;
    TSNode params = ts_node_child_by_field_name(decl, "parameters", 10);
    if (ts_node_is_null(params)) return;

    uint32_t cnt = ts_node_child_count(params);
    for (uint32_t i = 0; i < cnt; ++i) {
        TSNode p = ts_node_child(params, i);
        if (std::strcmp(ts_node_type(p), "parameter_declaration") != 0) continue;
        std::string ptxt = node_text(p, src);
        // Only clone `const T& name` parameters (guaranteed copy-constructible value references in STL!)
        if (!contains_word(ptxt, "const") || ptxt.find('&') == std::string::npos || ptxt.find("&&") != std::string::npos) {
            continue;
        }
        TSNode pd = ts_node_child_by_field_name(p, "declarator", 10);
        if (!ts_node_is_null(pd)) {
            std::set<std::string> pids;
            collect_all_identifiers(pd, src, pids);
            if (pids.size() == 1) {
                out.push_back(*pids.begin());
            }
        }
    }
}

FunctionScanInfo analyze_function(TSNode fn_node, const std::string& src, bool want_boxing, bool is_cpp) {
    FunctionScanInfo info;
    if (is_cpp) {
        extract_cpp_ref_params(fn_node, src, info.const_ref_params);
    }
    if (!want_boxing) return info;

    std::set<std::string> forbidden;
    std::vector<BoxedScope> candidates;
    std::map<std::string, int> counts;
    scan_fn_tree(fn_node, src, ts_node_end_byte(fn_node), false, forbidden, candidates, counts);

    // Reject overlapping scopes for the same name
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        if (forbidden.count(c.name)) continue;
        bool overlap = false;
        for (size_t j = 0; j < candidates.size(); ++j) {
            if (i == j || candidates[j].name != c.name) continue;
            if (!(c.use_until <= candidates[j].decl_start || candidates[j].use_until <= c.decl_start)) {
                overlap = true;
                break;
            }
        }
        if (!overlap) {
            info.boxed_vars.push_back(c);
        }
    }
    return info;
}

class AstCarbonizer {
public:
    AstCarbonizer(const std::string& src, const ReactionResult& chem, bool is_cpp)
        : src_(src), chem_(chem), is_cpp_(is_cpp) {}

    struct Ctx {
        bool in_runtime_fn = false;
        bool in_constexpr = false;
        int hydration_depth = 0;
        const FunctionScanInfo* fn_info = nullptr;
    };

    CarbonStats stats_;

    std::string emit_node(TSNode n, Ctx ctx) {
        if (ts_node_is_null(n)) return "";
        const char* tp = ts_node_type(n);

        // 1. Preprocessor safety
        if (std::strcmp(tp, "preproc_def") == 0 ||
            std::strcmp(tp, "preproc_function_def") == 0 ||
            std::strcmp(tp, "preproc_params") == 0 ||
            std::strcmp(tp, "preproc_include") == 0 ||
            std::strcmp(tp, "preproc_call") == 0) {
            return node_text(n, src_);
        }
        if (std::strcmp(tp, "preproc_if") == 0 || std::strcmp(tp, "preproc_elif") == 0) {
            return emit_with_constexpr_field(n, ctx, "condition");
        }

        // 2. Function definition entry
        if (std::strcmp(tp, "function_definition") == 0) {
            TSNode body = ts_node_child_by_field_name(n, "body", 4);
            if (!ts_node_is_null(body)) {
                uint32_t s = ts_node_start_byte(n);
                uint32_t bs = ts_node_start_byte(body);
                std::string header = src_.substr(s, bs - s);
                if (contains_word(header, "constexpr") || contains_word(header, "consteval")) {
                    return node_text(n, src_);
                }

                FunctionScanInfo scan = analyze_function(n, src_, chem_.pass_co > EPS, is_cpp_);
                stats_.heap_boxed_vars += scan.boxed_vars.size();
                stats_.anti_move_or_vector_sabotages += scan.const_ref_params.size();

                Ctx body_ctx = ctx;
                body_ctx.in_runtime_fn = true;
                body_ctx.in_constexpr = false;
                body_ctx.fn_info = &scan;

                stats_.functions_processed++;
                return header + emit_compound_statement(body, body_ctx, /*is_fn_root=*/true);
            }
        }

        if (!ctx.in_runtime_fn) {
            return emit_children_verbatim(n, ctx);
        }

        // 3. Compile-time constant / type context guards
        if (std::strcmp(tp, "template_argument_list") == 0 ||
            std::strcmp(tp, "enum_specifier") == 0 ||
            std::strcmp(tp, "bitfield_clause") == 0 ||
            std::strcmp(tp, "sizeof_expression") == 0 ||
            std::strcmp(tp, "alignof_expression") == 0 ||
            std::strcmp(tp, "decltype") == 0 ||
            std::strcmp(tp, "type_descriptor") == 0 ||
            std::strcmp(tp, "attribute_specifier") == 0 ||
            std::strcmp(tp, "attribute_declaration") == 0 ||
            std::strcmp(tp, "static_assert_declaration") == 0 ||
            std::strcmp(tp, "gnu_asm_expression") == 0) {
            return node_text(n, src_);
        }
        if (std::strcmp(tp, "case_statement") == 0) {
            return emit_with_constexpr_field(n, ctx, "value");
        }
        if (std::strcmp(tp, "array_declarator") == 0) {
            return emit_with_constexpr_field(n, ctx, "size");
        }
        if (ctx.in_constexpr) {
            return node_text(n, src_);
        }

        // 4. Declarations: skip static/constexpr, or rewrite `int x = v;` into heap-boxed allocation under -CO!
        if (std::strcmp(tp, "declaration") == 0) {
            std::string decl_txt = node_text(n, src_);
            if (contains_word(decl_txt, "constexpr") || contains_word(decl_txt, "static")) {
                return decl_txt;
            }
            if (chem_.pass_co > EPS && ctx.fn_info != nullptr) {
                uint32_t ds = ts_node_start_byte(n);
                for (const auto& bv : ctx.fn_info->boxed_vars) {
                    if (bv.decl_start == ds) {
                        std::string vname;
                        TSNode vval;
                        if (extract_boxable_int_decl(n, src_, vname, vval)) {
                            std::string init_expr = emit_node(vval, ctx);
                            if (is_cpp_) {
                                return "std::shared_ptr<int> " + vname + " = std::make_shared<int>(" + init_expr + ");";
                            }
                            return "int *" + vname + " __attribute__((cleanup(gcp_free_int))) = gcp_box_int(" + init_expr + ");";
                        }
                    }
                }
            }
        }

        // 5. Call expressions: guard ALL_CAPS C constant macros, plus C++ anti-move / vector sabotage under -CO!
        if (std::strcmp(tp, "call_expression") == 0) {
            TSNode fn = ts_node_child_by_field_name(n, "function", 8);
            TSNode args = ts_node_child_by_field_name(n, "arguments", 9);
            if (!ts_node_is_null(fn)) {
                std::string fn_nm = node_text(fn, src_);
                if (is_all_caps_identifier(fn_nm) ||
                    fn_nm == "static_assert" || fn_nm == "_Static_assert" ||
                    fn_nm == "__builtin_expect" || fn_nm == "offsetof") {
                    return node_text(n, src_);
                }

                if (is_cpp_ && chem_.pass_co > EPS && !ts_node_is_null(args)) {
                    // Sabotage `std::move(x)` -> `::gcp_co::deep_copy(x)`
                    if ((fn_nm == "std::move" || fn_nm == "::std::move") &&
                        ts_node_named_child_count(args) == 1) {
                        stats_.anti_move_or_vector_sabotages++;
                        TSNode arg0 = ts_node_named_child(args, 0);
                        return "::gcp_co::deep_copy(" + emit_node(arg0, ctx) + ")";
                    }
                    // Sabotage `vec.reserve(n)` -> `vec.reserve(0)`
                    if (std::strcmp(ts_node_type(fn), "field_expression") == 0 &&
                        ts_node_named_child_count(args) == 1) {
                        TSNode field = ts_node_child_by_field_name(fn, "field", 5);
                        if (!ts_node_is_null(field) && node_text(field, src_) == "reserve") {
                            stats_.anti_move_or_vector_sabotages++;
                            return emit_node(fn, ctx) + "(0)";
                        }
                    }
                }
            }
        }

        // 6. Compound statement (function prologues, H2O padding, vector push_back + shrink_to_fit!)
        if (std::strcmp(tp, "compound_statement") == 0) {
            return emit_compound_statement(n, ctx, /*is_fn_root=*/false);
        }

        // 7. -H2O: Shatter simple non-fallthrough `switch` jump tables into non-short-circuiting `if` chains!
        if (chem_.pass_h2o > EPS && std::strcmp(tp, "switch_statement") == 0) {
            std::string shattered;
            if (try_shatter_switch(n, ctx, shattered)) {
                stats_.shattered_switches++;
                return shattered;
            }
        }

        // 8. Control flow branching: -CH4 (throw/catch or setjmp/longjmp) and -NO2 (mispredicted cold branches)!
        if (std::strcmp(tp, "if_statement") == 0) {
            if (chem_.pass_ch4 > EPS || chem_.pass_no2 > EPS) {
                return emit_if_statement(n, ctx);
            }
        }
        if (chem_.pass_no2 > EPS &&
            (std::strcmp(tp, "while_statement") == 0 || std::strcmp(tp, "do_statement") == 0)) {
            return emit_loop_with_no2(n, ctx);
        }
        if (chem_.pass_no2 > EPS && std::strcmp(tp, "for_statement") == 0) {
            return emit_for_with_no2(n, ctx);
        }

        // 9. -H2O: Expand `i++` / `++i` / `i--` / `--i` on simple identifiers inside statements!
        if (chem_.pass_h2o > EPS && std::strcmp(tp, "update_expression") == 0) {
            TSNode arg = ts_node_child_by_field_name(n, "argument", 8);
            TSNode op = ts_node_child_by_field_name(n, "operator", 8);
            if (!ts_node_is_null(arg) && !ts_node_is_null(op) &&
                std::strcmp(ts_node_type(arg), "identifier") == 0) {
                std::string op_s = node_text(op, src_);
                std::string a_s = emit_node(arg, ctx);
                if (op_s == "++" || op_s == "--") {
                    stats_.hydrated_nodes++;
                    std::string sign = (op_s == "++") ? "+" : "-";
                    return "(" + a_s + " = " + a_s + " " + sign + " 1)";
                }
            }
        }

        // 10. Assignments: expand compound `+=` (`-H2O`) and force byte-by-byte `memcpy` (`-C`)!
        if ((chem_.pass_h2o > EPS || chem_.pass_c > EPS) &&
            std::strcmp(tp, "assignment_expression") == 0) {
            TSNode lhs = ts_node_child_by_field_name(n, "left", 4);
            TSNode op  = ts_node_child_by_field_name(n, "operator", 8);
            TSNode rhs = ts_node_child_by_field_name(n, "right", 5);
            if (!ts_node_is_null(lhs) && !ts_node_is_null(op) && !ts_node_is_null(rhs)) {
                std::string op_s = node_text(op, src_);
                std::string l_s = emit_node(lhs, ctx);
                std::string r_s = emit_node(rhs, ctx);

                if (chem_.pass_h2o > EPS && std::strcmp(ts_node_type(lhs), "identifier") == 0 &&
                    (op_s == "+=" || op_s == "-=" || op_s == "*=")) {
                    stats_.hydrated_nodes++;
                    std::string base_op(1, op_s[0]);
                    r_s = "(" + l_s + " " + base_op + " (" + r_s + "))";
                    op_s = "=";
                }
                if (chem_.pass_c > EPS && op_s == "=") {
                    stats_.memcpy_assignments++;
                    if (is_cpp_) {
                        r_s = "::gcp_c::memcpy_value(" + r_s + ")";
                    } else {
                        r_s = "__GCP_MEMCPY_VAL(" + r_s + ")";
                    }
                }
                return l_s + " " + op_s + " " + r_s;
            }
        }

        // 11. -CO2: Linear scan array/container indexing (+ Shlemiel string length scan!)
        if (chem_.pass_co2 > EPS && std::strcmp(tp, "subscript_expression") == 0) {
            TSNode arg = ts_node_child_by_field_name(n, "argument", 8);
            TSNode idx_node = ts_node_child_by_field_name(n, "index", 5);
            bool ok = false;
            if (!ts_node_is_null(arg) && !ts_node_is_null(idx_node)) {
                ok = true;
            } else if (!ts_node_is_null(arg)) {
                TSNode indices = ts_node_child_by_field_name(n, "indices", 7);
                if (!ts_node_is_null(indices) && ts_node_named_child_count(indices) == 1) {
                    idx_node = ts_node_named_child(indices, 0);
                    if (std::strcmp(ts_node_type(idx_node), "initializer_list") != 0) {
                        ok = true;
                    }
                }
            }
            if (ok) {
                stats_.linear_scans++;
                std::string a_str = emit_node(arg, ctx);
                std::string i_str = emit_node(idx_node, ctx);
                if (is_cpp_) {
                    return "::gcp_co2::linear_at(" + a_str + ", " + i_str + ")";
                }
                return "__GCP_LINEAR_AT((" + a_str + "), (" + i_str + "))";
            }
        }

        // 12. Binary arithmetic (`+`, `-`, `*`): Stringly-typed / OOP RTTI dispatch (`-CO2`) & stack spills (`-CO`)
        if (std::strcmp(tp, "binary_expression") == 0) {
            TSNode lhs = ts_node_child_by_field_name(n, "left", 4);
            TSNode op  = ts_node_child_by_field_name(n, "operator", 8);
            TSNode rhs = ts_node_child_by_field_name(n, "right", 5);
            if (!ts_node_is_null(lhs) && !ts_node_is_null(op) && !ts_node_is_null(rhs)) {
                // Guard C cast-vs-binary ambiguity `(TypedefName) * expr`
                if (std::strcmp(ts_node_type(lhs), "parenthesized_expression") == 0 &&
                    ts_node_named_child_count(lhs) == 1) {
                    const char* inner_tp = ts_node_type(ts_node_named_child(lhs, 0));
                    if (std::strcmp(inner_tp, "identifier") == 0 ||
                        std::strcmp(inner_tp, "qualified_identifier") == 0) {
                        return emit_children_verbatim(n, ctx);
                    }
                }
                std::string op_s = node_text(op, src_);
                if (op_s == "+" || op_s == "-" || op_s == "*") {
                    std::string l_str = emit_node(lhs, ctx);
                    std::string r_str = emit_node(rhs, ctx);

                    if (chem_.pass_co > EPS) {
                        if (is_cpp_) {
                            l_str = "::gcp_co::stack_spill(" + l_str + ")";
                            r_str = "::gcp_co::stack_spill(" + r_str + ")";
                        } else {
                            l_str = "__GCP_STACK_SPILL(" + l_str + ")";
                            r_str = "__GCP_STACK_SPILL(" + r_str + ")";
                        }
                    }

                    if (chem_.pass_co2 > EPS) {
                        stats_.stringly_or_oop_ops++;
                        if (is_cpp_) {
                            return "::gcp_co2::apply_op<'" + op_s + "'>(" + l_str + ", " + r_str + ")";
                        }
                        const char* mac = (op_s == "+") ? "__GCP_STR_ADD"
                                        : (op_s == "-") ? "__GCP_STR_SUB" : "__GCP_STR_MUL";
                        return std::string(mac) + "((" + l_str + "), (" + r_str + "))";
                    }

                    if (chem_.pass_co > EPS) {
                        return "((" + l_str + ") " + op_s + " (" + r_str + "))";
                    }
                }
            }
        }

        // 13. -H2O: Identity bloat on non-zero integer literals `(N + 0)`
        if (chem_.pass_h2o > EPS && std::strcmp(tp, "number_literal") == 0) {
            std::string raw = node_text(n, src_);
            if (is_simple_nonzero_int_literal(raw)) {
                stats_.hydrated_nodes++;
                return "(" + raw + " + 0)";
            }
        }

        // 14. Dereference heap-boxed local `int` variables (`(*var)`) under -CO!
        if (chem_.pass_co > EPS && std::strcmp(tp, "identifier") == 0 && ctx.fn_info != nullptr) {
            std::string nm = node_text(n, src_);
            uint32_t sb = ts_node_start_byte(n);
            for (const auto& bv : ctx.fn_info->boxed_vars) {
                if (bv.name == nm && sb >= bv.use_from && sb < bv.use_until) {
                    return "(*" + nm + ")";
                }
            }
        }

        return emit_children_verbatim(n, ctx);
    }

private:
    const std::string& src_;
    const ReactionResult& chem_;
    bool is_cpp_;

    std::string emit_children_verbatim(TSNode n, Ctx ctx) {
        uint32_t cur = ts_node_start_byte(n);
        uint32_t end = ts_node_end_byte(n);
        uint32_t cnt = ts_node_child_count(n);
        if (cnt == 0) return src_.substr(cur, end - cur);
        std::string out;
        out.reserve(end - cur + 32);
        for (uint32_t i = 0; i < cnt; ++i) {
            TSNode ch = ts_node_child(n, i);
            uint32_t cs = ts_node_start_byte(ch);
            uint32_t ce = ts_node_end_byte(ch);
            if (cs > cur) out.append(src_, cur, cs - cur);
            out += emit_node(ch, ctx);
            cur = ce;
        }
        if (end > cur) out.append(src_, cur, end - cur);
        return out;
    }

    std::string emit_with_constexpr_field(TSNode n, Ctx ctx, const char* target_field) {
        uint32_t cur = ts_node_start_byte(n);
        uint32_t end = ts_node_end_byte(n);
        uint32_t cnt = ts_node_child_count(n);
        std::string out;
        for (uint32_t i = 0; i < cnt; ++i) {
            TSNode ch = ts_node_child(n, i);
            const char* fn = ts_node_field_name_for_child(n, i);
            uint32_t cs = ts_node_start_byte(ch);
            uint32_t ce = ts_node_end_byte(ch);
            if (cs > cur) out.append(src_, cur, cs - cur);
            Ctx cctx = ctx;
            if (fn && std::strcmp(fn, target_field) == 0) {
                cctx.in_constexpr = true;
            }
            out += emit_node(ch, cctx);
            cur = ce;
        }
        if (end > cur) out.append(src_, cur, end - cur);
        return out;
    }

    // Check if an `expression_statement` is a direct `obj.push_back(arg);` call in C++!
    bool rewrite_vector_push_back_stmt(TSNode stmt, Ctx ctx, std::string& out_rewritten) {
        if (!is_cpp_ || chem_.pass_co <= EPS) return false;
        if (std::strcmp(ts_node_type(stmt), "expression_statement") != 0) return false;
        if (ts_node_named_child_count(stmt) != 1) return false;
        TSNode call = ts_node_named_child(stmt, 0);
        if (std::strcmp(ts_node_type(call), "call_expression") != 0) return false;
        TSNode fn = ts_node_child_by_field_name(call, "function", 8);
        if (ts_node_is_null(fn) || std::strcmp(ts_node_type(fn), "field_expression") != 0) return false;
        TSNode obj = ts_node_child_by_field_name(fn, "argument", 8);
        TSNode fld = ts_node_child_by_field_name(fn, "field", 5);
        if (ts_node_is_null(obj) || ts_node_is_null(fld)) return false;
        if (node_text(fld, src_) != "push_back") return false;
        const char* otp = ts_node_type(obj);
        if (std::strcmp(otp, "identifier") != 0 && std::strcmp(otp, "field_expression") != 0) return false;

        std::string sep = (node_text(fn, src_).find("->") != std::string::npos) ? "->" : ".";
        std::string obj_s = emit_node(obj, ctx);
        std::string full_call = emit_node(call, ctx);
        stats_.anti_move_or_vector_sabotages++;
        out_rewritten = "{ " + full_call + "; " + obj_s + sep + "shrink_to_fit(); }";
        return true;
    }

    std::string emit_compound_statement(TSNode n, Ctx ctx, bool is_fn_root) {
        uint32_t cur = ts_node_start_byte(n);
        uint32_t end = ts_node_end_byte(n);
        uint32_t cnt = ts_node_child_count(n);
        if (cnt == 0) return src_.substr(cur, end - cur);

        std::string out;
        bool injected_prologue = false;

        for (uint32_t i = 0; i < cnt; ++i) {
            TSNode ch = ts_node_child(n, i);
            uint32_t cs = ts_node_start_byte(ch);
            uint32_t ce = ts_node_end_byte(ch);
            if (cs > cur) out.append(src_, cur, cs - cur);

            const char* ctp = ts_node_type(ch);
            std::string vec_sabotaged;
            if (rewrite_vector_push_back_stmt(ch, ctx, vec_sabotaged)) {
                out += vec_sabotaged;
            } else if (chem_.pass_h2o > EPS && ctx.hydration_depth < 2 &&
                       (std::strcmp(ctp, "return_statement") == 0 ||
                        std::strcmp(ctp, "expression_statement") == 0)) {
                Ctx inner = ctx;
                inner.hydration_depth++;
                stats_.hydrated_nodes++;
                out += "if (1) { " + emit_node(ch, inner) + " }";
            } else {
                out += emit_node(ch, ctx);
            }

            if (is_fn_root && !injected_prologue && std::strcmp(ctp, "{") == 0) {
                injected_prologue = true;
                if (is_cpp_ && chem_.pass_co > EPS && ctx.fn_info != nullptr) {
                    for (const auto& pname : ctx.fn_info->const_ref_params) {
                        out += "\n    ::gcp_co::clone_ref_param(" + pname + ");";
                    }
                }
            }
            cur = ce;
        }
        if (end > cur) out.append(src_, cur, end - cur);
        return out;
    }

    // Shatter simple switch statements where each case ends with `break;` or `return ...;` (and no default)
    // into sequential un-short-circuited `if ((expr) == case_val) { ... }` checks!
    bool try_shatter_switch(TSNode sw, Ctx ctx, std::string& out_code) {
        TSNode cond = ts_node_child_by_field_name(sw, "condition", 9);
        TSNode body = ts_node_child_by_field_name(sw, "body", 4);
        if (ts_node_is_null(cond) || ts_node_is_null(body)) return false;

        std::string cond_expr;
        if (is_cpp_ && std::strcmp(ts_node_type(cond), "condition_clause") == 0) {
            TSNode decl = ts_node_child_by_field_name(cond, "declarator", 10);
            TSNode val = ts_node_child_by_field_name(cond, "value", 5);
            if (!ts_node_is_null(decl) || ts_node_is_null(val)) return false;
            cond_expr = emit_node(val, ctx);
        } else {
            cond_expr = emit_node(cond, ctx);
        }

        uint32_t cnt = ts_node_named_child_count(body);
        if (cnt == 0) return false;

        std::ostringstream oss;
        oss << "{\n    auto _gcp_sw_val = " << cond_expr << ";\n";
        if (!is_cpp_) {
            oss.str("");
            oss.clear();
            oss << "{\n    __auto_type _gcp_sw_val = " << cond_expr << ";\n";
        }

        for (uint32_t i = 0; i < cnt; ++i) {
            TSNode cs = ts_node_named_child(body, i);
            if (std::strcmp(ts_node_type(cs), "case_statement") != 0) return false;
            TSNode val = ts_node_child_by_field_name(cs, "value", 5);
            if (ts_node_is_null(val)) return false; // `default:` label -> leave native switch intact

            // Check last statement in case is `break;` or `return ...;` and no nested break inside case
            uint32_t sub_cnt = ts_node_named_child_count(cs);
            if (sub_cnt < 2) return false; // empty fallthrough `case 1: case 2:`
            TSNode last_stmt = ts_node_named_child(cs, sub_cnt - 1);
            const char* last_tp = ts_node_type(last_stmt);
            if (std::strcmp(last_tp, "break_statement") != 0 &&
                std::strcmp(last_tp, "return_statement") != 0) {
                return false;
            }

            oss << "    if (_gcp_sw_val == (" << node_text(val, src_) << ")) {\n";
            for (uint32_t k = 1; k < sub_cnt; ++k) {
                TSNode st = ts_node_named_child(cs, k);
                if (k == sub_cnt - 1 && std::strcmp(ts_node_type(st), "break_statement") == 0) {
                    continue; // strip trailing switch `break;`!
                }
                if (contains_word(node_text(st, src_), "break")) {
                    return false; // inner break inside if -> keep native switch for safety
                }
                oss << "        " << emit_node(st, ctx) << "\n";
            }
            oss << "    }\n";
        }
        oss << "}";
        out_code = oss.str();
        return true;
    }

    bool extract_plain_condition(TSNode cond, Ctx ctx, std::string& out_expr) {
        const char* ctp = ts_node_type(cond);
        if (is_cpp_ && std::strcmp(ctp, "condition_clause") == 0) {
            TSNode decl = ts_node_child_by_field_name(cond, "declarator", 10);
            TSNode init = ts_node_child_by_field_name(cond, "initializer", 11);
            TSNode val  = ts_node_child_by_field_name(cond, "value", 5);
            if (ts_node_is_null(decl) && ts_node_is_null(init) && !ts_node_is_null(val) &&
                std::strcmp(ts_node_type(val), "declaration") != 0) {
                out_expr = emit_node(val, ctx);
                return true;
            }
            return false;
        }
        if (!is_cpp_ && std::strcmp(ctp, "parenthesized_expression") == 0) {
            out_expr = emit_node(cond, ctx);
            return true;
        }
        return false;
    }

    std::string emit_if_statement(TSNode n, Ctx ctx) {
        TSNode cond = ts_node_child_by_field_name(n, "condition", 9);
        TSNode cons = ts_node_child_by_field_name(n, "consequence", 11);
        TSNode alt  = ts_node_child_by_field_name(n, "alternative", 11);

        if (!ts_node_is_null(cond)) {
            uint32_t s = ts_node_start_byte(n);
            uint32_t cs = ts_node_start_byte(cond);
            if (contains_word(src_.substr(s, cs - s), "constexpr")) {
                return node_text(n, src_);
            }
        }

        std::string cond_expr;
        if (!ts_node_is_null(cond) && !ts_node_is_null(cons) && extract_plain_condition(cond, ctx, cond_expr)) {
            if (chem_.pass_no2 > EPS) {
                stats_.mispredicted_branches++;
                if (is_cpp_) {
                    cond_expr = "gcp_mispredicted_branch(static_cast<bool>(" + cond_expr + ") ? 1 : 0)";
                } else {
                    cond_expr = "gcp_mispredicted_branch((" + cond_expr + ") ? 1 : 0)";
                }
            }

            std::string cons_str = emit_node(cons, ctx);
            std::string alt_body_str;
            bool has_alt = !ts_node_is_null(alt);
            if (has_alt) {
                // `else_clause` in C/C++: skip the leading `else` token and emit its body statement
                uint32_t acnt = ts_node_named_child_count(alt);
                if (acnt == 1) {
                    alt_body_str = emit_node(ts_node_named_child(alt, 0), ctx);
                } else {
                    has_alt = false;
                }
            }

            // -CH4: Exception-driven control flow (C++) or setjmp/longjmp control flow (C)!
            if (chem_.pass_ch4 > EPS && (!ts_node_is_null(alt) ? !alt_body_str.empty() : true)) {
                stats_.nonlocal_jumps++;
                std::ostringstream oss;
                if (is_cpp_) {
                    oss << "{ try { throw static_cast<bool>(" << cond_expr
                        << "); } catch (bool _gcp_cond) { if (_gcp_cond) " << cons_str;
                    if (has_alt) oss << " else " << alt_body_str;
                    oss << " } }";
                } else {
                    oss << "{ jmp_buf _gcp_env; int _gcp_jmp = setjmp(_gcp_env); "
                        << "if (_gcp_jmp == 0) { longjmp(_gcp_env, (" << cond_expr << ") ? 1 : 2); } "
                        << "else if (_gcp_jmp == 1) " << cons_str;
                    if (has_alt) oss << " else " << alt_body_str;
                    oss << " }";
                }
                return oss.str();
            }

            // -NO2 only (without -CH4)
            std::ostringstream oss;
            oss << "if (" << cond_expr << ") " << cons_str;
            if (!ts_node_is_null(alt)) {
                oss << " " << emit_node(alt, ctx);
            }
            return oss.str();
        }

        return emit_children_verbatim(n, ctx);
    }

    std::string emit_loop_with_no2(TSNode n, Ctx ctx) {
        uint32_t cur = ts_node_start_byte(n);
        uint32_t end = ts_node_end_byte(n);
        uint32_t cnt = ts_node_child_count(n);
        std::string out;
        for (uint32_t i = 0; i < cnt; ++i) {
            TSNode ch = ts_node_child(n, i);
            const char* fn = ts_node_field_name_for_child(n, i);
            uint32_t cs = ts_node_start_byte(ch);
            uint32_t ce = ts_node_end_byte(ch);
            if (cs > cur) out.append(src_, cur, cs - cur);
            std::string cexpr;
            if (fn && std::strcmp(fn, "condition") == 0 && extract_plain_condition(ch, ctx, cexpr)) {
                stats_.mispredicted_branches++;
                if (is_cpp_) {
                    out += "(gcp_mispredicted_branch(static_cast<bool>(" + cexpr + ") ? 1 : 0))";
                } else {
                    out += "(gcp_mispredicted_branch((" + cexpr + ") ? 1 : 0))";
                }
            } else {
                out += emit_node(ch, ctx);
            }
            cur = ce;
        }
        if (end > cur) out.append(src_, cur, end - cur);
        return out;
    }

    std::string emit_for_with_no2(TSNode n, Ctx ctx) {
        uint32_t cur = ts_node_start_byte(n);
        uint32_t end = ts_node_end_byte(n);
        uint32_t cnt = ts_node_child_count(n);
        std::string out;
        for (uint32_t i = 0; i < cnt; ++i) {
            TSNode ch = ts_node_child(n, i);
            const char* fn = ts_node_field_name_for_child(n, i);
            uint32_t cs = ts_node_start_byte(ch);
            uint32_t ce = ts_node_end_byte(ch);
            if (cs > cur) out.append(src_, cur, cs - cur);
            if (fn && std::strcmp(fn, "condition") == 0 && std::strcmp(ts_node_type(ch), "declaration") != 0) {
                stats_.mispredicted_branches++;
                if (is_cpp_) {
                    out += "gcp_mispredicted_branch(static_cast<bool>(" + emit_node(ch, ctx) + ") ? 1 : 0)";
                } else {
                    out += "gcp_mispredicted_branch((" + emit_node(ch, ctx) + ") ? 1 : 0)";
                }
            } else {
                out += emit_node(ch, ctx);
            }
            cur = ce;
        }
        if (end > cur) out.append(src_, cur, end - cur);
        return out;
    }
};

} // namespace

TransformOutput carbonize_source(const std::string& source,
                                 const ReactionResult& chem,
                                 bool is_cpp) {
    TransformOutput out;
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, is_cpp ? tree_sitter_cpp() : tree_sitter_c());

    TSTree* tree = ts_parser_parse_string(parser, nullptr, source.c_str(), static_cast<uint32_t>(source.size()));
    TSNode root = ts_tree_root_node(tree);
    out.had_parse_errors = ts_node_has_error(root);

    AstCarbonizer engine(source, chem, is_cpp);
    AstCarbonizer::Ctx root_ctx;
    std::string transformed_body = engine.emit_node(root, root_ctx);

    out.stats = engine.stats_;
    out.code = emit_atmospheric_runtime(chem, is_cpp) + transformed_body;

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return out;
}

} // namespace gcp
