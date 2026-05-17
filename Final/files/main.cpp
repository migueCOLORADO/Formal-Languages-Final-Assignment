// ================================================================
//  DisasterGuard
//  Sistema de Detección y Respuesta ante Desastres Naturales
//
//  Implementación de un lenguaje de reglas basado en hechos con:
//    - Análisis léxico
//    - Análisis sintáctico LL(1) por descenso recursivo
//    - Construcción de AST
//    - Intérprete con modelo de punto fijo
//    - Análisis estático (conflictos, redundancias, reglas inactivas)
//
//  Gramática LL(1) utilizada:
//    Program  → RuleList
//    RuleList → Rule RuleList | ε
//    Rule     → rule id : if Cond then Action
//    Cond     → Atom Cond'
//    Cond'    → AND Atom Cond' | ε
//    Atom     → id Atom'
//    Atom'    → RelOp value | ε
//    RelOp    → > | < | =
//    Action   → id
//
//  Uso: ./disasterguard <archivo.txt>
//       ./disasterguard < <archivo.txt>
// ================================================================

// Importamos Librerias
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
// MÓDULO 1: TOKENS
// Unidades mínimas de significado producidas por el léxico.
// ================================================================

enum class TokenType {
    RULE,         // palabra clave "rule"
    IF,           // palabra clave "if"
    THEN,         // palabra clave "then"
    AND,          // operador "AND"
    COLON,        // puntuación ":"
    GT,           // operador relacional ">"
    LT,           // operador relacional "<"
    EQ,           // operador relacional "="
    ID,           // identificador: [a-z][a-z0-9_]*
    VALUE,        // entero literal: [0-9]+
    END_OF_FILE   // fin de entrada
};

// Convierte un tipo de token a string legible (para mensajes de error)
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
        case TokenType::ID:          return "identificador";
        case TokenType::VALUE:       return "valor entero";
        case TokenType::END_OF_FILE: return "fin de archivo";
        default:                     return "desconocido";
    }
}

struct Token {
    TokenType type;
    string    lexeme;  // texto original del token
    int       line;    // línea de origen (para mensajes de error)

    Token(TokenType t, string lex, int ln)
        : type(t), lexeme(move(lex)), line(ln) {}
};


// ================================================================
// MÓDULO 2: ANALIZADOR LÉXICO
//
// Transforma la cadena de texto de las reglas en una lista de
// tokens. Ignora espacios en blanco. Distingue palabras clave de
// identificadores. "State" y otras palabras en mayúscula no usadas
// se descartan aquí; el separador "State:" se maneja antes de
// llamar al lexer.
// ================================================================

class Lexer {
    string src;
    size_t pos  = 0;
    int    line = 1;

    // Avanza el cursor sobre espacios y saltos de línea
    void skipWhitespace() {
        while (pos < src.size() && isspace((unsigned char)src[pos])) {
            if (src[pos] == '\n') ++line;
            ++pos;
        }
    }

public:
    explicit Lexer(string source) : src(move(source)) {}

    vector<Token> tokenize() {
        vector<Token> tokens;

        while (true) {
            skipWhitespace();
            if (pos >= src.size()) break;

            int  ln = line;
            char c  = src[pos];

            // ── Puntuación y operadores de un carácter ──────────────
            if (c == ':') { tokens.emplace_back(TokenType::COLON, ":", ln); ++pos; continue; }
            if (c == '>') { tokens.emplace_back(TokenType::GT,    ">", ln); ++pos; continue; }
            if (c == '<') { tokens.emplace_back(TokenType::LT,    "<", ln); ++pos; continue; }
            if (c == '=') { tokens.emplace_back(TokenType::EQ,    "=", ln); ++pos; continue; }

            // ── Literales enteros ────────────────────────────────────
            if (isdigit((unsigned char)c)) {
                string num;
                while (pos < src.size() && isdigit((unsigned char)src[pos]))
                    num += src[pos++];
                tokens.emplace_back(TokenType::VALUE, num, ln);
                continue;
            }

            // ── Identificadores en minúscula y palabras clave ────────
            // Restricción: identificadores empiezan con letra minúscula o '_'
            if (islower((unsigned char)c) || c == '_') {
                string id;
                while (pos < src.size() &&
                       (islower((unsigned char)src[pos]) ||
                        isdigit((unsigned char)src[pos]) ||
                        src[pos] == '_'))
                    id += src[pos++];

                // Clasificar como palabra clave o identificador
                if      (id == "rule") tokens.emplace_back(TokenType::RULE, id, ln);
                else if (id == "if")   tokens.emplace_back(TokenType::IF,   id, ln);
                else if (id == "then") tokens.emplace_back(TokenType::THEN, id, ln);
                else                   tokens.emplace_back(TokenType::ID,   id, ln);
                continue;
            }

            // ── Palabras en mayúscula (AND, State, etc.) ─────────────
            if (isupper((unsigned char)c)) {
                string word;
                while (pos < src.size() && isalpha((unsigned char)src[pos]))
                    word += src[pos++];
                // Solo AND es un token válido en las reglas
                if (word == "AND")
                    tokens.emplace_back(TokenType::AND, word, ln);
                // "State" y otras se ignoran (se manejan como separador de sección)
                continue;
            }

            // Carácter no reconocido — saltamos
            ++pos;
        }

        tokens.emplace_back(TokenType::END_OF_FILE, "", line);
        return tokens;
    }
};


