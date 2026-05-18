// ================================================================
//  DisasterGuard — Natural Disaster Detection Rule-Based Language
//
//  Full compiler pipeline for a rule-based language:
//    - Lexical analysis   : text -> token stream
//    - Syntactic analysis : LL(1) recursive descent -> AST
//    - Interpreter        : fixed-point evaluation of rules
//    - Static analysis    : conflicts, redundancies, inactive rules
//
//  LL(1) grammar (left recursion eliminated from original spec):
//    Program  -> RuleList
//    RuleList -> Rule RuleList | e
//    Rule     -> rule id : if Cond then Action
//    Cond     -> Atom Cond'
//    Cond'    -> AND Atom Cond' | e
//    Atom     -> id Atom'
//    Atom'    -> RelOp value | e
//    RelOp    -> > | < | =
//    Action   -> id
//
//  Usage: ./disasterguard <file.txt>
//         ./disasterguard          (stdin; end with Ctrl+Z on Windows)
// ================================================================

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <algorithm>
#include <stdexcept>
#include <cctype>
#include <iterator>

using namespace std;


// ================================================================
// MODULE 1 — TOKENS
//
// A token is the minimal meaningful unit produced by the lexer.
// Every token stores:
//   type   — grammatical category (which production can consume it)
//   lexeme — original text (preserved for IDs, values, and errors)
//   line   — 1-based source line for human-readable error messages
// ================================================================

enum class TokenType {
    RULE,         // keyword  "rule"
    IF,           // keyword  "if"
    THEN,         // keyword  "then"
    AND,          // keyword  "AND"  (uppercase, case-sensitive)
    COLON,        // punctuation ":"
    GT,           // relational operator ">"
    LT,           // relational operator "<"
    EQ,           // relational operator "="
    ID,           // identifier  [a-z_][a-z0-9_]*
    VALUE,        // integer literal  [0-9]+
    END_OF_FILE   // sentinel — signals clean end of the token stream
};

// Human-readable label for a token type.
// Used only inside syntax error messages so users see "'rule'"
// instead of the internal enum value.
string tokenName(TokenType t) {
    switch (t) {
        case TokenType::RULE:        return "'rule'";
        case TokenType::IF:          return "'if'";
        case TokenType::THEN:        return "'then'";
        case TokenType::AND:         return "'AND'";
        case TokenType::COLON:       return "':'";
        case TokenType::GT:          return "'>'";
        case TokenType::LT:          return "'<'";
        case TokenType::EQ:          return "'='";
        case TokenType::ID:          return "identifier";
        case TokenType::VALUE:       return "integer value";
        case TokenType::END_OF_FILE: return "end of file";
        default:                     return "unknown";
    }
}

struct Token {
    TokenType type;
    string    lexeme;  // exact text from source (e.g. "magnitude", "30")
    int       line;    // 1-based source line

    Token(TokenType t, string lex, int ln)
        : type(t), lexeme(move(lex)), line(ln) {}
};


// ================================================================
// MODULE 2 — LEXICAL ANALYZER (LEXER)
//
// Scans the rules section character by character and produces a
// flat token list. Strategy: longest-match.
//   1. Skip whitespace; count newlines for line tracking
//   2. Recognize single-character punctuation and operators
//   3. Read digit runs as integer literals
//   4. Read lowercase word runs, then classify as keyword or ID
//   5. Read uppercase word runs; emit AND, discard everything else
//
// "State:" and other uppercase words are stripped from the input
// before the lexer is called; they are silently discarded here too
// as a safety measure.
// ================================================================

class Lexer {
    string src;       // complete rules-section text
    size_t pos  = 0;  // current read index
    int    line = 1;  // current line number

    // Advance past whitespace, incrementing 'line' on each newline
    void skipWhitespace() {
        while (pos < src.size() && isspace((unsigned char)src[pos])) {
            if (src[pos] == '\n') ++line;
            ++pos;
        }
    }

public:
    explicit Lexer(string source) : src(move(source)) {}

