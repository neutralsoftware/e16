#include <parser.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
bool isSpecialRegister(const std::string &val) {
    return val == "pc" || val == "sp" || val == "fp" || val == "fl" ||
           val == "dp" || val == "ivt";
}

bool isGeneralRegister(const std::string &val) {
    if (!val.starts_with("r") || val.length() < 2 || val.length() > 3) {
        return false;
    }

    for (size_t i = 1; i < val.length(); i++) {
        if (!std::isdigit(static_cast<unsigned char>(val[i]))) {
            return false;
        }
    }

    int registerNum = std::stoi(val.substr(1));
    return registerNum >= 0 && registerNum < 16;
}

bool isIdentifier(const std::string &val) {
    if (val.empty()) {
        return false;
    }

    unsigned char first = static_cast<unsigned char>(val[0]);
    if (!std::isalpha(first) && val[0] != '_' && val[0] != '.') {
        return false;
    }

    for (char c : val) {
        unsigned char ch = static_cast<unsigned char>(c);
        if (!std::isalnum(ch) && c != '_' && c != '.') {
            return false;
        }
    }

    return true;
}

std::string unquote(const std::string &val) {
    std::string trimmed = utils::trim(val);
    if (trimmed.length() >= 2 &&
        ((trimmed.front() == '"' && trimmed.back() == '"') ||
         (trimmed.front() == '\'' && trimmed.back() == '\''))) {
        return trimmed.substr(1, trimmed.length() - 2);
    }

    return trimmed;
}

std::filesystem::path includeBase(const std::string &path) {
    if (path.empty()) {
        return std::filesystem::current_path();
    }

    std::filesystem::path source(path);
    if (source.has_parent_path()) {
        return source.parent_path();
    }

    return std::filesystem::current_path();
}

std::string readTextFile(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Could not open include file " +
                                 path.string() + ".");
    }

    std::string contents;
    std::string line;
    while (std::getline(file, line)) {
        contents += line;
        contents += '\n';
    }

    return contents;
}

std::unordered_map<std::string, int>
collectParserConstants(const std::vector<std::unique_ptr<Expression>> &expressions) {
    std::unordered_map<std::string, int> constants;

    for (const auto &expression : expressions) {
        if (expression->type != ExpressionType::Directive) {
            continue;
        }

        const auto *directive = dynamic_cast<Directive *>(expression.get());
        if (directive->name != ".const" && directive->name != ".constant") {
            continue;
        }

        if (directive->arguments.size() != 2) {
            continue;
        }

        try {
            int value = 0;
            if (utils::evaluateExpression(directive->arguments[1], constants,
                                          value)) {
                constants[directive->arguments[0]] = value;
            }
        } catch (...) {
        }
    }

    return constants;
}

bool resolveConstExpression(const std::string &text,
                            const std::unordered_map<std::string, int> &constants,
                            int &value) {
    return utils::evaluateExpression(text, constants, value);
}

std::string normalizeNumber(int value) {
    return std::to_string(value);
}

bool isExpressionCandidate(const std::string &value) {
    return value.find_first_of("+-*/%<>&^|~()") != std::string::npos;
}

size_t findTopLevelOffsetOperator(const std::string &val) {
    int parenDepth = 0;
    for (size_t i = 0; i < val.size(); i++) {
        if (val[i] == '(') {
            parenDepth++;
            continue;
        }
        if (val[i] == ')') {
            if (parenDepth > 0) {
                parenDepth--;
            }
            continue;
        }
        if (parenDepth != 0 || (val[i] != '+' && val[i] != '-')) {
            continue;
        }
        if (i == 0) {
            continue;
        }
        char previous = val[i - 1];
        if (previous == '<' || previous == '>' || previous == '&' ||
            previous == '^' || previous == '|' || previous == '*' ||
            previous == '/' || previous == '%' || previous == '+' ||
            previous == '-') {
            continue;
        }
        return i;
    }

    return std::string::npos;
}

