#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cctype>

using namespace std;

// ======================= ТОКЕНЫ =======================

enum TokenType { KEYWORD, TYPE, IDENTIFIER, NUMBER, STRING, CHAR_LIT, OPERATOR, DELIMITER, PREPROCESSOR, UNKNOWN };

string typeName(TokenType t) {
    switch (t) {
        case KEYWORD:      return "Ключевое слово";
        case TYPE:         return "Тип данных";
        case IDENTIFIER:   return "Идентификатор";
        case NUMBER:       return "Число";
        case STRING:       return "Строка";
        case CHAR_LIT:     return "Символ";
        case OPERATOR:     return "Оператор";
        case DELIMITER:    return "Разделитель";
        case PREPROCESSOR: return "Директива";
        default:           return "Неизвестно";
    }
}

struct Token {
    TokenType type;
    string value;
    int line;
    int col;
};

// ======================= ОШИБКИ =======================

struct Error {
    int line;
    int col;
    string kind;   // Лексическая / Синтаксическая / Семантическая
    string msg;
};

vector<Error> errors;

void addError(int line, int col, string kind, string msg) {
    errors.push_back({line, col, kind, msg});
}

// ======================= СЛОВАРИ =======================

set<string> types = {"int", "float", "double", "char", "bool", "void", "string",
                     "auto", "long", "short", "unsigned", "signed", "vector", "size_t"};

set<string> keywords = {"if", "else", "for", "while", "do", "return", "break", "continue",
                        "switch", "case", "default", "const", "static", "true", "false",
                        "nullptr", "using", "namespace", "struct", "class", "new", "delete",
                        "sizeof", "public", "private"};

vector<string> ops3 = {"<<=", ">>="};
vector<string> ops2 = {"==", "!=", "<=", ">=", "&&", "||", "++", "--", "+=", "-=",
                       "*=", "/=", "%=", "<<", ">>", "->", "::"};
string ops1 = "+-*/%=<>!&|^~?:.";
string delimiters = "(){}[];,";

// ======================= ЛЕКСЕР =======================
// Идёт по тексту посимвольно и возвращает массив токенов

vector<Token> lexer(const string& s) {
    vector<Token> tokens;
    size_t i = 0;
    int line = 1, col = 1;

    // перейти к следующему символу
    auto next = [&]() {
        if (s[i] == '\n') { line++; col = 1; }
        else col++;
        i++;
    };
    // посмотреть символ впереди, не сдвигаясь
    auto peek = [&](size_t k) -> char {
        return (i + k < s.size()) ? s[i + k] : '\0';
    };

    while (i < s.size()) {
        char c = s[i];
        int startLine = line, startCol = col;

        // 1. Пробелы и переводы строк пропускаем
        if (isspace((unsigned char)c)) { next(); continue; }

        // 2. Однострочный комментарий //
        if (c == '/' && peek(1) == '/') {
            while (i < s.size() && s[i] != '\n') next();
            continue;
        }

        // 3. Многострочный комментарий /* */
        if (c == '/' && peek(1) == '*') {
            next(); next();
            bool closed = false;
            while (i < s.size()) {
                if (s[i] == '*' && peek(1) == '/') { next(); next(); closed = true; break; }
                next();
            }
            if (!closed) addError(startLine, startCol, "Лексическая", "незакрытый комментарий /* */");
            continue;
        }

        // 4. Директива препроцессора (#include ...) — вся строка один токен
        if (c == '#') {
            string v;
            while (i < s.size() && s[i] != '\n') { v += s[i]; next(); }
            tokens.push_back({PREPROCESSOR, v, startLine, startCol});
            continue;
        }

        // 5. Слово: ключевое слово, тип или идентификатор
        if (isalpha((unsigned char)c) || c == '_') {
            string v;
            while (i < s.size() && (isalnum((unsigned char)s[i]) || s[i] == '_')) { v += s[i]; next(); }
            TokenType t = IDENTIFIER;
            if (types.count(v)) t = TYPE;
            else if (keywords.count(v)) t = KEYWORD;
            tokens.push_back({t, v, startLine, startCol});
            continue;
        }

        // 6. Число
        if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)peek(1)))) {
            string v;
            while (i < s.size() && (isalnum((unsigned char)s[i]) || s[i] == '.')) { v += s[i]; next(); }

            // проверяем, что число записано правильно: цифры, максимум одна точка, суффикс f/l/u
            string body = v;
            if (!body.empty() && string("fFlLuU").find(body.back()) != string::npos) body.pop_back();
            int dots = 0;
            bool ok = !body.empty();
            for (char ch : body) {
                if (ch == '.') dots++;
                else if (!isdigit((unsigned char)ch)) ok = false;
            }
            if (!ok || dots > 1) addError(startLine, startCol, "Лексическая", "неверная запись числа '" + v + "'");
            tokens.push_back({NUMBER, v, startLine, startCol});
            continue;
        }

        // 7. Строка "..." или символ '.'
        if (c == '"' || c == '\'') {
            char quote = c;
            string v(1, c);
            next();
            bool closed = false;
            while (i < s.size() && s[i] != '\n') {
                if (s[i] == '\\' && i + 1 < s.size()) {   // экранирование \n, \" и т.п.
                    v += s[i]; next();
                    v += s[i]; next();
                    continue;
                }
                v += s[i];
                if (s[i] == quote) { next(); closed = true; break; }
                next();
            }
            if (!closed) {
                if (quote == '"') addError(startLine, startCol, "Лексическая", "незакрытая строка");
                else addError(startLine, startCol, "Лексическая", "незакрытый символьный литерал");
            }
            tokens.push_back({quote == '"' ? STRING : CHAR_LIT, v, startLine, startCol});
            continue;
        }

        // 8. Операторы (сначала самые длинные)
        bool found = false;
        for (string op : ops3) {
            if (s.compare(i, 3, op) == 0) {
                next(); next(); next();
                tokens.push_back({OPERATOR, op, startLine, startCol});
                found = true;
                break;
            }
        }
        if (found) continue;
        for (string op : ops2) {
            if (s.compare(i, 2, op) == 0) {
                next(); next();
                tokens.push_back({OPERATOR, op, startLine, startCol});
                found = true;
                break;
            }
        }
        if (found) continue;
        if (ops1.find(c) != string::npos) {
            next();
            tokens.push_back({OPERATOR, string(1, c), startLine, startCol});
            continue;
        }

        // 9. Разделители: скобки, ; и ,
        if (delimiters.find(c) != string::npos) {
            next();
            tokens.push_back({DELIMITER, string(1, c), startLine, startCol});
            continue;
        }

        // 10. Всё остальное — недопустимый символ
        string v;
        if ((unsigned char)c >= 128) {   // русская буква и т.п. занимает несколько байт
            while (i < s.size() && (unsigned char)s[i] >= 128) { v += s[i]; next(); }
        } else {
            v += c; next();
        }
        addError(startLine, startCol, "Лексическая", "недопустимый символ '" + v + "'");
        tokens.push_back({UNKNOWN, v, startLine, startCol});
    }
    return tokens;
}