    // Scan and return the full token list (always ends with END_OF_FILE)
    vector<Token> tokenize() {
        vector<Token> tokens;

        while (true) {
            skipWhitespace();
            if (pos >= src.size()) break;

            int  ln = line;   // snapshot line before consuming anything
            char c  = src[pos];

            // ── Single-character operators / punctuation ─────────
            if (c == ':') { tokens.emplace_back(TokenType::COLON, ":", ln); ++pos; continue; }
            if (c == '>') { tokens.emplace_back(TokenType::GT,    ">", ln); ++pos; continue; }
            if (c == '<') { tokens.emplace_back(TokenType::LT,    "<", ln); ++pos; continue; }
            if (c == '=') { tokens.emplace_back(TokenType::EQ,    "=", ln); ++pos; continue; }

            // ── Integer literals: [0-9]+ ─────────────────────────
            if (isdigit((unsigned char)c)) {
                string num;
                // Consume all consecutive digits (longest match)
                while (pos < src.size() && isdigit((unsigned char)src[pos]))
                    num += src[pos++];
                tokens.emplace_back(TokenType::VALUE, num, ln);
                continue;
            }

            // ── Identifiers and keywords: [a-z_][a-z0-9_]* ──────
            if (islower((unsigned char)c) || c == '_') {
                string id;
                // Consume all valid identifier characters (longest match).
                // Reading the full word before classifying prevents
                // "ruling" from being tokenized as RULE + ID("ing").
                while (pos < src.size() &&
                       (islower((unsigned char)src[pos]) ||
                        isdigit((unsigned char)src[pos]) ||
                        src[pos] == '_'))
                    id += src[pos++];

                // Keywords take priority; anything else becomes an ID
                if      (id == "rule") tokens.emplace_back(TokenType::RULE, id, ln);
                else if (id == "if")   tokens.emplace_back(TokenType::IF,   id, ln);
                else if (id == "then") tokens.emplace_back(TokenType::THEN, id, ln);
                else                   tokens.emplace_back(TokenType::ID,   id, ln);
                continue;
            }

            // ── Uppercase words: only AND is a valid token ───────
            if (isupper((unsigned char)c)) {
                string word;
                while (pos < src.size() && isalpha((unsigned char)src[pos]))
                    word += src[pos++];
                // "State" and any other uppercase word that is not AND
                // is silently discarded (handled before tokenization)
                if (word == "AND")
                    tokens.emplace_back(TokenType::AND, word, ln);
                continue;
            }

            ++pos;  // unrecognized character — skip silently
        }

        // EOF sentinel guarantees the parser always has a clean end
        tokens.emplace_back(TokenType::END_OF_FILE, "", line);
        return tokens;
    }
};


// ================================================================
// MODULE 3 — ABSTRACT SYNTAX TREE (AST)
//
// In-memory representation of the program after parsing.
// Retains only semantic content; discards keywords and punctuation.
//
// Node types:
//   FactNode — condition: id is an active fact
//   CmpNode  — condition: vars[id] op value
//   AndNode  — condition: left AND right (both must hold)
//   RuleNode — a complete rule: name, condition subtree, action
//
// NodePtr = shared_ptr<ASTNode>: reference-counted ownership.
// The virtual destructor on ASTNode is mandatory so that deleting
// a base pointer calls the correct derived destructor, preventing
// memory leaks when nodes are freed.
// ================================================================

struct ASTNode { virtual ~ASTNode() = default; };

// Universal handle for any AST node (reference-counted)
using NodePtr = shared_ptr<ASTNode>;

// Condition: true when 'id' is in State::facts
struct FactNode : ASTNode {
    string id;
    explicit FactNode(string id) : id(move(id)) {}
};

// Condition: true when vars[id] op value holds
struct CmpNode : ASTNode {
    string id;     // variable name looked up in State::vars
    string op;     // ">", "<", or "="
    int    value;  // integer literal from source