std::string replaceMacroArguments(
    const std::string &line,
    const std::unordered_map<std::string, std::string> &arguments) {
    std::string result;
    bool inString = false;
    bool escaped = false;
    char quote = '\0';

    for (size_t i = 0; i < line.length();) {
        char c = line[i];

        if (escaped) {
            result += c;
            escaped = false;
            i++;
            continue;
        }

        if (inString && c == '\\') {
            result += c;
            escaped = true;
            i++;
            continue;
        }

        if ((c == '"' || c == '\'') && !inString) {
            inString = true;
            quote = c;
            result += c;
            i++;
            continue;
        }

        if (inString && c == quote) {
            inString = false;
            result += c;
            i++;
            continue;
        }

        if (!inString &&
            (std::isalpha(static_cast<unsigned char>(c)) || c == '_' ||
             c == '.')) {
            size_t start = i;
            i++;
            while (i < line.length()) {
                unsigned char ch = static_cast<unsigned char>(line[i]);
                if (!std::isalnum(ch) && line[i] != '_' && line[i] != '.') {
                    break;
                }
                i++;
            }
            std::string name = line.substr(start, i - start);
            auto found = arguments.find(name);
            result += found == arguments.end() ? name : found->second;
            continue;
        }

        result += c;
        i++;
    }

    return result;
}
}

std::vector<std::string> utils::split(std::string str, char separator) {
    std::vector<std::string> parts;

    size_t start = 0;
    while (true) {
        size_t end = str.find(separator, start);

        if (end == std::string::npos) {
            parts.emplace_back(str.substr(start));
            break;
        }

        parts.emplace_back(str.substr(start, end - start));
        start = end + 1;
    }

    return parts;
}

std::vector<std::string> utils::splitArguments(const std::string &str) {
    std::vector<std::string> parts;
    std::string current;
    bool inString = false;
    bool escaped = false;
    char quote = '\0';
    int parenDepth = 0;

    for (char c : str) {
        if (escaped) {
            current += c;
            escaped = false;
            continue;
        }

        if (inString && c == '\\') {
            current += c;
            escaped = true;
            continue;
        }

        if ((c == '"' || c == '\'') && !inString) {
            inString = true;
            quote = c;
            current += c;
            continue;
        }

        if (inString && c == quote) {
            inString = false;
            current += c;
            continue;
        }

        if (!inString && c == '(') {
            parenDepth++;
        } else if (!inString && c == ')' && parenDepth > 0) {
            parenDepth--;
        }

        if (!inString && parenDepth == 0 && c == ',') {
            std::string argument = trim(current);
            if (!argument.empty()) {
                parts.push_back(argument);
            }
            current.clear();
            continue;
        }

        current += c;
    }

    std::string argument = trim(current);
    if (!argument.empty()) {
        parts.push_back(argument);
    }

    return parts;
}

std::string utils::trim(std::string str) {
    while (!str.empty() &&
           std::isspace(static_cast<unsigned char>(str.front())))
        str = str.substr(1);

    while (!str.empty() && std::isspace(static_cast<unsigned char>(str.back())))
        str = str.substr(0, str.length() - 1);

    return str;
}

std::string utils::stripComment(const std::string &str) {
    bool inString = false;
    bool escaped = false;
    char quote = '\0';

    for (size_t i = 0; i < str.length(); i++) {
        char c = str[i];

        if (escaped) {
            escaped = false;
            continue;
        }

        if (inString && c == '\\') {
            escaped = true;
            continue;
        }

        if ((c == '"' || c == '\'') && !inString) {
            inString = true;
            quote = c;
            continue;
        }

        if (inString && c == quote) {
            inString = false;
            continue;
        }

        if (!inString && c == ';') {
            return str.substr(0, i);
        }
    }

    return str;
}

int utils::parseNumber(const std::string &s) {
    size_t pos = 0;
    int value;
    int sign = 1;
    std::string number = trim(s);

    if (number.empty()) {
        throw std::invalid_argument("Invalid numeric literal");
    }

    if (number[0] == '+' || number[0] == '-') {
        if (number[0] == '-') {
            sign = -1;
        }
        number = number.substr(1);
    }

    if (number.empty()) {
        throw std::invalid_argument("Invalid numeric literal");
    }

    if (number.starts_with("0x") || number.starts_with("0X")) {
        value = std::stoi(number, &pos, 16);
    } else if (number.starts_with("0b") || number.starts_with("0B")) {
        value = std::stoi(number.substr(2), &pos, 2);
        pos += 2;
    } else if (number.starts_with("0o") || number.starts_with("0O")) {
        value = std::stoi(number.substr(2), &pos, 8);
        pos += 2;
    } else {
        value = std::stoi(number, &pos, 10);
    }

    if (pos != number.size()) {
        throw std::invalid_argument("Invalid numeric literal");
    }

    return value * sign;
}

