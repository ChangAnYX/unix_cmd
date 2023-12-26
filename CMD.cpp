#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <iostream>
#include <map>
#include <pwd.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace std;

const string WHITE_SPACE = " \t\r\n";
const string SYMBOL = "|<>";
#define MAX_ARGV_LEN 128
#define SHOW_PANIC true
#define SHOW_WAIT_PANIC false
#define REDIR_IN_OFLAG O_RDONLY
#define REDIR_OUT_OFLAG (O_WRONLY | O_CREAT | O_TRUNC)

int pipe_fd[2];
#define CHAR_BUF_SIZE 1024
char char_buf[CHAR_BUF_SIZE];
string home_dir;
std::map<std::string, std::string> alias_map;

void init_alias() {
    alias_map.insert(std::pair<std::string, std::string>("ll", "ls -l"));
}
vector<string> cmd_history;

void panic(string hint, bool exit_ = false, int exit_code = 0) {
    if (SHOW_PANIC)
        cerr << "[!ExpShell panic]: " << hint << endl;
    if (exit_)
        exit(exit_code);
}

bool is_white_space(char ch) { return WHITE_SPACE.find(ch) != string::npos; }

bool is_symbol(char ch) { return SYMBOL.find(ch) != string::npos; }

vector<string> string_split(const string &s, const string &delims) {
    vector<string> vec;
    int p = 0, q;
    while ((q = s.find_first_of(delims, p)) != string::npos) {
        if (q > p)
            vec.push_back(s.substr(p, q - p));
        p = q + 1;
    }
    if (p < s.length())
        vec.push_back(s.substr(p));
    return vec;
}

vector<string> string_split_protect(const string &str, const string &delims) {
    vector<string> vec;
    string tmp = "";
    for (int i = 0; i < str.length(); i++) {
        if (is_white_space(str[i])) {
            vec.push_back(tmp);
            tmp = "";
        } else if (str[i] == '\"') {
            i++;
            while (str[i] != '\"' && i < str.length()) {
                tmp += str[i];
                i++;
            }
            if (i == str.length())
                panic("unclosed quote");
        } else
            tmp += str[i];
    }
    if (tmp.length() > 0)
        vec.push_back(tmp);
    return vec;
}

string string_split_last(const string &s, const string &delims) {
    vector<string> split_res = string_split(s, delims);
    return split_res.at(split_res.size() - 1);
}

string string_split_first(const string &s, const string &delims) {
    vector<string> split_res = string_split(s, delims);
    return split_res.at(0);
}

string trim(const string &s) {
    if (s.length() == 0)
        return string(s);
    int p = 0, q = s.length() - 1;
    while (is_white_space(s[p]))
        p++;
    while (is_white_space(s[q]))
        q--;
    return s.substr(p, q - p + 1);
}

string read_line() {
    string line;
    getline(cin, line);
    return line;
}

void show_command_prompt() {
    passwd *pwd = getpwuid(getuid());
    string username(pwd->pw_name);
    getcwd(char_buf, CHAR_BUF_SIZE);
    string cwd(char_buf);
    if (username == "root")
        home_dir = "/root";
    else
        home_dir = "/home/" + username;
    if (cwd == home_dir)
        cwd = "~";
    else if (cwd != "/") {
        cwd = string_split_last(cwd, "/");
    }
    gethostname(char_buf, CHAR_BUF_SIZE);
    string hostname(char_buf);
    hostname = string_split_first(hostname, ".");
    cout << "[" << username << "@" << hostname << " " << cwd << "]> ";
}

int fork_wrap() {
    int pid = fork();
    if (pid == -1)
        panic("fork failed.", true, 1);
    return pid;
}

int pipe_wrap(int pipe_fd[2]) {
    int ret = pipe(pipe_fd);
    if (ret == -1)
        panic("pipe failed", true, 1);
    return ret;
}

int dup2_wrap(int fd1, int fd2) {
    int dup2_ret = dup2(fd1, fd2);
    if (dup2_ret < 0)
        panic("dup2 failed.", true, 1);
    return dup2_ret;
}

int open_wrap(const char *file, int oflag) {
    int open_ret = open(file, oflag);
    if (open_ret < 0)
        panic("open failed.", true, 1);
    return open_ret;
}

void check_wait_status(int &wait_status) {
    if (WIFEXITED(wait_status) == 0) {
        char buf[8];
        sprintf(buf, "%d", WEXITSTATUS(wait_status));
        if (SHOW_WAIT_PANIC)
            panic("child exit with code " + string(buf));
    }
}

class cmd {
public:
    int type;
    cmd() { this->type = 0; }
};

class exec_cmd : public cmd {
public:
    vector<string> argv;
    exec_cmd(vector<string> &argv) {
        this->type = 1;
        this->argv = vector<string>(argv);
    }
};

class pipe_cmd : public cmd {
public:
    cmd *left;
    cmd *right;
    pipe_cmd() { this->type = 2; }
    pipe_cmd(cmd *left, cmd *right) {
        this->type = 2;
        this->left = left;
        this->right = right;
    }
};

class redirect_cmd : public cmd {
public:
    cmd *cmd_;
    string file;
    int fd;
    redirect_cmd() {}
    redirect_cmd(int type, cmd *cmd_, string file, int fd) {
        this->type = type;
        this->cmd_ = cmd_;
        this->file = file;
        this->fd = fd;
    }
};