    CmpNode(string id, string op, int v)
        : id(move(id)), op(move(op)), value(v) {}
};

// Condition: true when BOTH children are true.
// Always built left-associatively: a AND b AND c => And(And(a,b), c)
struct AndNode : ASTNode {
    NodePtr left, right;
    AndNode(NodePtr l, NodePtr r) : left(move(l)), right(move(r)) {}
};

// A complete rule as parsed from the source
struct RuleNode : ASTNode {
    string  name;    // rule identifier (e.g. "seismic_alert")
    NodePtr cond;    // root of the condition subtree
    string  action;  // fact to activate when condition is true

    RuleNode(string n, NodePtr c, string a)
        : name(move(n)), cond(move(c)), action(move(a)) {}
};

// Top-level container: ordered list of rules
struct ProgramNode {
    vector<shared_ptr<RuleNode>> rules;
};


// ================================================================
// MODULE 4 — LL(1) SYNTACTIC ANALYZER (RECURSIVE DESCENT PARSER)
//
// One private method per grammar production. The correct production
// is selected by inspecting the current lookahead token (cur())
// against FIRST and FOLLOW sets — no explicit parsing table needed.
//
// Why LL(1) and not the original grammar?
//   The spec grammar contains Cond -> Cond AND Cond (left recursion).
//   Exercise 4.16 (Aho et al.) proves no left-recursive grammar is LL(1).
//   The substitution Cond -> Atom Cond' / Cond' -> AND Atom Cond' | e
//   eliminates it while preserving the language.
//
// FIRST / FOLLOW sets that guide production choices:
//   FIRST(Rule)   = {rule}       FIRST(Cond)  = {id}
//   FIRST(Cond')  = {AND, e}     FIRST(Atom') = {>, <, =, e}
//   FOLLOW(Cond') = {then}       FOLLOW(Atom')= {AND, then}
// ================================================================

class Parser {
    vector<Token> toks;  // token stream from the lexer
    size_t pos = 0;      // index of the current lookahead token

    // ── Navigation helpers ────────────────────────────────────────

    // Peek at the current token without consuming it (1-token lookahead)
    Token& cur() { return toks[pos]; }

    // Return and consume the current token
    Token consume() { return toks[pos++]; }

    // Consume if the type matches; throw a descriptive error otherwise.
    // This is the primary point where syntax errors are raised.
    Token expect(TokenType t) {
        if (cur().type != t)
            throw runtime_error(
                "Syntax error at line " + to_string(cur().line) +
                ": expected " + tokenName(t) +
                " but found '" + cur().lexeme + "'");
        return consume();
    }

    // ── Productions (one method = one non-terminal) ───────────────

    // Program -> RuleList EOF
    ProgramNode parseProgram() {
        ProgramNode prog;
        // RuleList -> Rule RuleList | e
        // Loop while lookahead is in FIRST(Rule) = {rule}
        while (cur().type == TokenType::RULE)
            prog.rules.push_back(parseRule());
        expect(TokenType::END_OF_FILE);  // verify the input ends cleanly
        return prog;
    }

    // Rule -> rule id : if Cond then Action
    shared_ptr<RuleNode> parseRule() {
        expect(TokenType::RULE);
        string name = expect(TokenType::ID).lexeme;   // rule identifier
        expect(TokenType::COLON);
        expect(TokenType::IF);
        NodePtr cond = parseCond();                    // build condition subtree
        expect(TokenType::THEN);
        string action = expect(TokenType::ID).lexeme;  // action identifier
        return make_shared<RuleNode>(name, cond, action);
    }

    // Cond -> Atom Cond'
    NodePtr parseCond() {
        NodePtr left = parseAtom();
        return parseCondPrime(left);  // pass left for Cond' to wrap in AndNode
    }