// ======================= ПРОВЕРКА СКОБОК =======================

void checkBrackets(const vector<Token>& t) {
    vector<Token> stack;
    for (const Token& tk : t) {
        if (tk.type != DELIMITER) continue;
        string v = tk.value;

        if (v == "(" || v == "[" || v == "{") {
            stack.push_back(tk);
        } else if (v == ")" || v == "]" || v == "}") {
            if (stack.empty()) {
                addError(tk.line, tk.col, "Синтаксическая", "лишняя закрывающая скобка '" + v + "'");
                continue;
            }
            string open = stack.back().value;
            string need = (open == "(") ? ")" : (open == "[") ? "]" : "}";
            if (v != need) {
                addError(tk.line, tk.col, "Синтаксическая",
                         "ожидалась '" + need + "' (для '" + open + "' из строки " +
                         to_string(stack.back().line) + "), а встретилась '" + v + "'");
            }
            stack.pop_back();
        }
    }
    for (const Token& tk : stack) {
        addError(tk.line, tk.col, "Синтаксическая", "скобка '" + tk.value + "' не закрыта");
    }
}

// ======================= ПРОВЕРКА ПЕРЕМЕННЫХ =======================

struct Var {
    bool initialized;
    bool reported;   // чтобы не сообщать об одной переменной много раз
};

vector<map<string, Var>> scopes;   // области видимости: { ... }
set<string> functions;
set<string> userTypes;             // имена struct / class
set<string> knownNames = {"std", "cout", "cin", "cerr", "endl", "printf", "scanf", "puts",
                          "getline", "to_string", "stoi", "stod", "abs", "max", "min",
                          "sqrt", "pow", "swap", "exit", "rand", "srand", "time", "NULL", "main"};

Var* findVar(const string& name) {
    for (int k = (int)scopes.size() - 1; k >= 0; k--) {
        auto it = scopes[k].find(name);
        if (it != scopes[k].end()) return &it->second;
    }
    return nullptr;
}