// ================================================================
// MÓDULO 3: AST — Árbol de Sintaxis Abstracta
//
// Jerarquía de nodos:
//   ASTNode (base)
//     ├── FactNode      → condición de hecho activo: id
//     ├── CmpNode       → condición de comparación: id op value
//     ├── AndNode       → conjunción: cond AND cond
//     └── RuleNode      → regla completa: nombre, condición, acción
//
// ProgramNode no hereda de ASTNode por simplicidad; contiene la
// lista de RuleNode.
// ================================================================

struct ASTNode { virtual ~ASTNode() = default; };
using  NodePtr = shared_ptr<ASTNode>;

// Condición: hecho activo — verdadera si 'id' está en el conjunto de hechos
struct FactNode : ASTNode {
    string id;
    explicit FactNode(string id) : id(move(id)) {}
};

// Condición: comparación — verdadera si vars[id] op value
struct CmpNode : ASTNode {
    string id;   // variable a comparar
    string op;   // ">", "<" o "="
    int    value; // valor literal

    CmpNode(string id, string op, int v)
        : id(move(id)), op(move(op)), value(v) {}
};

// Condición: conjunción — verdadera si left Y right son verdaderas
struct AndNode : ASTNode {
    NodePtr left, right;
    AndNode(NodePtr l, NodePtr r) : left(move(l)), right(move(r)) {}
};

// Regla completa
struct RuleNode : ASTNode {
    string  name;    // identificador de la regla
    NodePtr cond;    // árbol de condición
    string  action;  // hecho a activar

    RuleNode(string n, NodePtr c, string a)
        : name(move(n)), cond(move(c)), action(move(a)) {}
};

// Programa: lista ordenada de reglas
struct ProgramNode {
    vector<shared_ptr<RuleNode>> rules;
};


// ================================================================
// MÓDULO 4: PARSER LL(1) — DESCENSO RECURSIVO
//
// Cada función parseX() implementa directamente la producción X
// de la gramática LL(1). No se usa tabla explícita; la selección
// de producción se hace con el símbolo actual (lookahead de 1).
//
// PRIMERO y SIGUIENTE relevantes:
//   PRIMERO(RuleList) = {rule, ε}
//   PRIMERO(Cond)     = {id}
//   PRIMERO(Cond')    = {AND, ε}
//   PRIMERO(Atom')    = {>, <, =, ε}
//   SIGUIENTE(Cond')  = {then}
//   SIGUIENTE(Atom')  = {AND, then}
// ================================================================

class Parser {
    vector<Token> toks;
    size_t pos = 0;

    // ── Utilidades de navegación ─────────────────────────────────

    Token& cur() { return toks[pos]; }

    Token consume() { return toks[pos++]; }

    // Consume el token actual si es del tipo esperado; lanza error si no
    Token expect(TokenType t) {
        if (cur().type != t)
            throw runtime_error(
                "Error sintáctico en línea " + to_string(cur().line) +
                ": se esperaba " + tokenName(t) +
                " pero se encontró '" + cur().lexeme + "'");
        return consume();
    }

    // ── Funciones de parsing ─────────────────────────────────────

    // Program → RuleList
    ProgramNode parseProgram() {
        ProgramNode prog;
        // RuleList → Rule RuleList | ε
        // PRIMERO(Rule) = {rule} → mientras veamos 'rule', parsear regla
        while (cur().type == TokenType::RULE)
            prog.rules.push_back(parseRule());
        expect(TokenType::END_OF_FILE);
        return prog;
    }