namespace {
enum class ExprTokenType { Number, Identifier, Operator, LeftParen, RightParen, End };

struct ExprToken {
    ExprTokenType type = ExprTokenType::End;
    std::string text;
};

std::vector<ExprToken> tokenizeExpression(const std::string &text) {
    std::vector<ExprToken> tokens;
    for (std::size_t i = 0; i < text.length();) {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        if (std::isspace(ch)) {
            i++;
            continue;
        }
        if (std::isdigit(ch)) {
            std::size_t start = i;
            i++;
            while (i < text.length() &&
                   std::isalnum(static_cast<unsigned char>(text[i]))) {
                i++;
            }
            tokens.push_back({ExprTokenType::Number, text.substr(start, i - start)});
            continue;
        }
        if (std::isalpha(ch) || text[i] == '_' || text[i] == '.') {
            std::size_t start = i;
            i++;
            while (i < text.length()) {
                unsigned char current = static_cast<unsigned char>(text[i]);
                if (!std::isalnum(current) && text[i] != '_' && text[i] != '.') {
                    break;
                }
                i++;
            }
            tokens.push_back({ExprTokenType::Identifier,
                              text.substr(start, i - start)});
            continue;
        }
        if (text[i] == '(') {
            tokens.push_back({ExprTokenType::LeftParen, "("});
            i++;
            continue;
        }
        if (text[i] == ')') {
            tokens.push_back({ExprTokenType::RightParen, ")"});
            i++;
            continue;
        }
        if (i + 1 < text.length()) {
            std::string two = text.substr(i, 2);
            if (two == "<<" || two == ">>") {
                tokens.push_back({ExprTokenType::Operator, two});
                i += 2;
                continue;
            }
        }
        if (std::string("+-*/%&^|~").find(text[i]) != std::string::npos) {
            tokens.push_back({ExprTokenType::Operator, text.substr(i, 1)});
            i++;
            continue;
        }
        throw std::invalid_argument("Invalid expression token");
    }
    tokens.push_back({ExprTokenType::End, ""});
    return tokens;
}

int expressionPrecedence(const std::string &op) {
    if (op == "|") {
        return 1;
    }
    if (op == "^") {
        return 2;
    }
    if (op == "&") {
        return 3;
    }
    if (op == "<<" || op == ">>") {
        return 4;
    }
    if (op == "+" || op == "-") {
        return 5;
    }
    if (op == "*" || op == "/" || op == "%") {
        return 6;
    }
    return -1;
}

class TokenExpressionParser {
  public:
    TokenExpressionParser(std::vector<ExprToken> tokens,
                          const std::unordered_map<std::string, int> &symbols)
        : tokens(std::move(tokens)), symbols(symbols) {}

    int parse() {
        int value = parseExpression(1);
        if (peek().type != ExprTokenType::End) {
            throw std::invalid_argument("Unexpected expression input");
        }
        return value;
    }

  private:
    std::vector<ExprToken> tokens;
    const std::unordered_map<std::string, int> &symbols;
    std::size_t position = 0;

    const ExprToken &peek() const {
        return tokens[position];
    }

    ExprToken advance() {
        return tokens[position++];
    }

    int parseExpression(int minPrecedence) {
        int left = parseUnary();
        while (peek().type == ExprTokenType::Operator) {
            std::string op = peek().text;
            int precedence = expressionPrecedence(op);
            if (precedence < minPrecedence) {
                break;
            }
            advance();
            int right = parseExpression(precedence + 1);
            left = applyBinary(op, left, right);
        }
        return left;
    }

    int parseUnary() {
        if (peek().type == ExprTokenType::Operator &&
            (peek().text == "+" || peek().text == "-" || peek().text == "~")) {
            std::string op = advance().text;
            int value = parseUnary();
            if (op == "-") {
                return -value;
            }
            if (op == "~") {
                return ~value;
            }
            return value;
        }
        return parsePrimary();
    }

    int parsePrimary() {
        ExprToken token = advance();
        if (token.type == ExprTokenType::Number) {
            return utils::parseNumber(token.text);
        }
        if (token.type == ExprTokenType::Identifier) {
            auto found = symbols.find(token.text);
            if (found == symbols.end()) {
                throw std::invalid_argument("Unknown symbol " + token.text);
            }
            return found->second;
        }
        if (token.type == ExprTokenType::LeftParen) {
            int value = parseExpression(1);
            if (peek().type != ExprTokenType::RightParen) {
                throw std::invalid_argument("Missing closing parenthesis");
            }
            advance();
            return value;
        }
        throw std::invalid_argument("Expected expression term");
    }

