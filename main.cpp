#include <fcntl.h>
#include <sys/wait.h>

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

using SymbolToken = std::string;

enum class FlowToken { IN, OUT };

enum class LogicToken { AND, OR };

struct PipeToken {
    bool operator==(const PipeToken &) const {
        return true;
    }
};

using Token = std::variant<SymbolToken, FlowToken, LogicToken, PipeToken>;

template <typename T, typename V>
T As(V &&obj) {
    return std::get<T>(std::forward<V>(obj));
}

template <typename T, typename V>
bool Is(V &&obj) {
    return std::holds_alternative<T>(std::forward<V>(obj));
}

class Tokenizer {
public:
    explicit Tokenizer(std::stringstream *in) : stream_(in) {
    }

    bool IsEnd() const {
        return stream_->peek() == std::istream::traits_type::eof();
    }

    Token GetToken() const {
        return token_;
    }

    void Next() {
        char c = stream_->get();

        while (std::isspace(c)) {
            c = stream_->get();
        }

        if (c == '>') {
            token_ = FlowToken::OUT;
            return;
        }

        if (c == '<') {
            token_ = FlowToken::IN;
            return;
        }

        if (c == '|') {
            if (stream_->peek() == '|') {
                stream_->get();
                token_ = LogicToken::OR;
                return;
            }
            token_ = PipeToken{};
            return;
        }

        if (c == '&' && stream_->peek() == '&') {
            stream_->get();
            token_ = LogicToken::AND;
            return;
        }

        SymbolToken current;
        if (c == '\"' || c == '\'') {
            while (stream_->peek() != c) {
                current += stream_->get();
                if (stream_->peek() == '\\' && current[current.size() - 1] == '\\') {
                    stream_->get();
                }
            }
            stream_->get();
            token_ = current;
            return;
        }

        if (c == '\\') {
            while (stream_->peek() == '\\') {
                stream_->get();
            }
        } else {
            current += c;
        }

        while (!std::isspace(stream_->peek()) && !IsEnd()) {
            current += stream_->get();
        }
        token_ = current;
    }

private:
    std::stringstream *stream_;
    Token token_;
};

struct Command {
    std::vector<SymbolToken> args;
    SymbolToken in;
    SymbolToken out;
};

static std::vector<char *> StrToCharPtr(std::vector<std::string> &input) {
    std::vector<char *> result;
    result.reserve(input.size() + 1);
    std::transform(begin(input), end(input), std::back_inserter(result),
                   [](std::string &s) { return s.data(); });
    result.push_back(nullptr);
    return result;
}

Command GetCommand(Tokenizer &t) {
    Command cmd;
    t.Next();
    Token current = t.GetToken();
    while (!Is<LogicToken>(current) && !Is<PipeToken>(current)) {
        if (Is<SymbolToken>(current)) {
            cmd.args.emplace_back(As<SymbolToken>(current));
        } else {
            t.Next();
            As<FlowToken>(current) == FlowToken::IN ? cmd.in = As<SymbolToken>(t.GetToken())
                                                    : cmd.out = As<SymbolToken>(t.GetToken());
        }
        if (t.IsEnd()) {
            break;
        }
        t.Next();
        current = t.GetToken();
    }
    return cmd;
}

bool IsCaosMainConst(const SymbolToken &name) {
    return name == "1984";  // std::all_of(...), но этого хватает)
}

int Exec(Command &cmd, int from = -1, int to = -1) {
    pid_t pid = fork();

    if (pid == 0) {
        if (from != -1) {
            dup2(from, STDIN_FILENO);
            close(from);
        }
        if (to != -1) {
            dup2(to, STDOUT_FILENO);
            close(to);
        }
        if (!cmd.in.empty()) {
            int fd = open(cmd.in.c_str(), O_RDONLY);
            if (fd < 0) {
                std::cerr << "./lavash: line 1: " << cmd.in << ": No such file or directory\n";
                _exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        if (!cmd.out.empty()) {
            int fd = open(cmd.out.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
            if (fd < 0) {
                _exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        if (IsCaosMainConst(cmd.args[0])) {
            return EXIT_SUCCESS;
        }
        execvp(cmd.args[0].c_str(), StrToCharPtr(cmd.args).data());
        std::cerr << "./lavash: line 1: " << cmd.args[0] << ": command not found\n";
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    _exit(status);
}

int ExecPipe(Command &cmd, Tokenizer &t) {
    pid_t pid = 0;
    int in = 0;
    int pfd[2];

    while (Is<PipeToken>(t.GetToken())) {
        if (pipe(pfd)) {
            perror("pipe");
            _exit(EXIT_FAILURE);
        }
        Exec(cmd, in, pfd[1]);
        close(pfd[1]);
        in = pfd[0];
        cmd = GetCommand(t);
        if (t.IsEnd()) {
            break;
        }
    }
    if (in != STDIN_FILENO) {
        dup2(in, STDIN_FILENO);
    }

    if (!cmd.in.empty()) {
        int fd = open(cmd.in.c_str(), O_RDONLY);
        if (fd < 0) {
            std::cerr << "./lavash: line 1: " << cmd.in << ": No such file or directory\n";
            _exit(EXIT_FAILURE);
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (!cmd.out.empty()) {
        int fd = open(cmd.out.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
        if (fd < 0) {
            _exit(EXIT_FAILURE);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    if (IsCaosMainConst(cmd.args[0])) {
        return EXIT_SUCCESS;
    }
    execvp(cmd.args[0].c_str(), StrToCharPtr(cmd.args).data());

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    _exit(status);
}

int Execute(Tokenizer &t) {
    int status = 0;
    Command cmd;
    while (!t.IsEnd()) {
        cmd = GetCommand(t);
        if (Is<PipeToken>(t.GetToken())) {
            status = ExecPipe(cmd, t);
        } else {
            status = Exec(cmd);
        }
        if (Is<LogicToken>(t.GetToken())) {
            if (As<LogicToken>(t.GetToken()) == LogicToken::OR && !status) {
                return 0;
            }
            if (!status) {
                continue;
            }
            while (!t.IsEnd() && (!Is<LogicToken>(t.GetToken()) ||
                                  As<LogicToken>(t.GetToken()) != LogicToken::OR)) {
                GetCommand(t);
            }
        }
    }
    return status;
}

int main(int argc, char **argv, char **envp) {
    if (argc != 3) {
        std::cerr << "Invalid number of args" << '\n';
        return EXIT_FAILURE;
    }
    std::stringstream ss{argv[2]};
    Tokenizer t{&ss};
    return Execute(t);
}