cmd *parse_exec_cmd(string seg) {
    seg = trim(seg);
    vector<string> argv = string_split_protect(seg, WHITE_SPACE);
    return new exec_cmd(argv);
}

cmd *parse(string line) {
    line = trim(line);
    string cur_read = "";
    cmd *cur_cmd = new cmd();
    int i = 0;
    while (i < line.length()) {
        if (line[i] == '<' || line[i] == '>') {
            cmd *lhs = parse_exec_cmd(cur_read);
            int j = i + 1;
            while (j < line.length() && !is_symbol(line[j]))
                j++;
            string file = trim(line.substr(i + 1, j - i));
            cur_cmd = new redirect_cmd(line[i] == '<' ? 4 : 8, lhs, file, -1);
            i = j;
        } else if (line[i] == '|') {
            cmd *rhs = parse(line.substr(i + 1));
            if (cur_cmd->type == 0)
                cur_cmd = parse_exec_cmd(cur_read);
            cur_cmd = new pipe_cmd(cur_cmd, rhs);
            return cur_cmd;
        } else
            cur_read += line[i++];
    }
    if (cur_cmd->type == 0)
        return parse_exec_cmd(cur_read);
    else
        return cur_cmd;
}

int process_builtin_command(string line) {
    if (line == "cd") {
        chdir(home_dir.c_str());
        return 1;
    } else if (line.substr(0, 2) == "cd") {
        string arg1 = string_split(line, WHITE_SPACE)[1];
        if (arg1.find("~") == 0)
            line = "cd " + home_dir + arg1.substr(1);
        int chdir_ret = chdir(trim(line.substr(2)).c_str());
        if (chdir_ret < 0) {
            panic("chdir failed");
            return -1;
        } else
            return 1;
    }
    if (line == "quit") {
        cout << "Bye from ExpShell." << endl;
        exit(0);
    }
    if (line == "history") {
        for (int i = cmd_history.size() - 1; i >= 0; i--)
            cout << "\t" << i << "\t" << cmd_history.at(i) << endl;
        return 1;
    }
    return 0;
}

void run_cmd(cmd *cmd_) {
    switch (cmd_->type) {
        case 1: {
            exec_cmd *ecmd = static_cast<exec_cmd *>(cmd_);
            if (alias_map.count(ecmd->argv[0]) != 0) {
                vector<string> arg0_replace =
                        string_split(alias_map.at(ecmd->argv[0]), WHITE_SPACE);
                ecmd->argv.erase(ecmd->argv.begin());
                for (vector<string>::reverse_iterator it = arg0_replace.rbegin();
                     it < arg0_replace.rend(); it++) {
                    ecmd->argv.insert(ecmd->argv.begin(), (*it));
                }
            }
            vector<char *> argv_c_str;
            for (int i = 0; i < ecmd->argv.size(); i++) {
                string arg_trim = trim(ecmd->argv[i]);
                if (arg_trim.length() > 0) {
                    char *tmp = new char[MAX_ARGV_LEN];
                    strcpy(tmp, arg_trim.c_str());
                    argv_c_str.push_back(tmp);
                }
            }
            argv_c_str.push_back(NULL);
            char **argv_c_arr = &argv_c_str[0];
            int execvp_ret = execvp(argv_c_arr[0], argv_c_arr);
            if (execvp_ret < 0)
                panic("execvp failed");
            break;
        }
        case 2: {
            pipe_cmd *pcmd = static_cast<pipe_cmd *>(cmd_);
            pipe_wrap(pipe_fd);
            if (fork_wrap() == 0) {
                close(pipe_fd[0]);
                dup2_wrap(pipe_fd[1], fileno(stdout));
                run_cmd(pcmd->left);
                close(pipe_fd[1]);
            }
            if (fork_wrap() == 0) {
                close(pipe_fd[1]);
                dup2_wrap(pipe_fd[0], fileno(stdin));
                run_cmd(pcmd->right);
                close(pipe_fd[0]);
            }
            close(pipe_fd[0]);
            close(pipe_fd[1]);
            int wait_status_1, wait_status_2;
            wait(&wait_status_1);
            wait(&wait_status_2);
            check_wait_status(wait_status_1);
            check_wait_status(wait_status_2);
            break;
        }
        case 4:
        case 8: {
            redirect_cmd *rcmd = static_cast<redirect_cmd *>(cmd_);
            if (fork_wrap() == 0) {
                rcmd->fd = open_wrap(rcmd->file.c_str(), rcmd->type == 4 ? REDIR_IN_OFLAG : REDIR_OUT_OFLAG);
                dup2_wrap(rcmd->fd, rcmd->type == 4 ? fileno(stdin) : fileno(stdout));
                run_cmd(rcmd->cmd_);
                close(rcmd->fd);
            }
            int wait_status;
            wait(&wait_status);
            check_wait_status(wait_status);
            break;
        }
        default:
            panic("unknown or null cmd type", true, 1);
    }
}

int main() {
    init_alias();
    string line;
    int wait_status;
    while (true) {
        show_command_prompt();
        line = trim(read_line());
        cmd_history.push_back(line);
        if (process_builtin_command(line) > 0)
            continue;
        if (fork_wrap() == 0) {
            cmd *cmd_ = parse(line);
            run_cmd(cmd_);
            exit(0);
        }
        wait(&wait_status);
        check_wait_status(wait_status);
    }
    return 0;
}
