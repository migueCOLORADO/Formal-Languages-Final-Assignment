
//  Formal SIATA DisasterGuard — Natural Disaster Detection Rule-Based Language
//
//  Full compiler pipeline: Lexer -> LL(1) Parser -> AST ->
//  Fixed-point Interpreter -> Static Analyzer
//
//  LL(1) grammar (left recursion from spec eliminated):
//    Program  -> RuleList
//    RuleList -> Rule RuleList | e
//    Rule     -> rule id : if Cond then id
//    Cond     -> Atom Cond'
//    Cond'    -> AND Atom Cond' | e      (FOLLOW = {then})
//    Atom     -> id Atom'
//    Atom'    -> RelOp value | e         (FOLLOW = {AND, then})
//    RelOp    -> > | < | =
//// By | Miguel Angel Colorado Castano | Sebastian Ibarra Prada | Juan Diego Munoz Buitrago
// Professor | Cesar Guerra Villa

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

// ── MODULE 1: TOKENS ─────────────────────────────────────────────

enum class TokenType {
    RULE, IF, THEN, AND,
    COLON, GT, LT, EQ,
    ID, VALUE, END_OF_FILE
};

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
    TokenType type; string lexeme; int line;
    Token(TokenType t, string l, int ln) : type(t), lexeme(move(l)), line(ln) {}
};

// ── MODULE 2: LEXER ───────────────────────────────────────────────
// Longest-match scan: reads full words before classifying as keyword or ID.
// Uppercase words other than AND are silently discarded (e.g. "State:").

class Lexer {
    string src; size_t pos = 0; int line = 1;

    void skipWS() {
        while (pos < src.size() && isspace((unsigned char)src[pos])) {
            if (src[pos] == '\n') ++line;
            ++pos;
        }
    }

public:
    explicit Lexer(string s) : src(move(s)) {}

    vector<Token> tokenize() {
        vector<Token> toks;
        while (true) {
            skipWS();
            if (pos >= src.size()) break;
            int ln = line; char c = src[pos];

            if (c == ':') { toks.emplace_back(TokenType::COLON, ":", ln); ++pos; continue; }
            if (c == '>') { toks.emplace_back(TokenType::GT,    ">", ln); ++pos; continue; }
            if (c == '<') { toks.emplace_back(TokenType::LT,    "<", ln); ++pos; continue; }
            if (c == '=') { toks.emplace_back(TokenType::EQ,    "=", ln); ++pos; continue; }

            if (isdigit((unsigned char)c)) {
                string num;
                while (pos < src.size() && isdigit((unsigned char)src[pos])) num += src[pos++];
                toks.emplace_back(TokenType::VALUE, num, ln);
                continue;
            }

            if (islower((unsigned char)c) || c == '_') {
                string word;
                while (pos < src.size() &&
                       (islower((unsigned char)src[pos]) ||
                        isdigit((unsigned char)src[pos]) || src[pos] == '_'))
                    word += src[pos++];
                if      (word == "rule") toks.emplace_back(TokenType::RULE, word, ln);
                else if (word == "if")   toks.emplace_back(TokenType::IF,   word, ln);
                else if (word == "then") toks.emplace_back(TokenType::THEN, word, ln);
                else                     toks.emplace_back(TokenType::ID,   word, ln);
                continue;
            }

            if (isupper((unsigned char)c)) {
                string word;
                while (pos < src.size() && isalpha((unsigned char)src[pos])) word += src[pos++];
                if (word == "AND") toks.emplace_back(TokenType::AND, word, ln);
                continue;
            }
            ++pos;
        }
        toks.emplace_back(TokenType::END_OF_FILE, "", line);
        return toks;
    }
};

// ── MODULE 3: AST ─────────────────────────────────────────────────
// NodePtr (shared_ptr<ASTNode>) is the universal handle for condition nodes.
// Virtual destructor on ASTNode is mandatory for correct cleanup via base pointer.