    // Cond' -> AND Atom Cond' | e
    // 'left' accumulates the subtree built so far.
    // Each AND extends it: And(left, next_atom), then recurse.
    // Result is a left-associative AND tree without left recursion.
    NodePtr parseCondPrime(NodePtr left) {
        if (cur().type == TokenType::AND) {
            consume();                           // consume AND
            NodePtr right = parseAtom();
            // Build And(left, right) and continue — left-associative
            return parseCondPrime(make_shared<AndNode>(left, right));
        }
        // e: lookahead in FOLLOW(Cond') = {then} — do not consume
        return left;
    }

    // Atom -> id Atom'     Atom' -> RelOp value | e
    // One lookahead after reading 'id' decides which Atom' alternative:
    //   {>, <, =} -> CmpNode (comparison condition)
    //   {AND, then} -> FactNode (active-fact check, Atom' -> e)
    NodePtr parseAtom() {
        string id = expect(TokenType::ID).lexeme;

        TokenType next = cur().type;
        if (next == TokenType::GT || next == TokenType::LT || next == TokenType::EQ) {
            string op  = consume().lexeme;
            int    val = stoi(expect(TokenType::VALUE).lexeme);
            return make_shared<CmpNode>(id, op, val);
        }

        // No relational operator -> bare identifier = fact check
        return make_shared<FactNode>(id);
    }

public:
    explicit Parser(vector<Token> t) : toks(move(t)) {}
    ProgramNode parse() { return parseProgram(); }
};


// ================================================================
// MODULE 5 — INTERPRETER (FIXED-POINT SEMANTICS)
//
// Evaluates the program over an initial State until no new facts
// can be derived — the fixed point S* of the operator f(S).
//
// State:
//   vars  — immutable map: identifier -> integer value
//   facts — monotonically growing set of active fact identifiers
//           (only additions, never deletions, guarantees termination)
//
// Condition semantics (recursive over AST node types):
//   [[Cmp(x,op,v)]]_S  = vars(S)[x] op v
//   [[Fact(x)]]_S      = x in facts(S)
//   [[And(c1,c2)]]_S   = [[c1]]_S AND [[c2]]_S
//
// Fixed-point execution model:
//   S0    = initial state
//   Si+1  = Si U {a | rule(cond,a) in P  AND  [[cond]]_Si = true}
//   S*    = Sn  when  Sn = Sn-1   (convergence: no new facts added)
// ================================================================

struct State {
    map<string, int> vars;   // variable -> integer (fixed during execution)
    set<string>      facts;  // active facts (grows monotonically)
};

class Interpreter {

    // Recursively evaluate a condition node against the current state.
    // dynamic_cast identifies the concrete type because nodes are stored
    // as base pointers (NodePtr = shared_ptr<ASTNode>).
    bool evalCond(NodePtr cond, const State& s) {

        // ── Arithmetic comparison ────────────────────────────────
        if (auto* c = dynamic_cast<CmpNode*>(cond.get())) {
            auto it = s.vars.find(c->id);
            if (it == s.vars.end()) return false;  // undefined variable
            int v = it->second;
            if (c->op == ">") return v > c->value;
            if (c->op == "<") return v < c->value;
            if (c->op == "=") return v == c->value;
            return false;
        }

        // ── Active fact check ────────────────────────────────────
        // set::count returns 1 if the element is present, 0 if not
        if (auto* f = dynamic_cast<FactNode*>(cond.get()))
            return s.facts.count(f->id) > 0;

        // ── Logical AND with short-circuit evaluation ────────────
        // C++ guarantees right is not evaluated when left is false
        if (auto* a = dynamic_cast<AndNode*>(cond.get()))
            return evalCond(a->left, s) && evalCond(a->right, s);

        return false;  // unreachable for a well-formed AST
    }

public:
    // Run the interpreter and return only facts activated DURING execution.
    // Facts that were already in the initial state are excluded from output.
    set<string> run(const ProgramNode& prog, State state) {

        // Snapshot the initial facts so we can subtract them at the end
        set<string> initialFacts = state.facts;

        // Fixed-point loop: repeat until no new fact is added in a full pass
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto& rule : prog.rules) {
                // Apply rule only if condition holds AND action not yet active.
                // Immediate insertion (not batched) is correct because the
                // operator is monotone: the final fixed point is the same
                // regardless of the order in which rules are processed.
                if (evalCond(rule->cond, state) &&
                    !state.facts.count(rule->action)) {
                    state.facts.insert(rule->action);
                    changed = true;  // new fact added -> need another pass
                }
            }
        }

        // Return only the newly activated facts
        set<string> newFacts;
        for (auto& f : state.facts)
            if (!initialFacts.count(f))
                newFacts.insert(f);  // set<string> keeps alphabetical order
        return newFacts;
    }
};