    int applyBinary(const std::string &op, int left, int right) {
        if (op == "+") {
            return left + right;
        }
        if (op == "-") {
            return left - right;
        }
        if (op == "*") {
            return left * right;
        }
        if (op == "/") {
            if (right == 0) {
                throw std::invalid_argument("Division by zero");
            }
            return left / right;
        }
        if (op == "%") {
            if (right == 0) {
                throw std::invalid_argument("Modulo by zero");
            }
            return left % right;
        }
        if (op == "<<") {
            if (right < 0 || right >= 32) {
                throw std::invalid_argument("Invalid left shift");
            }
            return left << right;
        }
        if (op == ">>") {
            if (right < 0 || right >= 32) {
                throw std::invalid_argument("Invalid right shift");
            }
            return left >> right;
        }
        if (op == "&") {
            return left & right;
        }
        if (op == "^") {
            return left ^ right;
        }
        if (op == "|") {
            return left | right;
        }
        throw std::invalid_argument("Unknown operator");
    }
};
}

bool utils::evaluateExpression(
    const std::string &text,
    const std::unordered_map<std::string, int> &symbols,
    int &value) {
    try {
        TokenExpressionParser parser(tokenizeExpression(trim(text)), symbols);
        value = parser.parse();
        return true;
    } catch (...) {
        return false;
    }
}

Parser::Parser(const std::string &input, const std::string &sourcePath)
    : input(input), sourcePath(sourcePath) {
    Dictionary::registerAllInstructions();
}

void Parser::fail(std::size_t line, const std::string &message) const {
    if (!diagnosticPath.empty()) {
        throw std::runtime_error(diagnosticPath + ":" + std::to_string(line) +
                                 ": " + message);
    }
    throw std::runtime_error("line " + std::to_string(line) + ": " + message);
}

void Parser::parse() {
    expressions.clear();
    macros.clear();
    macroExpansionDepth = 0;
    std::vector<std::string> includeStack;
    parseSource(input, sourcePath, includeStack);
}