struct ASTNode { virtual ~ASTNode() = default; };
using  NodePtr = shared_ptr<ASTNode>;

// Condition: true when 'id' is in State::facts
struct FactNode : ASTNode {
    string id;
    explicit FactNode(string id) : id(move(id)) {}
};

// Condition: true when vars[id] op value holds
struct CmpNode : ASTNode {
    string id, op; int value;
    CmpNode(string id, string op, int v) : id(move(id)), op(move(op)), value(v) {}
};

// Condition: true when both children hold. Always left-associative.
struct AndNode : ASTNode {
    NodePtr left, right;
    AndNode(NodePtr l, NodePtr r) : left(move(l)), right(move(r)) {}
};

// RuleNode does not need to inherit ASTNode — it is always held as shared_ptr<RuleNode>
struct RuleNode {
    string name; NodePtr cond; string action;
    RuleNode(string n, NodePtr c, string a) : name(move(n)), cond(move(c)), action(move(a)) {}
};

struct ProgramNode { vector<shared_ptr<RuleNode>> rules; };

// ── MODULE 4: LL(1) PARSER ────────────────────────────────────────
// One method per grammar production. Production choice via 1-token lookahead.
//
// Why LL(1)?  The spec grammar Cond -> Cond AND Cond has direct left recursion.
// Exercise 4.16 (Aho et al.) proves no left-recursive grammar is LL(1).
// The transformation Cond -> Atom Cond' / Cond' -> AND Atom Cond' | e removes it.
//
// FIRST(Cond') = {AND, e}    FOLLOW(Cond') = {then}
// FIRST(Atom') = {>,<,=, e}  FOLLOW(Atom') = {AND, then}

class Parser {
    vector<Token> toks; size_t pos = 0;

    Token& cur()     { return toks[pos]; }
    Token  consume() { return toks[pos++]; }

    Token expect(TokenType t) {
        if (cur().type != t)
            throw runtime_error(
                "Syntax error at line " + to_string(cur().line) +
                ": expected " + tokenName(t) + " but found '" + cur().lexeme + "'");
        return consume();
    }

    // Cond -> Atom Cond'
    NodePtr parseCond() { return parseCondPrime(parseAtom()); }

    // Cond' -> AND Atom Cond' | e
    // Accumulator pattern: each AND extends the tree left-associatively,
    // matching exactly the Cond' production without left recursion.
    NodePtr parseCondPrime(NodePtr left) {
        if (cur().type == TokenType::AND) {
            consume();
            return parseCondPrime(make_shared<AndNode>(left, parseAtom()));
        }
        return left;  // e: lookahead in FOLLOW(Cond') = {then}
    }

    // Atom -> id Atom'    Atom' -> RelOp value | e
    // One lookahead after id decides: {>,<,=} -> CmpNode, else -> FactNode
    NodePtr parseAtom() {
        string id = expect(TokenType::ID).lexeme;
        TokenType next = cur().type;
        if (next == TokenType::GT || next == TokenType::LT || next == TokenType::EQ) {
            string op = consume().lexeme;
            return make_shared<CmpNode>(id, op, stoi(expect(TokenType::VALUE).lexeme));
        }
        return make_shared<FactNode>(id);
    }

    shared_ptr<RuleNode> parseRule() {
        expect(TokenType::RULE);
        string name = expect(TokenType::ID).lexeme;
        expect(TokenType::COLON);
        expect(TokenType::IF);
        NodePtr cond = parseCond();
        expect(TokenType::THEN);
        return make_shared<RuleNode>(name, cond, expect(TokenType::ID).lexeme);
    }

    ProgramNode parseProgram() {
        ProgramNode prog;
        while (cur().type == TokenType::RULE) prog.rules.push_back(parseRule());
        expect(TokenType::END_OF_FILE);
        return prog;
    }

public:
    explicit Parser(vector<Token> t) : toks(move(t)) {}
    ProgramNode parse() { return parseProgram(); }
};