void checkVariables(const vector<Token>& t) {
    int n = t.size();
    scopes.clear();
    scopes.push_back({});                // глобальная область

    int parenDepth = 0, braceDepth = 0;
    vector<int> headerParens;            // скобки заголовков for(...) и функций f(...)
    bool nextParenIsHeader = false;
    bool reuseNextBrace = false;         // { после заголовка не создаёт новую область
    int popAtSemicolon = 0;              // for без фигурных скобок

    bool declMode = false;               // сейчас идёт объявление "int a = 1, b, c;"
    int declParen = 0, declBrace = 0;
    bool declClassType = false;

    vector<Var*> pendingInit;            // переменные слева от =, станут инициализированы после ;

    auto val = [&](int k) -> string { return (k >= 0 && k < n) ? t[k].value : ""; };
    auto isTypeTok = [&](int k) {
        return k < n && (t[k].type == TYPE || (t[k].type == IDENTIFIER && userTypes.count(t[k].value)));
    };
    auto applyPending = [&]() {
        for (Var* v : pendingInit) v->initialized = true;
        pendingInit.clear();
    };
    auto popScope = [&]() {
        applyPending();
        if (scopes.size() > 1) scopes.pop_back();
    };

    // объявить переменную с именем t[k]; возвращает индекс токена после имени
    auto declare = [&](int k) -> int {
        string name = t[k].value;
        int r = k + 1;
        bool isArray = false;
        while (val(r) == "[") {          // пропускаем размер массива [10]
            isArray = true;
            int d = 0;
            do {
                if (val(r) == "[") d++;
                else if (val(r) == "]") d--;
                r++;
            } while (r < n && d > 0);
        }
        bool isParam = parenDepth > 0 && (val(r) == "," || val(r) == ")");
        bool init = declClassType || isArray || isParam ||
                    val(r) == "=" || val(r) == "{" || val(r) == "(";

        if (scopes.back().count(name))
            addError(t[k].line, t[k].col, "Семантическая", "повторное объявление переменной '" + name + "'");
        scopes.back()[name] = {init, false};
        return r;
    };

    for (int p = 0; p < n; p++) {
        const Token& tk = t[p];
        string v = tk.value;

        // struct Point / class Point — запоминаем новое имя типа
        if (tk.type == KEYWORD && (v == "struct" || v == "class") && p + 1 < n && t[p + 1].type == IDENTIFIER) {
            userTypes.insert(t[p + 1].value);
            p++;
            continue;
        }

        // for (...) — скобка будет заголовком со своей областью видимости
        if (tk.type == KEYWORD && v == "for") {
            nextParenIsHeader = true;
            continue;
        }

        // ----- разделители -----
        if (tk.type == DELIMITER) {
            if (v == "(") {
                parenDepth++;
                if (nextParenIsHeader) {
                    scopes.push_back({});
                    headerParens.push_back(parenDepth);
                    nextParenIsHeader = false;
                }
            } else if (v == ")") {
                if (declMode && parenDepth == declParen) declMode = false;
                if (!headerParens.empty() && headerParens.back() == parenDepth) {
                    headerParens.pop_back();
                    if (val(p + 1) == "{") reuseNextBrace = true;   // тело функции / цикла
                    else if (val(p + 1) == ";") popScope();         // прототип функции
                    else popAtSemicolon++;                          // for без { }
                }
                parenDepth--;
            } else if (v == "{") {
                braceDepth++;
                if (reuseNextBrace) reuseNextBrace = false;
                else scopes.push_back({});
            } else if (v == "}") {
                braceDepth--;
                if (declMode && braceDepth < declBrace) declMode = false;
                popScope();
            } else if (v == ";") {
                applyPending();
                if (declMode && parenDepth == declParen) declMode = false;
                if (popAtSemicolon > 0 && parenDepth == 0) {
                    popAtSemicolon--;
                    popScope();
                }
            } else if (v == ",") {
                // int a = 1, b; — после запятой объявляется следующая переменная
                if (declMode && parenDepth == declParen && braceDepth == declBrace &&
                    p + 1 < n && t[p + 1].type == IDENTIFIER && !isTypeTok(p + 1)) {
                    p = declare(p + 1) - 1;
                }
            }
            continue;
        }

        // ----- объявление: тип имя ... -----
        if (isTypeTok(p)) {
            declClassType = (v == "string" || v == "vector" || userTypes.count(v));
            int q = p + 1;
            while (q < n && (isTypeTok(q) || val(q) == "*" || val(q) == "&" || val(q) == "const")) q++;
            if (val(q) == "<") {                         // vector<int>
                int d = 0;
                do {
                    if (val(q) == "<") d++;
                    else if (val(q) == ">") d--;
                    else if (val(q) == ">>") d -= 2;
                    q++;
                } while (q < n && d > 0);
                while (val(q) == "*" || val(q) == "&") q++;
            }
            if (q < n && t[q].type == IDENTIFIER && !isTypeTok(q)) {
                if (val(q + 1) == "(" && scopes.size() == 1) {   // объявление функции
                    functions.insert(t[q].value);
                    nextParenIsHeader = true;
                    p = q;
                    continue;
                }
                declMode = true;
                declParen = parenDepth;
                declBrace = braceDepth;
                p = declare(q) - 1;
            }
            continue;
        }

        // ----- использование идентификатора -----
        if (tk.type == IDENTIFIER) {
            string prev = val(p - 1), next = val(p + 1);

            // obj.field, ptr->field, std::cout, std:: — не проверяем
            if (prev == "." || prev == "->" || prev == "::" || next == "::") continue;

            // вызов функции
            if (next == "(") {
                if (!functions.count(v) && !knownNames.count(v) && !findVar(v))
                    addError(tk.line, tk.col, "Семантическая", "вызов необъявленной функции '" + v + "'");
                continue;
            }

            Var* var = findVar(v);
            if (!var) {
                if (!knownNames.count(v))
                    addError(tk.line, tk.col, "Семантическая", "переменная '" + v + "' не объявлена");
                continue;
            }

            // a = ...  — присваивание, переменная станет инициализированной
            if (next == "=") {
                pendingInit.push_back(var);
                continue;
            }

            // cin >> a  или  scanf("%d", &a)  — значение читается с клавиатуры
            if (prev == ">>" || (prev == "&" && (val(p - 2) == "(" || val(p - 2) == ","))) {
                var->initialized = true;
                continue;
            }

            if (!var->initialized && !var->reported) {
                addError(tk.line, tk.col, "Семантическая", "переменная '" + v + "' используется без инициализации");
                var->reported = true;
            }
        }
    }
}

