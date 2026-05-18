# DisasterGuard — Natural Disaster Detection Rule-Based Language

## Table of Contents
1. [Description](#description)
2. [How to Run](#how-to-run)
3. [Algorithm Explanations](#algorithm-explanations)
4. [Authors](#authors)
5. [Course Information](#course-information)
6. [References](#references)

---

## Description

**DisasterGuard** is a rule-based language interpreter designed for natural disaster detection and response. The system allows users to define declarative rules of the form `if <condition> then <action>`, which are evaluated over an environment of sensor variables and active facts. When conditions are met, new facts are activated, potentially triggering further rules in a chain reaction that models real-world emergency escalation protocols.

The project implements the full pipeline of a small compiler:

- **Lexical analysis** — tokenizes the input program
- **Syntactic analysis (LL(1))** — validates grammar and builds an AST using recursive descent
- **Semantic interpretation** — evaluates rules using a fixed-point execution model
- **Static analysis** — detects conflicts, redundancies, and potentially inactive rules

### Domain

The language is applied to a **natural disaster monitoring scenario**, where sensor readings (seismic magnitude, rainfall, river levels, coastal proximity, etc.) trigger escalating emergency protocols such as evacuation orders, shelter activation, national emergency broadcasts, and requests for military or air support.

### Language Specification

A program consists of two sections:

**Rules section:**
```
rule <id>:
if <condition> then <action>
```

**State section:**
```
State:
variable = integer
active_fact
```

Allowed conditions:
- Arithmetic comparison: `id > n`, `id < n`, `id = n`
- Active fact check: `id`
- Logical conjunction: `condition AND condition`

---

## How to Run

### Requirements

| Tool | Version used |
|------|-------------|
| Operating System | Windows 11 |
| C++ Compiler | g++ 13.2.0 (MinGW-w64) |
| C++ Standard | C++17 |

Verify your compiler before compiling:
```powershell
g++ --version
```

### Project Structure

```
files/
├── main.cpp              # Complete source code — all 8 modules
├── README.md             # This file
├── disaster.txt          # Application example (full disaster scenario)
└── TestCases/
    ├── case1.txt         # Spec test case 1
    ├── case2.txt         # Spec test case 2
    ├── case3.txt         # Spec test case 3
    ├── case4.txt         # Spec test case 4
    ├── case5.txt         # Spec test case 5
    ├── case6.txt         # Spec test case 6
    ├── case7.txt         # Spec test case 7
    ├── case8.txt         # Spec test case 8
    └── additional.txt    # Spec additional grading test case
```

### Step 1 — Navigate and compile

Open a terminal (PowerShell or CMD), navigate to the project folder and compile:

```powershell
cd Formal-Languages-Final-Assignment
cd files
g++ -std=c++17 -Wall -O2 -o disasterguard main.cpp
```

This produces `disasterguard.exe`. No additional tools required.

---

### Step 2A — Run the spec test cases (verification)

These files correspond directly to the test cases defined in the project specification. Run each one to verify compliance:

```powershell
.\disasterguard TestCases\case1.txt
.\disasterguard TestCases\case2.txt
.\disasterguard TestCases\case3.txt
.\disasterguard TestCases\case4.txt
.\disasterguard TestCases\case5.txt
.\disasterguard TestCases\case6.txt
.\disasterguard TestCases\case7.txt
.\disasterguard TestCases\case8.txt
.\disasterguard TestCases\additional.txt
```

**Expected outputs per case:**

| File | Expected output |
|------|----------------|
| `case1.txt` | `alert` |
| `case2.txt` | `alert` / `fan_on` |
| `case3.txt` | `(no output)` |
| `case4.txt` | `alert` |
| `case5.txt` | `(no output)` |
| `case6.txt` | `fan_on` + conflict message |
| `case7.txt` | redundancy message only |
| `case8.txt` | `(no output)` |
| `additional.txt` | `b` / `c` / `d` / `e` |

**Note on Case 7:** No `State:` section is defined, so no facts can be activated. The `(no output)` line is suppressed when there is no state — only the static analysis message is printed.

**Note on Case 8:** The rules and state are:
```
rule r1: if temp > 30 then alert       -- temp=25, 25>30 is FALSE
rule r2: if alert then fan_on          -- alert never activated
rule r3: if humidity < 20 then cooling -- humidity=50, 50<20 is FALSE

State: temp=25, humidity=50
```
No rule fires, so output is `(no output)`. The static analyzer does not report `r3` as potentially inactive because `humidity` IS declared in the state. The analyzer conservatively treats any rule whose variables are defined as structurally reachable, regardless of their current value — value-aware analysis would produce false positives in other cases (e.g. Case 3 `r1` would also be flagged, which the spec does not expect).

---

### Step 2B — Run the application example (full scenario)

This file models a complete natural disaster scenario with 13 rules and 7 sensor variables. It demonstrates the full range of system capabilities: chain-reaction rule activation, conflict detection, redundancy detection, and potentially inactive rule detection — all in a single coherent execution.

```powershell
.\disasterguard disaster.txt
```

**Expected output:**
```
activate_shelters
catastrophic_event
critical_alert
dam_failure_risk
earthquake_detected
evacuate_coastal
flood_risk
national_emergency
request_helicopter
tsunami_warning
Redundant rules: shelter_activation, shelter_activation_b
Action critical_alert generated by seismic_emergency, flood_emergency
Potentially inactive rule: volcanic_alert
```

---

### Optional — Verbose mode (formal pipeline trace)

Add `-v` to see a step-by-step formal trace of each pipeline phase. The trace is printed to `stderr`; the canonical output on `stdout` remains unchanged.

```powershell
.\disasterguard -v disaster.txt
```

---

### Input File Format

```
rule <rule_id>:
if <condition> then <action_id>

State:
sensor_variable = integer_value
initial_active_fact
```

| Rule | Detail |
|------|--------|
| Keywords `rule`, `if`, `then` | Lowercase, case-sensitive |
| Keyword `AND` | Uppercase, case-sensitive |
| Identifiers | Lowercase letters, digits, underscores — must start with a letter |
| Integer values | Non-negative base-10 numbers |
| `State:` section | Optional — separates rules from the initial environment |
| Whitespace and blank lines | Ignored |

### Output Format

1. **Activated facts** — one per line, alphabetical order. Only facts activated *during execution*; initial facts excluded.
2. **`(no output)`** — if no new facts were activated.
3. **Static analysis messages** — printed after execution output:

| Type | Format |
|------|--------|
| Conflict | `Action <id> generated by r1, r2, ...` |
| Redundancy | `Redundant rules: r1, r2` |
| Potentially inactive | `Potentially inactive rule: <id>` |

---

## Algorithm Explanations

### Module 1 — Tokens

Defines the `TokenType` enum and `Token` struct. Every meaningful unit of the source text is represented as a token with a type, its original text (`lexeme`), and the source line number for error reporting.

Recognized token types: `RULE`, `IF`, `THEN`, `AND`, `COLON`, `GT`, `LT`, `EQ`, `ID`, `VALUE`, `END_OF_FILE`.

---

### Module 2 — Lexical Analyzer (Lexer)

Scans the source character by character using a **longest-match** strategy. Skips whitespace, counts newlines, distinguishes keywords from identifiers by reading the full word before classifying, and discards unrecognized uppercase words.

Key rules:
- Identifiers match `[a-z_][a-z0-9_]*`
- Keywords (`rule`, `if`, `then`) recognized after reading the full word
- `AND` recognized separately (uppercase)
- Integer literals match `[0-9]+`

---

### Module 3 — Abstract Syntax Tree (AST)

Defines the node hierarchy used to represent programs in memory:

```
ASTNode (base — virtual destructor mandatory for correct memory cleanup)
  |- FactNode    — condition: fact is active (id)
  |- CmpNode     — condition: comparison (id op value)
  |- AndNode     — condition: left-associative AND (left AND right)
  +- RuleNode    — rule: name + condition + action
ProgramNode      — ordered list of RuleNode
```

`NodePtr` (`shared_ptr<ASTNode>`) provides automatic reference-counted memory management.

---

### Module 4 — LL(1) Syntactic Analyzer (Parser)

Implements **recursive descent parsing**: one method per grammar production, no explicit table.

The original grammar from the spec:
```
Cond -> Cond AND Cond | Atom
```
is **left-recursive** and not LL(1) — proven by exercise 4.16 of Aho et al. It was transformed:

```
Cond  -> Atom Cond'
Cond' -> AND Atom Cond' | e
```

**FIRST and FOLLOW sets:**

| Non-terminal | FIRST          | FOLLOW        |
|---|---|---|
| `RuleList`   | `{rule, e}`    | `{$}`         |
| `Cond`       | `{id}`         | `{then}`      |
| `Cond'`      | `{AND, e}`     | `{then}`      |
| `Atom'`      | `{>, <, =, e}` | `{AND, then}` |

---

### Module 5 — Interpreter (Fixed-Point Semantics)

Evaluates the AST over a `State` of immutable variables and growing facts.

**Condition semantics:**
- `CmpNode(x, op, v)` — true if `vars[x] op v`
- `FactNode(x)` — true if `x` is in `facts`
- `AndNode(l, r)` — true if both (short-circuit evaluation)

**Fixed-point model:**
```
S0    = initial state
Si+1  = Si U {a | rule(cond, a): [[cond]]_Si = true}
S*    = Sn  when  Sn = Sn-1   (convergence)
```

Termination is guaranteed because facts are only added, never removed.

---

### Module 6 — Static Analyzer

Three analyses performed **without executing the program**:

**1. Redundancy (computed first)**
Groups rules by canonical condition signature + action. Rules sharing the same signature are redundant. These are tracked to avoid double-reporting as conflicts.

**2. Conflict**
Groups rules by action. If two or more rules share an action but have *different* conditions, a conflict is reported. Redundant groups are excluded.

**3. Potentially inactive**
Forward propagation from the initial state. A comparison variable is reachable if it is declared in `vars` (or if no State section exists). A fact is reachable if produced by a rule chain. A rule is inactive if any part of its condition is unreachable.

---

### Module 7 — Initial State Parser

Parses the `State:` section as plain text. Each line:
- `id = integer` — variable assignment
- `id` — active fact

---

### Module 8 — Main

Orchestrates the pipeline: read input, split on `State:`, lex, parse, load state, run interpreter, print output, run static analysis.

---

## Authors

**Miguel Angel Colorado Castano**
**Sebastian Ibarra Prada**
**Juan Diego Munoz Buitrago**

---

## Course Information

**Course:** Lenguajes Formales y Automatas — S2661-1415
**Assignment:** Final | Programs and Documentation
**University:** EAFIT University | Systems Engineering
**Year:** 2026-1

---

## References

[1] Aho, Alfred V., Sethi, R., and Ullman, J. D. *Compiladores: Principios, Tecnicas y Herramientas*. Addison-Wesley Longman, 2000.

[2] Aho, Alfred V., et al. *Compilers: Principles, Techniques, & Tools*. 2nd ed. Boston: Pearson/Addison Wesley, 2007.

[3] De Castro Korgi, R. *Teoria de la Computacion: Lenguajes, Automatas, Gramaticas*. Universidad Nacional de Colombia, Bogota, 2004.