// ── MODULE 5: INTERPRETER ────────────────────────────────────────
// Fixed-point execution: S0 = initial state
//   Si+1 = Si U {a | rule(cond,a): [[cond]]_Si = true}
//   S*   = Sn when Sn = Sn-1  (convergence; terminates because facts only grow)

struct State {
    map<string, int> vars;  // immutable variable assignments
    set<string>      facts; // grows monotonically during execution
};

class Interpreter {

    // Recursive evaluation mirrors the AST structure:
    // dynamic_cast identifies the concrete type at runtime.
    bool evalCond(NodePtr cond, const State& s) {
        if (auto* c = dynamic_cast<CmpNode*>(cond.get())) {
            auto it = s.vars.find(c->id);
            if (it == s.vars.end()) return false;
            if (c->op == ">") return it->second > c->value;
            if (c->op == "<") return it->second < c->value;
            if (c->op == "=") return it->second == c->value;
        }
        if (auto* f = dynamic_cast<FactNode*>(cond.get()))
            return s.facts.count(f->id) > 0;
        if (auto* a = dynamic_cast<AndNode*>(cond.get()))
            return evalCond(a->left, s) && evalCond(a->right, s); // short-circuit
        return false;
    }

public:
    // Returns only facts activated DURING execution (initial facts excluded).
    set<string> run(const ProgramNode& prog, State state) {
        set<string> initial = state.facts;
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto& rule : prog.rules) {
                if (evalCond(rule->cond, state) && !state.facts.count(rule->action)) {
                    state.facts.insert(rule->action);
                    changed = true;
                }
            }
        }
        // set<string> iteration is in ascending lexicographic order — spec requirement met
        set<string> result;
        for (auto& f : state.facts)
            if (!initial.count(f)) result.insert(f);
        return result;
    }
};

// ── MODULE 6: STATIC ANALYZER ────────────────────────────────────
// Three analyses on the AST without executing the program.
//
// Order matters: REDUNDANCY computed first so redundant pairs are excluded
// from CONFLICT detection (they are mutually exclusive categories).
//
// INACTIVE: forward propagation from init state. CmpNode is reachable if
// its variable is declared in vars (value not checked — conservative approach).
// FactNode is reachable only if in the activatable set.
// hasState=false means no State: section -> all CmpNodes treated as reachable.

class StaticAnalyzer {

    // Canonical key for a condition. AND operands sorted so (a AND b)==(b AND a).
    string condKey(NodePtr c) {
        if (!c) return "";
        if (auto* cmp = dynamic_cast<CmpNode*>(c.get()))
            return cmp->id + cmp->op + to_string(cmp->value);
        if (auto* f = dynamic_cast<FactNode*>(c.get()))
            return "~" + f->id;
        if (dynamic_cast<AndNode*>(c.get())) {
            vector<string> parts; collectParts(c, parts);
            sort(parts.begin(), parts.end());
            string k; for (auto& p : parts) k += p + "&"; return k;
        }
        return "";
    }

    void collectParts(NodePtr c, vector<string>& parts) {
        if (auto* a = dynamic_cast<AndNode*>(c.get()))
            { collectParts(a->left, parts); collectParts(a->right, parts); }
        else parts.push_back(condKey(c));
    }