void Parser::parseSource(const std::string &source, const std::string &path,
                         std::vector<std::string> &includeStack) {
    std::string previousDiagnosticPath = diagnosticPath;
    diagnosticPath = path;
    const std::vector<std::string> lines = utils::split(source, '\n');
    for (size_t lineIndex = 0; lineIndex < lines.size(); lineIndex++) {
        std::size_t lineNumber = lineIndex + 1;
        std::string trimmedLine =
            utils::trim(utils::stripComment(lines[lineIndex]));
        if (trimmedLine.empty()) {
            continue;
        }

        std::string firstToken;
        size_t firstEnd = 0;
        while (firstEnd < trimmedLine.length() &&
               !std::isblank(static_cast<unsigned char>(trimmedLine[firstEnd]))) {
            firstToken += trimmedLine[firstEnd];
            firstEnd++;
        }

        if (firstToken == ".macro") {
            size_t i = firstEnd;
            while (i < trimmedLine.length() &&
                   std::isblank(static_cast<unsigned char>(trimmedLine[i]))) {
                i++;
            }
            size_t nameStart = i;
            while (i < trimmedLine.length() &&
                   !std::isblank(static_cast<unsigned char>(trimmedLine[i])) &&
                   trimmedLine[i] != ',') {
                i++;
            }
            std::string macroName = trimmedLine.substr(nameStart, i - nameStart);
            if (!isIdentifier(macroName) || macroName.starts_with(".")) {
                fail(lineNumber, ".macro expects a macro name.");
            }
            if (macros.contains(macroName)) {
                fail(lineNumber, "Macro " + macroName + " is already defined.");
            }
            while (i < trimmedLine.length() &&
                   (std::isblank(static_cast<unsigned char>(trimmedLine[i])) ||
                    trimmedLine[i] == ',')) {
                i++;
            }
            std::vector<std::string> parameters =
                utils::splitArguments(trimmedLine.substr(i));
            for (const std::string &parameter : parameters) {
                if (!isIdentifier(parameter)) {
                    fail(lineNumber, "Invalid macro parameter " + parameter + ".");
                }
            }

            MacroDefinition macro;
            macro.parameters = parameters;
            macro.line = lineNumber;
            bool foundEnd = false;
            while (++lineIndex < lines.size()) {
                std::string bodyLine =
                    utils::trim(utils::stripComment(lines[lineIndex]));
                if (bodyLine == ".endmacro") {
                    foundEnd = true;
                    break;
                }
                if (bodyLine.starts_with(".macro")) {
                    fail(lineIndex + 1, "Nested macros are not supported.");
                }
                macro.body.push_back(lines[lineIndex]);
            }
            if (!foundEnd) {
                fail(lineNumber, "Macro " + macroName + " is missing .endmacro.");
            }
            macros[macroName] = macro;
            continue;
        }

        if (firstToken == ".endmacro") {
            fail(lineNumber, ".endmacro without matching .macro.");
        }

        auto macro = macros.find(firstToken);
        if (macro != macros.end()) {
            if (macroExpansionDepth >= 64) {
                fail(lineNumber, "Macro expansion depth exceeded.");
            }
            std::string argumentText = utils::trim(trimmedLine.substr(firstEnd));
            std::vector<std::string> arguments = utils::splitArguments(argumentText);
            if (arguments.size() != macro->second.parameters.size()) {
                fail(lineNumber,
                     "Macro " + firstToken + " expected " +
                         std::to_string(macro->second.parameters.size()) +
                         " arguments but got " +
                         std::to_string(arguments.size()) + ".");
            }
            std::unordered_map<std::string, std::string> replacements;
            for (size_t i = 0; i < arguments.size(); i++) {
                replacements[macro->second.parameters[i]] = arguments[i];
            }
            std::string expanded;
            for (const std::string &bodyLine : macro->second.body) {
                expanded += replaceMacroArguments(bodyLine, replacements);
                expanded += '\n';
            }
            macroExpansionDepth++;
            parseSource(expanded, path, includeStack);
            macroExpansionDepth--;
            continue;
        }

        if (trimmedLine[trimmedLine.length() - 1] == ':') {
            expressions.push_back(std::make_unique<Label>(
                trimmedLine.substr(0, trimmedLine.length() - 1), lineNumber,
                path));
        } else if (trimmedLine[0] == '.') {
            std::string name;

            size_t i = 0;

            while (i < trimmedLine.length() &&
                   !std::isblank(static_cast<unsigned char>(trimmedLine[i]))) {
                name += trimmedLine[i];
                i++;
            }

            while (i < trimmedLine.length() &&
                   std::isblank(static_cast<unsigned char>(trimmedLine[i]))) {
                i++;
            }

            std::vector<std::string> arguments =
                utils::splitArguments(trimmedLine.substr(i));

            if (name == ".include") {
                if (arguments.size() != 1) {
                    fail(lineNumber,
                         ".include expects exactly one file path argument.");
                }

                std::filesystem::path includePath = unquote(arguments[0]);
                if (includePath.is_relative()) {
                    includePath = includeBase(path) / includePath;
                }

                includePath = std::filesystem::weakly_canonical(includePath);
                std::string includeKey = includePath.string();
                if (std::find(includeStack.begin(), includeStack.end(),
                              includeKey) != includeStack.end()) {
                    fail(lineNumber,
                         "Recursive include detected for " + includeKey + ".");
                }

                includeStack.push_back(includeKey);
                try {
                    parseSource(readTextFile(includePath), includeKey,
                                includeStack);
                } catch (const std::exception &error) {
                    includeStack.pop_back();
                    std::string message = error.what();
                    if (message.starts_with(includeKey + ":")) {
                        diagnosticPath = previousDiagnosticPath;
                        throw;
                    }
                    diagnosticPath = path;
                    fail(lineNumber, message);
                }
                includeStack.pop_back();
                continue;
            }

            expressions.push_back(std::make_unique<Directive>(
                name, arguments, std::string(trimmedLine), lineNumber, path));
        } else {
            std::string opcode;

            size_t i = 0;

            while (i < trimmedLine.length() &&
                   !std::isblank(static_cast<unsigned char>(trimmedLine[i]))) {
                opcode += trimmedLine[i];
                i++;
            }

            while (i < trimmedLine.length() &&
                   std::isblank(static_cast<unsigned char>(trimmedLine[i]))) {
                i++;
            }

            std::vector<std::string> operands =
                utils::splitArguments(trimmedLine.substr(i));

            expressions.push_back(std::make_unique<Instruction>(
                opcode, operands, std::string(trimmedLine), lineNumber, path));
        }
    }
    diagnosticPath = previousDiagnosticPath;
}