    // Rule → rule id : if Cond then Action
    shared_ptr<RuleNode> parseRule() {
        expect(TokenType::RULE);
        string name = expect(TokenType::ID).lexeme;
        expect(TokenType::COLON);
        expect(TokenType::IF);
        NodePtr cond = parseCond();
        expect(TokenType::THEN);
        string action = expect(TokenType::ID).lexeme;
        return make_shared<RuleNode>(name, cond, action);
    }

    // Cond → Atom Cond'
    NodePtr parseCond() {
        NodePtr left = parseAtom();
        return parseCondPrime(left);
    }

    // Cond' → AND Atom Cond' | ε
    // Se construye left-asociativo acumulando AND
    NodePtr parseCondPrime(NodePtr left) {
        if (cur().type == TokenType::AND) {
            consume();  // consume AND
            NodePtr right = parseAtom();
            // Combinar en nodo AND y continuar
            return parseCondPrime(make_shared<AndNode>(left, right));
        }
        // ε: SIGUIENTE(Cond') = {then} → no consumir nada
        return left;
    }

    // Atom  → id Atom'
    // Atom' → RelOp value | ε
    NodePtr parseAtom() {
        string id = expect(TokenType::ID).lexeme;

        // Lookahead: ¿viene un operador relacional?
        // PRIMERO(RelOp) = {>, <, =}
        TokenType next = cur().type;
        if (next == TokenType::GT || next == TokenType::LT || next == TokenType::EQ) {
            string op  = consume().lexeme;
            int    val = stoi(expect(TokenType::VALUE).lexeme);
            return make_shared<CmpNode>(id, op, val);
        }

        // Atom' → ε: es un hecho activo
        // SIGUIENTE(Atom') = {AND, then} → correcto no consumir
        return make_shared<FactNode>(id);
    }

public:
    explicit Parser(vector<Token> t) : toks(move(t)) {}

    ProgramNode parse() { return parseProgram(); }
};


// ================================================================
// MÓDULO 5: INTÉRPRETE — SEMÁNTICA DE PUNTO FIJO
//
// El intérprete evalúa las reglas repetidamente hasta que no se
// añaden nuevos hechos (convergencia al punto fijo).
//
// Estado = { variables: map<string,int>, facts: set<string> }
//
// Evaluación de condiciones:
//   Cmp(x, op, v)  → verdadera si vars[x] op v
//   Fact(x)        → verdadera si x ∈ facts
//   And(c1, c2)    → verdadera si c1 ∧ c2
// ================================================================

struct State {
    map<string, int> vars;   // variables con valores enteros
    set<string>      facts;  // hechos activos
};

class Interpreter {

    // Evalúa recursivamente una condición sobre el estado dado
    bool evalCond(NodePtr cond, const State& s) {
        // Comparación aritmética
        if (auto* c = dynamic_cast<CmpNode*>(cond.get())) {
            auto it = s.vars.find(c->id);
            if (it == s.vars.end()) return false;  // variable no definida
            int v = it->second;
            if (c->op == ">") return v > c->value;
            if (c->op == "<") return v < c->value;
            if (c->op == "=") return v == c->value;
            return false;
        }
        // Hecho activo
        if (auto* f = dynamic_cast<FactNode*>(cond.get()))
            return s.facts.count(f->id) > 0;
        // Conjunción (evaluación en cortocircuito)
        if (auto* a = dynamic_cast<AndNode*>(cond.get()))
            return evalCond(a->left, s) && evalCond(a->right, s);
        return false;
    }

public:
    // Ejecuta el programa y devuelve los hechos NUEVOS activados
    // (excluye los hechos que ya estaban en el estado inicial)
    set<string> run(const ProgramNode& prog, State state) {
        set<string> initialFacts = state.facts;  // guardar estado inicial

        bool changed = true;
        while (changed) {
            changed = false;
            for (auto& rule : prog.rules) {
                // Si la condición es verdadera y el hecho aún no está activo
                if (evalCond(rule->cond, state) &&
                    !state.facts.count(rule->action)) {
                    state.facts.insert(rule->action);
                    changed = true;  // nueva iteración necesaria
                }
            }
        }

        // Retornar solo los hechos activados DURANTE la ejecución
        set<string> newFacts;
        for (auto& f : state.facts)
            if (!initialFacts.count(f))
                newFacts.insert(f);
        return newFacts;
    }
};


