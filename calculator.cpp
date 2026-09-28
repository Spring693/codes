// calculator.cpp — 支持变量的科学表达式计算器。
//
// 功能：把中缀表达式转为后缀（逆波兰）再求值，支持 + - * / % ^、
//       括号、一元负号、常量 pi/e，以及 sin/cos/log/sqrt/abs 等函数
//       和变量赋值（x = 3; y = x * 2 + 1）。
//
// 编译：
//   g++ -std=c++17 -O2 -o calculator calculator.cpp
// 运行：
//   ./calculator                // 进入交互模式
//   ./calculator "sqrt(2)^2"    // 计算单个表达式后退出

#include <cmath>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <map>
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kE = 2.71828182845904523536;

int precedence(const std::string& op) {
    if (op == "+" || op == "-") return 1;
    if (op == "*" || op == "/" || op == "%") return 2;
    if (op == "^") return 3;
    return 0;
}

bool rightAssociative(const std::string& op) { return op == "^"; }

bool isFunctionName(const std::string& name) {
    return name == "sin" || name == "cos" || name == "tan" ||
           name == "log" || name == "ln" || name == "sqrt" ||
           name == "abs" || name == "exp";
}

double applyFunction(const std::string& name, double value) {
    if (name == "sin") return std::sin(value);
    if (name == "cos") return std::cos(value);
    if (name == "tan") return std::tan(value);
    if (name == "log") return std::log10(value);
    if (name == "ln") return std::log(value);
    if (name == "sqrt") {
        if (value < 0) throw std::runtime_error("sqrt 的参数不能为负数");
        return std::sqrt(value);
    }
    if (name == "abs") return std::fabs(value);
    if (name == "exp") return std::exp(value);
    throw std::runtime_error("未知函数：" + name);
}

double applyOperator(const std::string& op, double lhs, double rhs) {
    if (op == "+") return lhs + rhs;
    if (op == "-") return lhs - rhs;
    if (op == "*") return lhs * rhs;
    if (op == "/") {
        if (rhs == 0.0) throw std::runtime_error("除数为零");
        return lhs / rhs;
    }
    if (op == "%") {
        if (rhs == 0.0) throw std::runtime_error("取模的除数为零");
        return std::fmod(lhs, rhs);
    }
    if (op == "^") return std::pow(lhs, rhs);
    throw std::runtime_error("未知运算符：" + op);
}

// 把表达式切分为数字、标识符、运算符与括号。
std::vector<std::string> tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < input.size()) {
        char c = input[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            size_t start = i;
            bool seenDot = false;
            while (i < input.size() &&
                   (std::isdigit(static_cast<unsigned char>(input[i])) || input[i] == '.')) {
                if (input[i] == '.') {
                    if (seenDot) throw std::runtime_error("数字中含有多个小数点");
                    seenDot = true;
                }
                ++i;
            }
            // 支持科学计数法，如 1e-3
            if (i < input.size() && (input[i] == 'e' || input[i] == 'E')) {
                ++i;
                if (i < input.size() && (input[i] == '+' || input[i] == '-')) ++i;
                while (i < input.size() && std::isdigit(static_cast<unsigned char>(input[i]))) ++i;
            }
            tokens.push_back(input.substr(start, i - start));
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c))) {
            size_t start = i;
            while (i < input.size() &&
                   std::isalnum(static_cast<unsigned char>(input[i]))) {
                ++i;
            }
            tokens.push_back(input.substr(start, i - start));
            continue;
        }
        if (std::string("+-*/%^(),=").find(c) != std::string::npos) {
            tokens.emplace_back(1, c);
            ++i;
            continue;
        }
        throw std::runtime_error(std::string("无法识别的字符：") + c);
    }
    return tokens;
}

std::string lastVariable;  // 记录最近一次赋值的变量名，供 ans 使用