// ======================= ПРОВЕРКА ; =======================
// Если строка закончилась на имени/числе, а следующая начинается с нового оператора —
// скорее всего, забыли точку с запятой.

void checkSemicolons(const vector<Token>& t) {
    int parenDepth = 0;
    for (size_t i = 1; i < t.size(); i++) {
        const Token& a = t[i - 1];
        const Token& b = t[i];

        if (a.value == "(") parenDepth++;
        if (a.value == ")") parenDepth--;
        if (a.value == "{" || a.value == "}" || a.value == ";") parenDepth = 0;  // круглые скобки не переходят через { } и ;

        // незакрытую строку не проверяем — об этой ошибке уже сообщил лексер
        bool closedString = a.type == STRING && a.value.size() >= 2 && a.value.back() == '"';
        bool endsStatement = a.type == IDENTIFIER || a.type == NUMBER || closedString ||
                             a.type == CHAR_LIT || a.value == "++" || a.value == "--";
        bool startsStatement = b.type == IDENTIFIER || b.type == TYPE || b.type == KEYWORD;

        if (parenDepth == 0 && b.line > a.line && endsStatement && startsStatement) {
            addError(a.line, a.col + (int)a.value.size(), "Синтаксическая",
                     "пропущена ';' после '" + a.value + "'");
        }
    }
}

// ======================= MAIN =======================

int main(int argc, char* argv[]) {
#ifdef _WIN32
    system("chcp 65001 > nul");   // чтобы русский текст нормально выводился в консоли Windows
#endif

    string fileName = (argc > 1) ? argv[1] : "test_ok.cpp";
    ifstream file(fileName);
    if (!file) {
        cout << "Не удалось открыть файл: " << fileName << endl;
        return 1;
    }
    stringstream buffer;
    buffer << file.rdbuf();
    string code = buffer.str();

    // 1. Лексический анализ
    vector<Token> tokens = lexer(code);

    cout << "Файл: " << fileName << "\n\n";
    cout << "========== ТОКЕНЫ (" << tokens.size() << ") ==========\n";
    cout << "Стр:Поз   Значение                 Тип\n";
    for (const Token& tk : tokens) {
        cout << setw(4) << tk.line << ":" << left << setw(5) << tk.col << right << " "
             << left << setw(25) << tk.value << right << typeName(tk.type) << "\n";
    }

    // 2. Синтаксический анализ
    checkBrackets(tokens);
    checkVariables(tokens);
    checkSemicolons(tokens);

    // 3. Вывод ошибок по порядку строк
    stable_sort(errors.begin(), errors.end(), [](const Error& a, const Error& b) {
        if (a.line != b.line) return a.line < b.line;
        return a.col < b.col;
    });

    cout << "\n========== РЕЗУЛЬТАТ ==========\n";
    if (errors.empty()) {
        cout << "Ошибок не найдено. Код корректен.\n";
    } else {
        cout << "Найдено ошибок: " << errors.size() << "\n";
        for (const Error& e : errors) {
            cout << "[" << e.kind << "] строка " << e.line << ", позиция " << e.col
                 << ": " << e.msg << "\n";
        }
    }
    return errors.empty() ? 0 : 1;
}