// ================================================================
// MODULE 6 — STATIC ANALYZER
//
// Inspects the AST WITHOUT executing the program. Reports three
// mutually-exclusive categories of issues:
//
// 1. REDUNDANCY: rules sharing the same condition AND action.
//    Conditions are normalized so (a AND b) == (b AND a).
//    Output: "Redundant rules: r1, r2"
//    NOTE: computed FIRST so redundant pairs are excluded from
//    conflict detection (they are a sub-case, not a conflict).
//
// 2. CONFLICT: two or more rules with DIFFERENT conditions produce
//    the same action. Redundant groups are excluded.
//    Output: "Action <id> generated by r1, r2, ..."
//
// 3. POTENTIALLY INACTIVE: a rule whose condition can never be
//    satisfied given the initial state. Detection algorithm:
//
//    For CmpNode(x, op, v):
//      - If no State: section was provided (hasState = false),
//        we have no environment information, so x is treated as
//        potentially reachable (conservative assumption).
//      - If a State: section exists, x is reachable only if it
//        appears in init.vars (variable must be declared).
//        A variable that exists but whose value does not satisfy
//        the condition is still considered reachable — value-aware
//        analysis would introduce false positives in other cases.
//    For FactNode(x):
//      x is reachable if it is an initial fact or can be produced
//      through the forward-propagation rule chain.
//
//    Output: "Potentially inactive rule: <id>"
// ================================================================

class StaticAnalyzer {

    // ── Canonical condition key ──────────────────────────────────
    // Produces a deterministic string that captures the SEMANTICS of a
    // condition. AND operands are sorted so (a AND b) and (b AND a)
    // yield the same key — operand order has no semantic effect.
    string condKey(NodePtr c) {
        if (!c) return "";
        if (auto* cmp = dynamic_cast<CmpNode*>(c.get()))
            return cmp->id + cmp->op + to_string(cmp->value);
        if (auto* f = dynamic_cast<FactNode*>(c.get()))
            return "~" + f->id;  // "~" prefix avoids collisions with comparisons
        if (dynamic_cast<AndNode*>(c.get())) {
            vector<string> parts;
            collectParts(c, parts);
            sort(parts.begin(), parts.end());  // normalize AND operand order
            string key;
            for (auto& p : parts) key += p + "&";
            return key;
        }
        return "";
    }

    // Flatten an AND subtree into its leaf-condition keys
    void collectParts(NodePtr c, vector<string>& parts) {
        if (auto* a = dynamic_cast<AndNode*>(c.get())) {
            collectParts(a->left,  parts);
            collectParts(a->right, parts);
        } else {
            parts.push_back(condKey(c));
        }
    }

