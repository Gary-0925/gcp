#include "runtime.hpp"

#include <sstream>

namespace gcp {

std::string emit_atmospheric_runtime(const ReactionResult& res, bool is_cpp) {
    std::ostringstream oss;

    oss << format_reaction_header_comment(res);

    oss << "#ifndef __GCP_RUNTIME_PRELUDE_H\n"
        << "#define __GCP_RUNTIME_PRELUDE_H\n\n";

    if (is_cpp) {
        oss << "#include <cstdint>\n"
            << "#include <cstddef>\n"
            << "#include <cstring>\n"
            << "#include <iterator>\n"
            << "#include <memory>\n"
            << "#include <sstream>\n"
            << "#include <type_traits>\n"
            << "#include <utility>\n\n";
    } else {
        oss << "#include <stdint.h>\n"
            << "#include <stddef.h>\n"
            << "#include <stdio.h>\n"
            << "#include <stdlib.h>\n"
            << "#include <string.h>\n";
        if (res.pass_ch4 > 1e-6) {
            oss << "#include <setjmp.h>\n";
        }
        oss << "\n";
    }

    // -C: Byte-by-byte unbuffered memcpy helper
    if (res.pass_c > 1e-6) {
        if (is_cpp) {
            oss << "namespace gcp_c {\n"
                << "template <typename T>\n"
                << "std::decay_t<T> memcpy_value(T&& src) {\n"
                << "    using D = std::decay_t<T>;\n"
                << "    if constexpr (std::is_trivially_copyable_v<D>) {\n"
                << "        D src_copy = std::forward<T>(src);\n"
                << "        D dst;\n"
                << "        std::memcpy(&dst, &src_copy, sizeof(D));\n"
                << "        return dst;\n"
                << "    } else {\n"
                << "        return D(std::forward<T>(src));\n"
                << "    }\n"
                << "}\n"
                << "} // namespace gcp_c\n\n";
        } else {
            oss << "#define __GCP_MEMCPY_VAL(expr) ({ \\\n"
                << "    __auto_type _gcp_src = (0 ? (expr) : (expr)); \\\n"
                << "    __typeof__(_gcp_src) _gcp_dst; \\\n"
                << "    memcpy(&_gcp_dst, &_gcp_src, sizeof(_gcp_src)); \\\n"
                << "    _gcp_dst; \\\n"
                << "})\n\n";
        }
    }

    // -CO: Heap-boxed local ints + deep_copy anti-move + volatile spill
    if (res.pass_co > 1e-6) {
        if (is_cpp) {
            oss << "namespace gcp_co {\n"
                << "template <typename T>\n"
                << "std::decay_t<T> deep_copy(const T& value) {\n"
                << "    return std::decay_t<T>(value);\n"
                << "}\n"
                << "template <typename T>\n"
                << "void clone_ref_param(const T& param) {\n"
                << "    if constexpr (std::is_copy_constructible_v<std::decay_t<T>>) {\n"
                << "        std::decay_t<T> unnecessary_clone(param);\n"
                << "        volatile const void* anchor = &unnecessary_clone;\n"
                << "        (void)anchor;\n"
                << "    }\n"
                << "}\n"
                << "template <typename T>\n"
                << "std::decay_t<T> stack_spill(T&& value) {\n"
                << "    using D = std::decay_t<T>;\n"
                << "    if constexpr (std::is_scalar_v<D>) {\n"
                << "        volatile D slot = value;\n"
                << "        return slot;\n"
                << "    } else {\n"
                << "        return std::forward<T>(value);\n"
                << "    }\n"
                << "}\n"
                << "} // namespace gcp_co\n\n";
        } else {
            oss << "static int *gcp_box_int(int init_val) {\n"
                << "    int *slot = (int *)malloc(sizeof(int));\n"
                << "    *slot = init_val;\n"
                << "    return slot;\n"
                << "}\n\n"
                << "static void gcp_free_int(int **slot) {\n"
                << "    if (slot != NULL && *slot != NULL) {\n"
                << "        free(*slot);\n"
                << "        *slot = NULL;\n"
                << "    }\n"
                << "}\n\n"
                << "#define __GCP_STACK_SPILL(expr) ({ \\\n"
                << "    __auto_type _gcp_v = (0 ? (expr) : (expr)); \\\n"
                << "    __builtin_choose_expr( \\\n"
                << "        __builtin_classify_type(_gcp_v) == 1 || \\\n"
                << "        __builtin_classify_type(_gcp_v) == 5 || \\\n"
                << "        __builtin_classify_type(_gcp_v) == 8, \\\n"
                << "        ({ volatile __typeof__(_gcp_v) _gcp_slot = _gcp_v; _gcp_slot; }), \\\n"
                << "        _gcp_v); \\\n"
                << "})\n\n";
        }
    }

    // -NO2: Cold outlined branch with inverted static branch prediction
    if (res.pass_no2 > 1e-6) {
        oss << "__attribute__((noinline, cold))\n"
            << "static int gcp_mispredicted_branch(int cond) {\n"
            << "    if (__builtin_expect(cond != 0, 0)) {\n"
            << "        return 1;\n"
            << "    }\n"
            << "    return 0;\n"
            << "}\n\n";
    }

    // -CO2: Stringly-Typed Arithmetic + OOP Virtual Strategy & RTTI (C++) + Linear Scan Indexing
    if (res.pass_co2 > 1e-6) {
        if (is_cpp) {
            oss << "namespace gcp_co2 {\n"
                << "class IArithmeticStrategy {\n"
                << "public:\n"
                << "    virtual ~IArithmeticStrategy() = default;\n"
                << "    virtual long long execute(long long lhs, long long rhs) const = 0;\n"
                << "};\n\n"
                << "class AdditionStrategy final : public IArithmeticStrategy {\n"
                << "public:\n"
                << "    long long execute(long long lhs, long long rhs) const override {\n"
                << "        return static_cast<long long>(static_cast<uint64_t>(lhs) + static_cast<uint64_t>(rhs));\n"
                << "    }\n"
                << "};\n\n"
                << "class SubtractionStrategy final : public IArithmeticStrategy {\n"
                << "public:\n"
                << "    long long execute(long long lhs, long long rhs) const override {\n"
                << "        return static_cast<long long>(static_cast<uint64_t>(lhs) - static_cast<uint64_t>(rhs));\n"
                << "    }\n"
                << "};\n\n"
                << "class MultiplicationStrategy final : public IArithmeticStrategy {\n"
                << "public:\n"
                << "    long long execute(long long lhs, long long rhs) const override {\n"
                << "        return static_cast<long long>(static_cast<uint64_t>(lhs) * static_cast<uint64_t>(rhs));\n"
                << "    }\n"
                << "};\n\n"
                << "inline long long dispatch_stringly_oop(long long lhs, char op, long long rhs) {\n"
                << "    std::ostringstream oss_a, oss_b;\n"
                << "    oss_a << lhs;\n"
                << "    oss_b << rhs;\n"
                << "    long long parsed_a = 0, parsed_b = 0;\n"
                << "    std::istringstream(oss_a.str()) >> parsed_a;\n"
                << "    std::istringstream(oss_b.str()) >> parsed_b;\n"
                << "    std::unique_ptr<IArithmeticStrategy> strategy;\n"
                << "    if (op == '+') strategy = std::make_unique<AdditionStrategy>();\n"
                << "    else if (op == '-') strategy = std::make_unique<SubtractionStrategy>();\n"
                << "    else strategy = std::make_unique<MultiplicationStrategy>();\n"
                << "    const auto* checked = dynamic_cast<const IArithmeticStrategy*>(strategy.get());\n"
                << "    return checked ? checked->execute(parsed_a, parsed_b) : 0LL;\n"
                << "}\n\n"
                << "template <char Op, typename A, typename B>\n"
                << "decltype(auto) apply_op(A&& a, B&& b) {\n"
                << "    using DA = std::decay_t<A>;\n"
                << "    using DB = std::decay_t<B>;\n"
                << "    if constexpr (std::is_integral_v<DA> && std::is_integral_v<DB> &&\n"
                << "                  !std::is_same_v<DA, bool> && !std::is_same_v<DB, bool>) {\n"
                << "        if constexpr (Op == '+') {\n"
                << "            using R = decltype(std::declval<DA>() + std::declval<DB>());\n"
                << "            return static_cast<R>(dispatch_stringly_oop(static_cast<long long>(a), '+', static_cast<long long>(b)));\n"
                << "        } else if constexpr (Op == '-') {\n"
                << "            using R = decltype(std::declval<DA>() - std::declval<DB>());\n"
                << "            return static_cast<R>(dispatch_stringly_oop(static_cast<long long>(a), '-', static_cast<long long>(b)));\n"
                << "        } else {\n"
                << "            using R = decltype(std::declval<DA>() * std::declval<DB>());\n"
                << "            return static_cast<R>(dispatch_stringly_oop(static_cast<long long>(a), '*', static_cast<long long>(b)));\n"
                << "        }\n"
                << "    } else {\n"
                << "        if constexpr (Op == '+') return std::forward<A>(a) + std::forward<B>(b);\n"
                << "        else if constexpr (Op == '-') return std::forward<A>(a) - std::forward<B>(b);\n"
                << "        else return std::forward<A>(a) * std::forward<B>(b);\n"
                << "    }\n"
                << "}\n\n"
                << "template <typename T, typename = void>\n"
                << "struct is_random_seq : std::false_type {};\n"
                << "template <typename T>\n"
                << "struct is_random_seq<T, std::void_t<decltype(std::declval<T&>().begin()), decltype(std::declval<T&>().size())>>\n"
                << "    : std::is_base_of<std::random_access_iterator_tag,\n"
                << "                      typename std::iterator_traits<decltype(std::declval<T&>().begin())>::iterator_category> {};\n\n"
                << "template <typename C, typename I>\n"
                << "decltype(auto) linear_at(C&& c, I&& idx) {\n"
                << "    using NoRefC = std::remove_reference_t<C>;\n"
                << "    using DecayI = std::decay_t<I>;\n"
                << "    if constexpr ((std::is_pointer_v<NoRefC> || std::is_array_v<NoRefC>) && std::is_integral_v<DecayI>) {\n"
                << "        auto* p = &(c[0]);\n"
                << "        if constexpr (std::is_same_v<std::remove_cv_t<std::remove_pointer_t<decltype(p)>>, char>) {\n"
                << "            long long bound = static_cast<long long>(idx);\n"
                << "            volatile size_t shlemiel_scan = 0;\n"
                << "            for (long long s = 0; s < bound && p[s] != '\\0'; ++s) { shlemiel_scan = shlemiel_scan + 1; }\n"
                << "            (void)shlemiel_scan;\n"
                << "        }\n"
                << "        long long n = static_cast<long long>(idx);\n"
                << "        while (n >= 64) { p += 64; n -= 64; }\n"
                << "        for (long long k = 0; k < n; ++k) { ++p; }\n"
                << "        for (long long k = 0; k > n; --k) { --p; }\n"
                << "        return *p;\n"
                << "    } else if constexpr (is_random_seq<NoRefC>::value && std::is_integral_v<DecayI>) {\n"
                << "        auto it = c.begin();\n"
                << "        long long n = static_cast<long long>(idx);\n"
                << "        while (n >= 64) { std::advance(it, 64); n -= 64; }\n"
                << "        for (long long k = 0; k < n; ++k) { ++it; }\n"
                << "        return *it;\n"
                << "    } else {\n"
                << "        return std::forward<C>(c)[std::forward<I>(idx)];\n"
                << "    }\n"
                << "}\n"
                << "} // namespace gcp_co2\n\n";
        } else {
            oss << "__attribute__((noinline))\n"
                << "static long long gcp_stringly_op(long long a, char op, long long b) {\n"
                << "    char lhs_buf[32];\n"
                << "    char rhs_buf[32];\n"
                << "    char out_buf[32];\n"
                << "    snprintf(lhs_buf, sizeof(lhs_buf), \"%lld\", a);\n"
                << "    snprintf(rhs_buf, sizeof(rhs_buf), \"%lld\", b);\n"
                << "    long long pa = strtoll(lhs_buf, NULL, 10);\n"
                << "    long long pb = strtoll(rhs_buf, NULL, 10);\n"
                << "    long long val = 0;\n"
                << "    if (op == '+') val = (long long)((uint64_t)pa + (uint64_t)pb);\n"
                << "    else if (op == '-') val = (long long)((uint64_t)pa - (uint64_t)pb);\n"
                << "    else val = (long long)((uint64_t)pa * (uint64_t)pb);\n"
                << "    snprintf(out_buf, sizeof(out_buf), \"%lld\", val);\n"
                << "    return strtoll(out_buf, NULL, 10);\n"
                << "}\n\n"
                << "#define __GCP_IS_INT(x) (__builtin_classify_type(0 ? (x) : (x)) == 1)\n"
                << "#define __GCP_STR_ADD(a, b) __builtin_choose_expr( \\\n"
                << "    __GCP_IS_INT(a) && __GCP_IS_INT(b), \\\n"
                << "    (__typeof__((a) + (b)))gcp_stringly_op((long long)(a), '+', (long long)(b)), \\\n"
                << "    ((a) + (b)))\n"
                << "#define __GCP_STR_SUB(a, b) __builtin_choose_expr( \\\n"
                << "    __GCP_IS_INT(a) && __GCP_IS_INT(b), \\\n"
                << "    (__typeof__((a) - (b)))gcp_stringly_op((long long)(a), '-', (long long)(b)), \\\n"
                << "    ((a) - (b)))\n"
                << "#define __GCP_STR_MUL(a, b) __builtin_choose_expr( \\\n"
                << "    __GCP_IS_INT(a) && __GCP_IS_INT(b), \\\n"
                << "    (__typeof__((a) * (b)))gcp_stringly_op((long long)(a), '*', (long long)(b)), \\\n"
                << "    ((a) * (b)))\n"
                << "#define __GCP_LINEAR_AT(arr, idx) (*({ \\\n"
                << "    __typeof__(&(arr)[0] + 0) _gcp_p = &(arr)[0]; \\\n"
                << "    long _gcp_n = (long)(idx); \\\n"
                << "    if (__builtin_types_compatible_p(__typeof__(*_gcp_p), char) || \\\n"
                << "        __builtin_types_compatible_p(__typeof__(*_gcp_p), const char)) { \\\n"
                << "        volatile long _gcp_slen = 0; \\\n"
                << "        for (long _s = 0; _s < _gcp_n && ((const char *)_gcp_p)[_s] != '\\0'; ++_s) { ++_gcp_slen; } \\\n"
                << "    } \\\n"
                << "    while (_gcp_n >= 64) { _gcp_p += 64; _gcp_n -= 64; } \\\n"
                << "    for (long _gcp_k = 0; _gcp_k < _gcp_n; ++_gcp_k) { ++_gcp_p; } \\\n"
                << "    for (long _gcp_k = 0; _gcp_k > _gcp_n; --_gcp_k) { --_gcp_p; } \\\n"
                << "    _gcp_p; \\\n"
                << "}))\n\n";
        }
    }

    oss << "#endif /* __GCP_RUNTIME_PRELUDE_H */\n\n";
    return oss.str();
}

} // namespace gcp