void Parser::verifyIntegrity() {
    for (auto &expression : expressions) {
        diagnosticPath = expression->sourcePath;
        if (expression->type == ExpressionType::Directive) {
            auto directive = dynamic_cast<Directive *>(expression.get());
            if (directive->name == ".const" || directive->name == ".constant" ||
                directive->name == ".string" || directive->name == ".data" ||
                directive->name == ".byte" || directive->name == ".word" ||
                directive->name == ".addr24" ||
                directive->name == ".addressOf" ||
                directive->name == ".locate" ||
                directive->name == ".include" ||
                directive->name == ".symbols" || directive->name == ".bin") {
                continue;
            }
            fail(directive->line,
                 "Directive " + directive->name + " is not a valid directive.");
        }
        if (expression->type == ExpressionType::Instruction) {
            auto instruction = dynamic_cast<Instruction *>(expression.get());
            bool foundMatch = false;
            for (auto &validInstruction : Dictionary::all()) {
                if (instruction->opcode == validInstruction->name &&
                    instruction->operands.size() ==
                        static_cast<std::size_t>(
                            validInstruction->argumentCount)) {
                    foundMatch = true;

                    for (auto argument : instruction->operands) {
                        if (argument.starts_with("0") ||
                            std::isdigit(
                                static_cast<unsigned char>(argument[0]))) {
                            if (isExpressionCandidate(argument)) {
                                continue;
                            }
                            try {
                                int val = utils::parseNumber(argument);
                                (void)val;
                            } catch (...) {
                                fail(instruction->line,
                                     "String '" + argument +
                                         "' was detected as a number but "
                                         "cannot be "
                                         "parsed as one.");
                            }
                        }

                        if (argument.starts_with("r") &&
                            argument.length() > 1 &&
                            std::isdigit(
                                static_cast<unsigned char>(argument[1])) &&
                            !isGeneralRegister(argument)) {
                            fail(instruction->line,
                                 "Register " + argument +
                                     " is invalid in the E16 architecture.");
                        }
                    }

                    verifySpecialRegisters(instruction);
                    continue;
                }
                if (instruction->opcode == validInstruction->name &&
                    instruction->operands.size() !=
                        static_cast<std::size_t>(
                            validInstruction->argumentCount)) {
                    fail(instruction->line,
                         "Instruction " + instruction->opcode + " expected " +
                             std::to_string(validInstruction->argumentCount) +
                             " arguments but instead " +
                             std::to_string(instruction->operands.size()) +
                             " were provided.");
                }
            }
            if (!foundMatch) {
                fail(instruction->line, "Instruction " + instruction->opcode +
                                            " is not a valid instruction.");
            }
        }
    }
}

void Parser::printExpressions() {
    std::cout << "Parsing has " << expressions.size() << " expressions. "
              << std::endl;
    for (auto &expression : expressions) {
        if (expression->type == ExpressionType::Label) {
            auto label = dynamic_cast<Label *>(expression.get());
            std::cout << "Label: " << label->name << std::endl;
        } else if (expression->type == ExpressionType::Directive) {
            auto directive = dynamic_cast<Directive *>(expression.get());
            std::cout << "Directive: " << directive->name << std::endl;
            for (auto argument : directive->arguments) {
                std::cout << "    " << argument << std::endl;
            }
        } else if (expression->type == ExpressionType::Instruction) {
            auto instruction = dynamic_cast<Instruction *>(expression.get());
            std::cout << "Instruction: " << instruction->opcode << std::endl;
            if (instruction->parsedOperands.empty()) {
                for (auto operand : instruction->operands) {
                    std::cout << "    " << operand << std::endl;
                }
            } else {
                for (auto operand : instruction->parsedOperands) {
                    std::cout << "    "
                              << "Addressing Mode: "
                              << addressingModeToString(operand.mode)
                              << ", Base: " << operand.base;

                    if (operand.offset != 0) {
                        std::cout << ", Offset: "
                                  << std::to_string(operand.offset);
                    }

                    if (!operand.offsetReg.empty()) {
                        std::cout << ", Offset Register: " << operand.offsetReg;
                    }
                    std::cout << std::endl;
                }
            }
        }
    }
}