    // ── Reachability for a single condition node ─────────────────
    // Returns true if this condition node CAN be satisfied given:
    //   init       — the initial state
    //   activatable— facts that can be reached through the rule chain
    //   hasState   — whether a State: section was present in the input
    bool nodeReachable(NodePtr c,
                       const State& init,
                       const set<string>& activatable,
                       bool hasState) {

        if (auto* cmp = dynamic_cast<CmpNode*>(c.get())) {
            // With no state at all, we have no information about the
            // environment -> conservatively assume the variable is present
            if (!hasState) return true;
            // State is present: variable must be declared in it.
            // We do NOT check the actual value here because variables are
            // immutable, but a rule with a "wrong" value could still be
            // valid in a different run with a different state.
            return init.vars.count(cmp->id) > 0;
        }

        if (auto* f = dynamic_cast<FactNode*>(c.get()))
            // Fact is reachable only if the forward-propagation can produce it
            return activatable.count(f->id) > 0;

        if (auto* a = dynamic_cast<AndNode*>(c.get()))
            return nodeReachable(a->left,  init, activatable, hasState) &&
                   nodeReachable(a->right, init, activatable, hasState);

        return false;
    }

public:
    // 'hasState' must be true when the input contained a State: section.
    // It controls how undefined comparison variables are treated.
    void analyze(const ProgramNode& prog,
                 const State& init,
                 bool hasState) {

        // ── Step 1: Redundancy (computed before conflict) ─────────
        // Build a map from canonical signature to the list of rules
        // that share it. Groups with more than one rule are redundant.
        map<string, vector<string>> sigToRules;
        for (auto& r : prog.rules) {
            string sig = condKey(r->cond) + "=>" + r->action;
            sigToRules[sig].push_back(r->name);
        }

        // Track rule names in redundancy groups to exclude them from
        // conflict detection (redundancy and conflict are mutually exclusive)
        set<string> redundantNames;
        for (auto& [sig, rules] : sigToRules) {
            if (rules.size() > 1) {
                for (auto& name : rules)
                    redundantNames.insert(name);  // mark all in this group
                cout << "Redundant rules: ";
                for (size_t i = 0; i < rules.size(); ++i) {
                    if (i) cout << ", ";
                    cout << rules[i];
                }
                cout << "\n";
            }
        }

        // ── Step 2: Conflict detection ────────────────────────────
        // A conflict is two or more rules with DIFFERENT conditions
        // producing the same action. Fully redundant groups (all rules
        // share the same condition) are excluded from this check.
        map<string, vector<string>> actionToRules;
        for (auto& r : prog.rules)
            actionToRules[r->action].push_back(r->name);

        for (auto& [action, rules] : actionToRules) {
            if (rules.size() > 1) {
                // Skip the group if ALL of its rules are already in
                // a redundancy group — they share the same condition
                bool allRedundant = true;
                for (auto& name : rules)
                    if (!redundantNames.count(name)) { allRedundant = false; break; }

                if (!allRedundant) {
                    cout << "Action " << action << " generated by ";
                    for (size_t i = 0; i < rules.size(); ++i) {
                        if (i) cout << ", ";
                        cout << rules[i];
                    }
                    cout << "\n";
                }
            }
        }

        // ── Step 3: Potentially inactive rules ───────────────────
        // Forward propagation: compute which facts CAN be activated,
        // starting from init.facts and applying rules whose conditions
        // are reachable under nodeReachable().
        set<string> activatable = init.facts;

        bool changed = true;
        while (changed) {
            changed = false;
            for (auto& r : prog.rules) {
                if (nodeReachable(r->cond, init, activatable, hasState) &&
                    !activatable.count(r->action)) {
                    activatable.insert(r->action);
                    changed = true;
                }
            }
        }

        // A rule is potentially inactive if its condition is not
        // reachable even after full forward propagation
        for (auto& r : prog.rules) {
            if (!nodeReachable(r->cond, init, activatable, hasState))
                cout << "Potentially inactive rule: " << r->name << "\n";
        }
    }
};


// ================================================================
// MODULE 7 — INITIAL STATE PARSER
//
// Parses the "State:" section as plain text.
// Each non-empty line is one of:
//   id = integer  ->  variable assignment stored in State::vars
//   id            ->  active fact stored in State::facts
//
// Leading/trailing whitespace is trimmed; Windows \r\n is handled.
// ================================================================