// ================================================================
// MÓDULO 6: ANÁLISIS ESTÁTICO
//
// Detecta tres tipos de situaciones sobre la estructura del
// programa, sin ejecutarlo:
//
//   1. CONFLICTO: dos o más reglas producen la misma acción
//      → "Action <id> generated by r1, r2, ..."
//
//   2. REDUNDANCIA: dos reglas tienen condición Y acción idénticas
//      → "Redundant rules: r1, r2"
//
//   3. REGLA POTENCIALMENTE INACTIVA: la condición referencia
//      identificadores que nunca serán accesibles dado el estado
//      inicial y el conjunto de hechos derivables
//      → "Potentially inactive rule: <id>"
// ================================================================

class StaticAnalyzer {

    // Genera una clave canónica de una condición para comparación de igualdad.
    // Los AND se normalizan ordenando sus partes para evitar falsos negativos
    // por diferente asociatividad (a AND b vs b AND a).
    string condKey(NodePtr c) {
        if (!c) return "";
        if (auto* cmp = dynamic_cast<CmpNode*>(c.get()))
            return cmp->id + cmp->op + to_string(cmp->value);
        if (auto* f = dynamic_cast<FactNode*>(c.get()))
            return "~" + f->id;
        if (dynamic_cast<AndNode*>(c.get())) {
            vector<string> parts;
            collectParts(c, parts);
            sort(parts.begin(), parts.end());
            string key;
            for (auto& p : parts) key += p + "&";
            return key;
        }
        return "";
    }

    // Descompone un árbol AND en sus hojas individuales
    void collectParts(NodePtr c, vector<string>& parts) {
        if (auto* a = dynamic_cast<AndNode*>(c.get())) {
            collectParts(a->left,  parts);
            collectParts(a->right, parts);
        } else {
            parts.push_back(condKey(c));
        }
    }

    // Recolecta todos los identificadores referenciados en una condición
    void collectIds(NodePtr c, set<string>& ids) {
        if (auto* cmp = dynamic_cast<CmpNode*>(c.get()))
            ids.insert(cmp->id);
        else if (auto* f = dynamic_cast<FactNode*>(c.get()))
            ids.insert(f->id);
        else if (auto* a = dynamic_cast<AndNode*>(c.get())) {
            collectIds(a->left,  ids);
            collectIds(a->right, ids);
        }
    }

public:
    void analyze(const ProgramNode& prog, const State& init) {

        // ── 1. CONFLICTOS ────────────────────────────────────────────
        // Agrupar reglas por acción; si una acción tiene >1 regla → conflicto
        map<string, vector<string>> actionToRules;
        for (auto& r : prog.rules)
            actionToRules[r->action].push_back(r->name);

        for (auto& [action, rules] : actionToRules) {
            if (rules.size() > 1) {
                cout << "Action " << action << " generated by ";
                for (size_t i = 0; i < rules.size(); ++i) {
                    if (i) cout << ", ";
                    cout << rules[i];
                }
                cout << "\n";
            }
        }

        // ── 2. REDUNDANCIAS ──────────────────────────────────────────
        // Dos reglas son redundantes si tienen la misma (condición, acción)
        map<string, vector<string>> sigToRules;
        for (auto& r : prog.rules) {
            string sig = condKey(r->cond) + "=>" + r->action;
            sigToRules[sig].push_back(r->name);
        }

        for (auto& [sig, rules] : sigToRules) {
            if (rules.size() > 1) {
                cout << "Redundant rules: ";
                for (size_t i = 0; i < rules.size(); ++i) {
                    if (i) cout << ", ";
                    cout << rules[i];
                }
                cout << "\n";
            }
        }

        // ── 3. REGLAS POTENCIALMENTE INACTIVAS ───────────────────────
        // Calculamos el conjunto de identificadores "alcanzables":
        // empezamos con las variables y hechos del estado inicial,
        // y propagamos hacia adelante las acciones de reglas cuyas
        // condiciones son satisfacibles con los identificadores alcanzables.
        set<string> reachable;
        for (auto& [k, _] : init.vars)  reachable.insert(k);
        for (auto& f       : init.facts) reachable.insert(f);

        bool changed = true;
        while (changed) {
            changed = false;
            for (auto& r : prog.rules) {
                // ¿Todos los ids de la condición son alcanzables?
                set<string> needed;
                collectIds(r->cond, needed);
                bool allOk = true;
                for (auto& id : needed)
                    if (!reachable.count(id)) { allOk = false; break; }
                // Si sí, la acción también es alcanzable
                if (allOk && !reachable.count(r->action)) {
                    reachable.insert(r->action);
                    changed = true;
                }
            }
        }

        // Una regla es potencialmente inactiva si algún id de su
        // condición nunca es alcanzable
        for (auto& r : prog.rules) {
            set<string> needed;
            collectIds(r->cond, needed);
            for (auto& id : needed) {
                if (!reachable.count(id)) {
                    cout << "Potentially inactive rule: " << r->name << "\n";
                    break;
                }
            }
        }
    }
};