// 中缀转后缀（Shunting-yard 算法）。
std::vector<std::string> toPostfix(const std::vector<std::string>& tokens) {
    std::vector<std::string> output;
    std::stack<std::string> ops;
    bool expectOperand = true;  // 用于区分一元负号与二元减号

    for (const auto& token : tokens) {
        if (!token.empty() &&
            (std::isdigit(static_cast<unsigned char>(token[0])) || token[0] == '.')) {
            output.push_back(token);
            expectOperand = false;
        } else if (isFunctionName(token)) {
            ops.push(token);
            expectOperand = true;
        } else if (token == ",") {
            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }
            if (ops.empty()) throw std::runtime_error("逗号位置不合法");
            expectOperand = true;
        } else if (token == "u-") {
            ops.push("u-");
            expectOperand = true;
        } else if (precedence(token) > 0) {
            if (expectOperand) {
                if (token == "-") {
                    ops.push("u-");
                    continue;
                }
                if (token != "+") {
                    throw std::runtime_error("运算符 " + token + " 缺少左操作数");
                }
                continue;  // 一元正号可直接忽略
            }
            while (!ops.empty() && ops.top() != "(" &&
                   (precedence(ops.top()) > precedence(token) ||
                    (precedence(ops.top()) == precedence(token) && !rightAssociative(token)))) {
                output.push_back(ops.top());
                ops.pop();
            }
            ops.push(token);
            expectOperand = true;
        } else if (token == "(") {
            ops.push(token);
            expectOperand = true;
        } else if (token == ")") {
            while (!ops.empty() && ops.top() != "(") {
                output.push_back(ops.top());
                ops.pop();
            }
            if (ops.empty()) throw std::runtime_error("括号不匹配");
            ops.pop();
            if (!ops.empty() && isFunctionName(ops.top())) {
                output.push_back(ops.top());
                ops.pop();
            }
            expectOperand = false;
        } else {
            output.push_back(token);  // 变量或常量
            expectOperand = false;
        }
    }

    while (!ops.empty()) {
        if (ops.top() == "(") throw std::runtime_error("括号不匹配");
        output.push_back(ops.top());
        ops.pop();
    }
    return output;
}

double evalPostfix(const std::vector<std::string>& postfix,
                   std::map<std::string, double>& variables) {
    std::stack<double> stack;

    for (const auto& token : postfix) {
        if (!token.empty() &&
            (std::isdigit(static_cast<unsigned char>(token[0])) || token[0] == '.')) {
            stack.push(std::stod(token));
        } else if (isFunctionName(token)) {
            if (stack.empty()) throw std::runtime_error(token + " 缺少参数");
            double v = stack.top();
            stack.pop();
            stack.push(applyFunction(token, v));
        } else if (token == "u-") {
            if (stack.empty()) throw std::runtime_error("一元负号缺少操作数");
            double v = stack.top();
            stack.pop();
            stack.push(-v);
        } else if (precedence(token) > 0) {
            if (stack.size() < 2) throw std::runtime_error("运算符 " + token + " 缺少操作数");
            double rhs = stack.top();
            stack.pop();
            double lhs = stack.top();
            stack.pop();
            stack.push(applyOperator(token, lhs, rhs));
        } else {
            if (token == "pi") {
                stack.push(kPi);
            } else if (token == "e") {
                stack.push(kE);
            } else if (token == "ans") {
                stack.push(variables.count(lastVariable) ? variables[lastVariable] : 0.0);
            } else if (variables.count(token)) {
                stack.push(variables[token]);
            } else {
                throw std::runtime_error("未定义的变量：" + token);
            }
        }
    }

    if (stack.size() != 1) throw std::runtime_error("表达式不合法");
    return stack.top();
}

// 求值入口，支持 "x = 表达式" 形式的赋值。
double evaluate(const std::string& expression, std::map<std::string, double>& variables) {
    auto tokens = tokenize(expression);
    if (tokens.empty()) throw std::runtime_error("空表达式");

    if (tokens.size() >= 3 && tokens[1] == "=") {
        const std::string& name = tokens[0];
        if (isFunctionName(name) || name == "pi" || name == "e" || name == "ans") {
            throw std::runtime_error("不能给保留名赋值：" + name);
        }
        std::vector<std::string> rhs(tokens.begin() + 2, tokens.end());
        double value = evalPostfix(toPostfix(rhs), variables);
        variables[name] = value;
        lastVariable = name;
        return value;
    }

    return evalPostfix(toPostfix(tokens), variables);
}

void printHelp() {
    std::cout << "支持：+ - * / % ^  ( )  一元负号  常量 pi/e/ans\n"
              << "函数：sin cos tan log ln sqrt abs exp\n"
              << "赋值：x = 3 * (2 + 1)  之后可写 x ^ 2\n"
              << "命令：help / vars / quit\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::map<std::string, double> variables;

    if (argc > 1) {
        std::string expr;
        for (int i = 1; i < argc; ++i) {
            expr += argv[i];
            if (i + 1 < argc) expr += " ";
        }
        try {
            double value = evaluate(expr, variables);
            std::cout << std::setprecision(12) << value << "\n";
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "错误：" << e.what() << "\n";
            return 1;
        }
    }

    std::cout << "科学表达式计算器（输入 help 查看用法，quit 退出）\n";
    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        if (line == "quit" || line == "exit") break;
        if (line == "help") {
            printHelp();
            continue;
        }
        if (line == "vars") {
            if (variables.empty()) {
                std::cout << "（暂无变量）\n";
            } else {
                for (const auto& kv : variables) {
                    std::cout << "  " << kv.first << " = " << kv.second << "\n";
                }
            }
            continue;
        }
        try {
            double value = evaluate(line, variables);
            std::cout << "= " << std::setprecision(12) << value << "\n";
        } catch (const std::exception& e) {
            std::cout << "错误：" << e.what() << "\n";
        }
    }
    return 0;
}