    bool reachable(NodePtr c, const State& init,
                   const set<string>& activatable, bool hasState) {
        if (auto* cmp = dynamic_cast<CmpNode*>(c.get())) {
            if (!hasState) return true;           // no state info -> conservative
            return init.vars.count(cmp->id) > 0; // variable must be declared
        }
        if (auto* f = dynamic_cast<FactNode*>(c.get()))
            return activatable.count(f->id) > 0;
        if (auto* a = dynamic_cast<AndNode*>(c.get()))
            return reachable(a->left, init, activatable, hasState) &&
                   reachable(a->right, init, activatable, hasState);
        return false;
    }

public:
    void analyze(const ProgramNode& prog, const State& init, bool hasState) {

        // ── 1. Redundancy ─────────────────────────────────────────
        map<string, vector<string>> sigMap;
        for (auto& r : prog.rules)
            sigMap[condKey(r->cond) + "=>" + r->action].push_back(r->name);

        set<string> redundant;
        for (auto& [sig, names] : sigMap) {
            if (names.size() > 1) {
                for (auto& n : names) redundant.insert(n);
                cout << "Redundant rules: ";
                for (size_t i = 0; i < names.size(); ++i) { if (i) cout << ", "; cout << names[i]; }
                cout << "\n";
            }
        }

        // ── 2. Conflict (redundant groups excluded) ───────────────
        map<string, vector<string>> actMap;
        for (auto& r : prog.rules) actMap[r->action].push_back(r->name);

        for (auto& [action, names] : actMap) {
            if (names.size() < 2) continue;
            bool allRedundant = true;
            for (auto& n : names) if (!redundant.count(n)) { allRedundant = false; break; }
            if (!allRedundant) {
                cout << "Action " << action << " generated by ";
                for (size_t i = 0; i < names.size(); ++i) { if (i) cout << ", "; cout << names[i]; }
                cout << "\n";
            }
        }

        // ── 3. Potentially inactive (forward propagation) ─────────
        set<string> activatable = init.facts;
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto& r : prog.rules) {
                if (reachable(r->cond, init, activatable, hasState) &&
                    !activatable.count(r->action)) {
                    activatable.insert(r->action);
                    changed = true;
                }
            }
        }
        for (auto& r : prog.rules)
            if (!reachable(r->cond, init, activatable, hasState))
                cout << "Potentially inactive rule: " << r->name << "\n";
    }
};

// ── MODULE 7: INITIAL STATE PARSER ───────────────────────────────
// Plain text parse of the State: section.
// Each line: "id = integer" -> var assignment, "id" alone -> active fact.

State parseInitialState(const string& text) {
    State s; istringstream ss(text); string line;
    while (getline(ss, line)) {
        auto a = line.find_first_not_of(" \t\r");
        if (a == string::npos) continue;
        line = line.substr(a);
        auto b = line.find_last_not_of(" \t\r");
        if (b != string::npos) line = line.substr(0, b + 1);
        if (line.empty()) continue;

        auto eq = line.find('=');
        if (eq != string::npos) {
            auto   ie = line.find_last_not_of(" \t", eq - 1);
            string id = (ie != string::npos) ? line.substr(0, ie + 1) : "";
            auto   vs = line.find_first_not_of(" \t", eq + 1);
            string vl = (vs != string::npos) ? line.substr(vs) : "";
            if (!id.empty() && !vl.empty())
                try { s.vars[id] = stoi(vl); } catch (...) {}
        } else {
            s.facts.insert(line);
        }
    }
    return s;
}

// ── MODULE 8: MAIN ────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    string input;
    if (argc > 1) {
        ifstream file(argv[1]);
        if (!file) { cerr << "Error: cannot open '" << argv[1] << "'\n"; return 1; }
        input.assign(istreambuf_iterator<char>(file), {});
    } else {
        input.assign(istreambuf_iterator<char>(cin), {});
    }

    const string SEP = "State:";
    auto sepPos = input.find(SEP);
    bool   hasState  = (sepPos != string::npos);
    string rulesText = hasState ? input.substr(0, sepPos) : input;
    string stateText = hasState ? input.substr(sepPos + SEP.size()) : "";

    try {
        Parser parser(Lexer(rulesText).tokenize());
        ProgramNode program = parser.parse();
        State state = parseInitialState(stateText);

        Interpreter interp;
        set<string> activated = interp.run(program, state);

        if (activated.empty() && hasState) cout << "(no output)\n";
        else for (auto& f : activated) cout << f << "\n";

        StaticAnalyzer().analyze(program, state, hasState);

    } catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n"; return 1;
    }
    return 0;
}