// ================================================================
// MÓDULO 7: PARSER DEL ESTADO INICIAL
//
// El estado inicial tiene dos formatos posibles por línea:
//   id = integer  → asignación de variable
//   id            → hecho activo
//
// Se lee como texto plano sin pasar por el lexer principal.
// ================================================================

State parseInitialState(const string& section) {
    State s;
    istringstream ss(section);
    string line;

    while (getline(ss, line)) {
        // Eliminar espacios al inicio y al final
        auto start = line.find_first_not_of(" \t\r");
        if (start == string::npos) continue;
        line = line.substr(start);
        auto end = line.find_last_not_of(" \t\r");
        if (end != string::npos) line = line.substr(0, end + 1);
        if (line.empty()) continue;

        // ¿Contiene '='? → asignación de variable
        auto eq = line.find('=');
        if (eq != string::npos) {
            // Extraer id (antes del '=')
            auto idEnd = line.find_last_not_of(" \t", eq - 1);
            string id  = (idEnd != string::npos) ? line.substr(0, idEnd + 1) : "";

            // Extraer valor (después del '=')
            auto valStart = line.find_first_not_of(" \t", eq + 1);
            string valStr = (valStart != string::npos) ? line.substr(valStart) : "";

            if (!id.empty() && !valStr.empty()) {
                try { s.vars[id] = stoi(valStr); }
                catch (...) { /* ignorar líneas malformadas */ }
            }
        } else {
            // Solo un identificador → hecho activo
            s.facts.insert(line);
        }
    }

    return s;
}


// ================================================================
// MÓDULO 8: PUNTO DE ENTRADA PRINCIPAL
//
// Flujo:
//   1. Leer entrada (archivo o stdin)
//   2. Separar sección de reglas y sección de estado en "State:"
//   3. Análisis léxico → tokens
//   4. Análisis sintáctico → AST
//   5. Parsear estado inicial
//   6. Ejecutar intérprete → hechos activados
//   7. Imprimir resultados
//   8. Ejecutar análisis estático
// ================================================================

int main(int argc, char* argv[]) {
    // ── Leer entrada ─────────────────────────────────────────────
    string input;
    if (argc > 1) {
        ifstream file(argv[1]);
        if (!file) {
            cerr << "Error: no se puede abrir el archivo '" << argv[1] << "'\n";
            return 1;
        }
        input.assign(istreambuf_iterator<char>(file), {});
    } else {
        input.assign(istreambuf_iterator<char>(cin), {});
    }

    // ── Separar sección de reglas y estado ───────────────────────
    // El separador es la línea "State:"
    const string SEPARATOR = "State:";
    auto sepPos = input.find(SEPARATOR);

    string rulesText = (sepPos != string::npos)
                       ? input.substr(0, sepPos)
                       : input;
    string stateText = (sepPos != string::npos)
                       ? input.substr(sepPos + SEPARATOR.size())
                       : "";

    try {
        // ── 1. Análisis léxico ────────────────────────────────────
        Lexer lexer(rulesText);
        auto tokens = lexer.tokenize();

        // ── 2. Análisis sintáctico + AST ──────────────────────────
        Parser parser(move(tokens));
        ProgramNode program = parser.parse();

        // ── 3. Estado inicial ─────────────────────────────────────
        State initialState = parseInitialState(stateText);

        // ── 4. Ejecución (intérprete) ─────────────────────────────
        Interpreter interp;
        set<string> activated = interp.run(program, initialState);

        // ── 5. Imprimir hechos activados ──────────────────────────
        // Los hechos ya vienen en orden alfabético (set<string>)
        if (activated.empty()) {
            cout << "(no output)\n";
        } else {
            for (auto& f : activated)
                cout << f << "\n";
        }

        // ── 6. Análisis estático ──────────────────────────────────
        StaticAnalyzer analyzer;
        analyzer.analyze(program, initialState);

    } catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