void Parser::verifySpecialRegisters(Instruction *instruction) {
    if (instruction->opcode == "get") {
        if (!isGeneralRegister(instruction->operands[0]) ||
            !isSpecialRegister(instruction->operands[1])) {
            fail(instruction->line,
                 "get expects a general register followed by a special "
                 "register.");
        }
        return;
    }

    if (instruction->opcode == "set") {
        if (!isSpecialRegister(instruction->operands[0])) {
            fail(instruction->line,
                 "set expects a special register as its first argument.");
        }
        return;
    }

    for (auto argument : instruction->operands) {
        if (isSpecialRegister(argument)) {
            if (instruction->opcode == "mov") {
                fail(instruction->line,
                     "For readability, try to address special registers like " +
                         argument + " with 'get' and 'set'");
            }
        }
    }
}

void Parser::parseAddressingModes() {
    const auto constants = collectParserConstants(expressions);
    std::unordered_map<std::string, int> parserSymbols = constants;
    for (const auto &expression : expressions) {
        if (expression->type != ExpressionType::Label) {
            continue;
        }
        auto *label = dynamic_cast<Label *>(expression.get());
        parserSymbols[label->name] = 0;
    }

    for (auto &expression : expressions) {
        diagnosticPath = expression->sourcePath;
        if (expression->type == ExpressionType::Instruction) {
            auto *instruction = dynamic_cast<Instruction *>(expression.get());
            instruction->parsedOperands.clear();
            for (std::string operand : instruction->operands) {
                int resolvedExpression = 0;
                if (resolveConstExpression(operand, constants,
                                           resolvedExpression)) {
                    instruction->parsedOperands.push_back(InstructionOperand(
                        AddressingMode::Immediate,
                        normalizeNumber(resolvedExpression)));
                    continue;
                }

                int deferredExpression = 0;
                if (utils::evaluateExpression(operand, parserSymbols,
                                              deferredExpression)) {
                    instruction->parsedOperands.push_back(InstructionOperand(
                        AddressingMode::Immediate, operand));
                    continue;
                }

                try {
                    int val = utils::parseNumber(operand);
                    (void)val;
                    instruction->parsedOperands.push_back(
                        InstructionOperand(AddressingMode::Immediate, operand));
                    continue;
                } catch (...) {
                }

                if (isRegister(operand)) {
                    instruction->parsedOperands.push_back(
                        InstructionOperand(AddressingMode::Register, operand));
                    continue;
                }

                if (operand.starts_with("dp:")) {
                    int val = 0;
                    if (utils::evaluateExpression(operand.substr(3), constants,
                                                  val)) {
                        instruction->parsedOperands.push_back(
                            InstructionOperand(AddressingMode::DirectPageOffset,
                                               "dp", val));
                        continue;
                    }
                    fail(instruction->line,
                         "Badly formulated direct page offset mode argument " +
                             operand + ".");
                }

                if (operand.starts_with("#")) {
                    int val = 0;
                    if (utils::evaluateExpression(operand.substr(1), constants,
                                                  val)) {
                        instruction->parsedOperands.push_back(
                            InstructionOperand(AddressingMode::StackRelative,
                                               "sp", val));
                        continue;
                    }
                    fail(instruction->line,
                         "Badly formulated stack-relative offset mode "
                         "argument " +
                             operand + ".");
                }

                if (operand[0] == '@') {
                    if (operand.length() > 2 && operand[1] == '(' &&
                        operand.back() == ')') {
                        std::string inner = utils::trim(
                            operand.substr(2, operand.length() - 3));
                        int resolvedAddress = 0;
                        if (resolveConstExpression(inner, constants,
                                                   resolvedAddress)) {
                            instruction->parsedOperands.push_back(
                                InstructionOperand(
                                    AddressingMode::Absolute,
                                    normalizeNumber(resolvedAddress)));
                            continue;
                        }

                        if (utils::evaluateExpression(inner, parserSymbols,
                                                      resolvedAddress)) {
                            instruction->parsedOperands.push_back(
                                InstructionOperand(AddressingMode::Absolute,
                                                   inner));
                            continue;
                        }
                    }

                    int resolvedAddress = 0;
                    if (utils::evaluateExpression(operand.substr(1), constants,
                                                  resolvedAddress)) {
                        instruction->parsedOperands.push_back(
                            InstructionOperand(AddressingMode::Absolute,
                                               normalizeNumber(resolvedAddress)));
                        continue;
                    }

                    if (utils::evaluateExpression(operand.substr(1),
                                                  parserSymbols,
                                                  resolvedAddress)) {
                        instruction->parsedOperands.push_back(
                            InstructionOperand(AddressingMode::Absolute,
                                               operand.substr(1)));
                        continue;
                    }

                    if (isRegister(operand.substr(1))) {
                        instruction->parsedOperands.push_back(
                            InstructionOperand(AddressingMode::RegisterIndirect,
                                               operand.substr(1)));
                        continue;
                    }

                    if (operand.length() > 2 && operand[1] == '(' &&
                        operand.back() == ')') {
                        std::string inner = utils::trim(
                            operand.substr(2, operand.length() - 3));
                        size_t signPos = findTopLevelOffsetOperator(inner);

                        if (signPos == std::string::npos) {
                            fail(instruction->line,
                                 "Badly formulated mode for argument " +
                                     operand);
                        }

                        std::string left =
                            utils::trim(inner.substr(0, signPos));
                        char sign;
                        sign = inner[signPos];
                        std::string right =
                            utils::trim(inner.substr(signPos + 1));

                        if (left.empty() || right.empty()) {
                            fail(instruction->line,
                                 "Badly formulated mode for argument " +
                                     operand);
                        }

                        if (isRegister(right) && sign == '+') {
                            instruction->parsedOperands.push_back(
                                InstructionOperand(AddressingMode::Indexed,
                                                   left, 0, right));
                            continue;
                        }

                        if (isRegister(left)) {
                            int val = 0;
                            if (utils::evaluateExpression(
                                    (sign == '-' ? "-" : "") + right,
                                    constants, val)) {
                                instruction->parsedOperands.push_back(
                                    InstructionOperand(
                                        AddressingMode::BasePlusOffset, left,
                                        val));
                                continue;
                            }
                            fail(instruction->line,
                                 "The offsetting mode for argument " +
                                     operand + " is badly formulated");
                        }

                        fail(instruction->line,
                             "Badly formulated mode for argument " + operand);
                    }

                    if (isIdentifier(operand.substr(1))) {
                        instruction->parsedOperands.push_back(
                            InstructionOperand(AddressingMode::Absolute,
                                               operand.substr(1)));
                        continue;
                    }
                }

                bool foundLabelOrConstant = false;
                for (auto &candidate : expressions) {
                    if (foundLabelOrConstant) {
                        break;
                    }
                    if (candidate->type == ExpressionType::Label) {
                        Label *label = dynamic_cast<Label *>(candidate.get());
                        if (label->name == operand) {
                            instruction->parsedOperands.push_back(
                                InstructionOperand(AddressingMode::Label,
                                                   operand));
                            foundLabelOrConstant = true;
                        }
                    } else if (candidate->type == ExpressionType::Directive) {
                        Directive *directive =
                            dynamic_cast<Directive *>(candidate.get());
                        if (directive->name == ".const" ||
                            directive->name == ".constant") {
                            if (directive->arguments.size() != 2) {
                                fail(directive->line,
                                     "A constant directive must have exactly "
                                     "two arguments.");
                            }
                            std::string name = directive->arguments[0];
                            std::string substitution = directive->arguments[1];
                            if (name == operand) {
                                try {
                                    int val = utils::parseNumber(substitution);
                                    (void)val;
                                    instruction->parsedOperands.push_back(
                                        InstructionOperand(
                                            AddressingMode::Immediate,
                                            substitution));
                                    foundLabelOrConstant = true;
                                } catch (...) {
                                    fail(instruction->line,
                                         "The offsetting mode for argument " +
                                             operand + " is badly formulated");
                                }
                            }
                        }
                    }
                }

                if (!foundLabelOrConstant) {
                    fail(instruction->line,
                         "The addressing mode for argument " + operand +
                             " is not correctly formulated.");
                }
            }
        }
    }
}

bool Parser::isRegister(std::string val) {
    return isGeneralRegister(val) || isSpecialRegister(val);
}

std::string Parser::addressingModeToString(AddressingMode mode) {
    switch (mode) {
    case AddressingMode::Immediate:
        return "Immediate";
    case AddressingMode::Register:
        return "Register";
    case AddressingMode::Absolute:
        return "Absolute";
    case AddressingMode::BasePlusOffset:
        return "Base + Offset";
    case AddressingMode::DirectPageOffset:
        return "Direct Page Offset";
    case AddressingMode::Indexed:
        return "Indexed";
    case AddressingMode::RegisterIndirect:
        return "Register Indirect";
    case AddressingMode::StackRelative:
        return "Stack Relative";
    case AddressingMode::Label:
        return "Label";
    }
    return "Unknown";
}