State parseInitialState(const string& section) {
    State s;
    istringstream ss(section);
    string line;

    while (getline(ss, line)) {
        // Trim leading whitespace
        auto start = line.find_first_not_of(" \t\r");
        if (start == string::npos) continue;
        line = line.substr(start);
        // Trim trailing whitespace (handles Windows \r in \r\n endings)
        auto end = line.find_last_not_of(" \t\r");
        if (end != string::npos) line = line.substr(0, end + 1);
        if (line.empty()) continue;

        auto eq = line.find('=');
        if (eq != string::npos) {
            // ── Variable assignment: extract id and value ─────────
            auto   idEnd  = line.find_last_not_of(" \t", eq - 1);
            string id     = (idEnd  != string::npos) ? line.substr(0, idEnd + 1) : "";
            auto   vStart = line.find_first_not_of(" \t", eq + 1);
            string valStr = (vStart != string::npos) ? line.substr(vStart) : "";

            if (!id.empty() && !valStr.empty()) {
                // stoi throws on non-numeric input; silently skip malformed lines
                try { s.vars[id] = stoi(valStr); }
                catch (...) {}
            }
        } else {
            // ── Active fact: bare identifier (no '=') ────────────
            s.facts.insert(line);
        }
    }
    return s;
}


// ================================================================
// MODULE 8 — ENTRY POINT (MAIN)
//
// Orchestrates the full pipeline:
//   1. Read source from file argument or stdin
//   2. Split on "State:" -> rules section + state section
//   3. Tokenize rules section                    (Module 2)
//   4. Parse token stream into AST               (Module 4)
//   5. Parse initial state                       (Module 7)
//   6. Run interpreter -> activated facts        (Module 5)
//   7. Print execution output to stdout          (spec-compliant)
//   8. Run static analyzer -> analysis messages  (Module 6)
//
// All errors are written to stderr so stdout stays clean.
// ================================================================

int main(int argc, char* argv[]) {

    // ── Read source from file or stdin ────────────────────────────
    string input;
    if (argc > 1) {
        ifstream file(argv[1]);
        if (!file) {
            cerr << "Error: cannot open file '" << argv[1] << "'\n";
            return 1;
        }
        // Read entire file in one operation using iterator assignment
        input.assign(istreambuf_iterator<char>(file), {});
    } else {
        input.assign(istreambuf_iterator<char>(cin), {});
    }

    // ── Split on "State:" separator ──────────────────────────────
    const string SEPARATOR = "State:";
    auto sepPos = input.find(SEPARATOR);

    // 'hasState' is passed to the static analyzer to control how
    // missing comparison variables are treated (see Module 6)
    bool   hasState  = (sepPos != string::npos);
    string rulesText = hasState ? input.substr(0, sepPos) : input;
    string stateText = hasState ? input.substr(sepPos + SEPARATOR.size()) : "";

    try {
        // ── Phase 1: Lexical analysis ─────────────────────────────
        Lexer lexer(rulesText);
        auto tokens = lexer.tokenize();

        // ── Phase 2: Syntactic analysis + AST construction ───────
        // move() transfers token vector ownership into the parser,
        // avoiding an unnecessary copy of potentially many tokens
        Parser parser(move(tokens));
        ProgramNode program = parser.parse();

        // ── Phase 3: Load initial state ───────────────────────────
        State initialState = parseInitialState(stateText);

        // ── Phase 4: Fixed-point execution ───────────────────────
        Interpreter interp;
        set<string> activated = interp.run(program, initialState);

        // ── Phase 5: Print execution output ──────────────────────
        // set<string> iterates in ascending lexicographic order,
        // satisfying the spec requirement for alphabetical output
        if (activated.empty()) {
            cout << "(no output)\n";
        } else {
            for (auto& f : activated)
                cout << f << "\n";
        }

        // ── Phase 6: Static analysis ──────────────────────────────
        // Printed AFTER the execution output, as required by the spec
        StaticAnalyzer analyzer;
        analyzer.analyze(program, initialState, hasState);

    } catch (const exception& e) {
        // Lexical and syntactic errors surface as runtime_error.
        // Written to stderr so stdout remains clean.
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
