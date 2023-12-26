#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <map>

#define HISTORY_SIZE 8

#define ORDER_TYPE_COMMON 1
#define ORDER_TYPE_PIPE 2
#define ORDER_TYPE_REDIRECT_IN 3
#define ORDER_TYPE_REDIRECT_OUT 4

std::map<std::string, std::string> alias_map;

void init_alias() {
    alias_map.insert(std::pair<std::string, std::string>("ll", "ls -l"));
}

class Order : public std::string {
public:
    std::vector<std::string> orderLine;
    int type = 0;

    void runOrder() {
        switch (type) {
            case ORDER_TYPE_COMMON: {
                if (orderLine[0] == "cd") {
                    // TODO: 处理cd命令
                }
                break;
            }
            case ORDER_TYPE_PIPE: {
                // TODO: 处理管道命令
                break;
            }
            case ORDER_TYPE_REDIRECT_IN:
            case ORDER_TYPE_REDIRECT_OUT: {
                // TODO: 处理重定向命令
                break;
            }
        }
    }
};

class Cmd {
private:
    bool state = true;
    std::string order;
    std::vector<std::string> commandLine;
    std::vector<Order> historyOrder;

    void setCommandLine() {
        commandLine = split(order, " ");
    }

    void markOrder() {
        std::string exe = commandLine[0];
        if (exe == "clear") {
            system("cls");
        } else if (exe == "exit" || exe == "quit") {
            state = false;
        }

        Order orderObj;
        orderObj.orderLine = commandLine;
        commandLine.push_back(orderObj);
    }

    static std::vector<std::string> split(const std::string& str, const std::string& delim) {
        std::vector<std::string> res;
        if (str.empty()) return res;

        char* strS = new char[str.length() + 1];
        strcpy(strS, str.c_str());

        char* d = new char[delim.length() + 1];
        strcpy(d, delim.c_str());

        char* p = strtok(strS, d);
        while (p) {
            std::string s = p;
            res.push_back(s);
            p = strtok(nullptr, d);
        }
        delete[] strS;
        delete[] d;
        return res;
    }

public:
    void loop() {
        do {
            std::cout << ">> ";
            std::getline(std::cin, order);
            setCommandLine();
            markOrder();
        } while (state);
        std::cout << std::endl;
        std::cout << "Bye" << std::endl;
    }
};

int main(int argc, char** argv) {
    init_alias();
    Cmd cmd;
    cmd.loop();
    return 0;
}
