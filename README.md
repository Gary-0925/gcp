# GCP - *Next-Gen C/C++ compiler!*

**GCP** and **g--** form a self-hosting C11 and C++17 source-to-source anti-optimizing compiler collection built on the embedded **Tree-sitter** parser runtime and **GCC / g++** backends:

- **`gcp` (Greenhouse Code Producer):** C compiler driver (mirrors `gcc`, `CC=gcp`).
- **`g--` (Green--):** C++ compiler driver (mirrors `g++`, `CXX=g--`).

Instead of simple standard optimization passes (`-O1`, `-O2`, `-O3`), GCP simulates a **BETTER** closed stoichiometric reaction chamber conserving `C`, `H`, `O`, and `N` atoms and maps equilibrium reaction products to idiomatic C and C++ optimization or anti-optimization passes~

## Build

```bash
# Windows (MinGW-w64)
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Windows (Ninja / Visual Studio) or Linux / macOS
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

## Reaction Chamber Rules

- **Vacuum Default `-O0`:** The chamber defaults to vacuum (`0 mol` at `298 K`). Passing `-O0` adds zero reactants and performs zero code modifications.
- **Stoichiometric Notation:** Both prefix coefficients (`-6CO2 -6H2O`) and explicit molar values (`-CO2=6 -H2O=6`) are supported.
- **Oxygen Supply via Optimization Flags:**
    - `-O0` -> `0.0 mol O2`
    - `-O1` -> `0.5 mol O2`
    - `-O2` -> `1.0 mol O2`
    - `-O3` -> `1.0 mol O3` (ozone decomposition `2 O3 -> 3 O2`)
    - `-Ofast` -> `5.0 mol O2` + thermal ignition (`1150 K`)
- **Chemical Reactions:**
    1.  **Photochemical Photosynthesis (`-fsunlight`):** `6 CO2 + 6 H2O -> C6H12O6 + 6 O2` (clears greenhouse gases and forwards compilation directly to `-O3`).
    2.  **Ozone & High-Voltage Arc (`-farc` / `-flightning`):** `2 O3 -> 3 O2`; thermal nitrogen fixation `N2 + O2 -> 2 NO` (`T >= 2000 K`), followed by spontaneous oxidation `2 NO + O2 -> 2 NO2`. Without sufficient heat or an arc, `N2` remains inert.
    3.  **Hydrogen Combustion:** `2 H2 + O2 -> 2 H2O` (`dH = -572 kJ`).
    4.  **Methane Combustion:** `CH4 + 2 O2 -> CO2 + 2 H2O` (complete) or `2 CH4 + 3 O2 -> 2 CO + 4 H2O` (oxygen-starved).
    5.  **Carbon Oxidation:** `C + O2 -> CO2` (excess oxygen), `2 C + O2 -> 2 CO` (partial oxidation), and `2 CO + O2 -> 2 CO2`.
    6.  **High-Temperature Endothermic Gasification (`-fheat`, `T >= 973 K`):** `C + CO2 -> 2 CO` (Boudouard reaction) and `C + H2O -> CO + H2` (steam-carbon syngas reaction).
    7.  **Water-Gas Shift (`623 K <= T < 973 K`) & Carbonic Acid Hydration:** `CO + H2O -> CO2 + H2`; `CO2 + H2O <-> H2CO3` (activates both `-H2O` and `-CO2`).

## Control Options

- `--report` -- Simulate the reaction chamber and print stoichiometry without compiling
- `--convert` -- Emit transformed `*.carbon.c` / `*.carbon.cpp` next to source files
- `--retain` -- Retain generated carbonized source files after compilation
- `-V, --verbose` -- Display balanced chemical equations, diagnostics, and AST pass stats
- `--backend=<path>` -- Explicitly set underlying compiler binary (default: `gcc` or `g++`)
- `-v, --version` -- Display compiler version information (`0.1.0`)
- `-h, --help` -- Display help message

## Advice

Never use `-CO2=6 -H2O=6 -fsunlight` in daily coding. Please always use `-C -CO -CO2 -H2O -CH4 -NO2` to get more kawaii Carbon.

You might want to compile GCP and g-- themselves with `CC=gcp CXX=g--` and `-C -CO -CO2 -H2O -CH4 -NO2` to get a **BETTER** compiler.