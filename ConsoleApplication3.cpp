#include <iostream>
#include <list>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>

using namespace std;
mutex mtx;

struct Process {
    int pid;
    int remainingTime;
    int wakeupTime;
    bool isForeground;
    bool isPromoted;
};

struct DQNode {
    list<Process> processes;
    DQNode* next;
};

class DynamicQueue {
private : 
    DQNode* bottom;
    DQNode* top;
    mutex mtx;
    condition_variable cv;
    int threshold;
    int totalProcesses;
    int currentProcesses;
    int promotionIndex;

public : 
    DynamicQueue(int totalProcesses) : bottom(nullptr), top(nullptr), threshold(totalProcesses / 3), totalProcesses(totalProcesses), currentProcesses(0), promotionIndex(0) {}

    void enqueue(Process p) {
        lock_guard<mutex> lock(mtx);
        if (p.isForeground) {
            if (!top) {
                top = new DQNode;
                top->processes.push_back(p);
                bottom = top;
            }
            else top->processes.push_back(p);
        }
        else {
            if (!bottom) {
                bottom = new DQNode;
                bottom->processes.push_back(p);
                top = bottom;
            }
            else  bottom->processes.push_back(p);
        }
        currentProcesses++;
        cv.notify_one();
        split_n_merge();
    }

    Process dequeue() {
        unique_lock<mutex> lock(mtx);
        while (top == nullptr) {
            cv.wait(lock);
        }
        Process p = top->processes.front();
        top->processes.pop_front();
        if (top->processes.empty()) {
            DQNode* temp = top;
            top = top->next;
            delete temp;
        }
        currentProcesses--;
        promote();
        return p;
    }

    void promote() {
        lock_guard<mutex> lock(mtx);
        DQNode* p = bottom;
        while (p != top) {
            if (!p->processes.empty()) {
                p->processes.front().isPromoted = true;
                top->processes.push_back(p->processes.front());
                p->processes.pop_front();
                if (p->processes.empty()) {
                    DQNode* temp = p;
                    p = p->next;
                    delete temp;
                }
                else  p = p->next;
            }
            else p = p->next;
        }
        split_n_merge();
    }

    void split_n_merge() {
        lock_guard<mutex> lock(mtx);
        if (top && top->processes.size() > threshold) {
            int halfSize = top->processes.size() / 2;
            list<Process> temp;
            for (int i = 0; i < halfSize; i++) {
                temp.push_back(top->processes.front());
                top->processes.pop_front();
            }
            DQNode* newNode = new DQNode;
            newNode->processes = move(temp);
            newNode->next = top;
            top = newNode;
            split_n_merge();
        }
    }

    int getCurrentProcesses() {
        return currentProcesses;
    }

    DQNode* getBottom() {
        return bottom;
    }
};

class ProcessManager {
private : 
    DynamicQueue dq;
    list<Process> waitQueue;
    mutex mtx;
    condition_variable cv;
    int currentTime;

public : 
    ProcessManager(int totalProcesses) : dq(totalProcesses), currentTime(0) {}

    void createProcess(bool isForeground, int duration) {
        lock_guard<mutex> lock(mtx);
        Process p = { currentProcesses(), isForeground, duration, false, currentTime + duration };
        dq.enqueue(p);
        cv.notify_one();
    }

    void runProcesses() {
        while (true) {
            lock_guard<mutex> lock(mtx);
            if (dq.getCurrentProcesses() == 0 && waitQueue.empty()) break;
            Process p = dq.dequeue();
            cout << "Running: [" << p.pid << (p.isForeground ? "F" : "B") << "]" << endl;
            cout << "------------------------------" << endl;
            cout << "DQ: ";
            printDQ();
            cout << "-------------------------------" << endl;
            cout << "WQ: ";
            printWQ();
            cout << endl;
            this_thread::sleep_for(chrono::seconds(1));
            p.remainingTime--;
            if (p.remainingTime > 0) {
                waitQueue.push_back(p);
                waitQueue.sort([](const Process& a, const Process& b) {
                    return a.wakeupTime < b.wakeupTime;
                    });
            }
            currentTime++;
            cv.notify_one();
            wakeUpProcesses();
        }
    }

    void wakeUpProcesses() {
        lock_guard<mutex> lock(mtx);
        while (!waitQueue.empty() && waitQueue.front().wakeupTime <= currentTime) {
            Process p = waitQueue.front();
            waitQueue.pop_front();
            dq.enqueue(p);
        }
    }

    int currentProcesses() {
        return dq.getCurrentProcesses() + waitQueue.size();
    }

    void printDQ() {
        lock_guard<mutex> lock(mtx);
        DQNode* p = dq.getBottom();
        cout << "p => [";
        while (p) {
            for (const auto& process : p->processes) {
                cout << process.pid << (process.isForeground ? "F" : "B") << (process.isPromoted ? "*" : "") << " ";
            }
            p = p->next;
        }
        cout << "]" << endl;
    }

    void printWQ() {
        lock_guard<mutex> lock(mtx);
        cout << "[";
        for (const auto& process : waitQueue) {
            cout << process.pid << (process.isForeground ? "F" : "B") << ":" << process.remainingTime << " ";
        }
        cout << "]" << endl;
    }
};

char** parse(const char* command) {
    vector<string> tokens;
    string cmd(command);
    size_t pos = 0;
    string delimiter = " ";
    while ((pos = cmd.find(delimiter)) != string::npos) {
        tokens.push_back(cmd.substr(0, pos));
        cmd.erase(0, pos + delimiter.length());
    }
    tokens.push_back(cmd);
    char** args = new char* [tokens.size() + 1];
    for (size_t i = 0; i < tokens.size(); i++) {
        args[i] = strdup(tokens[i].c_str());
    }
    args[tokens.size()] = strdup("");
    return args;
}

void exec(char** args) {
    for (int i = 0; args[i]; i++) free(args[i]);
    delete[] args;
}

void processCommand(string command) {
    vector<string> args;
    stringstream ss(command);
    string arg;
    while (getline(ss, arg, ';')) {
        args.push_back(arg);
    }

    for (string& a : args) {
        stringstream ss2(a);
        string cmd;
        ss2 >> cmd;

        if (cmd == "echo") {
            string str;
            ss2 >> str;
            cout << str << endl;
        }
        else if (cmd == "dummy") {
            int n;
            ss2 >> n;
            for (int i = 0; i < n; i++) {
                // 아무 일도 하지 않는 프로세스 생성
            }
        }
        else if (cmd == "gcd") {
            int x, y;
            ss2 >> x >> y;
            int a = max(x, y), b = min(x, y);
            while (b != 0) {
                int temp = b;
                b = a % b;
                a = temp;
            }
            cout << "GCD(" << x << ", " << y << ") = " << a << endl;
        }
        else if (cmd == "prime") {
            int x;
            ss2 >> x;
            vector<bool> is_prime(x + 1, true);
            is_prime[0] = is_prime[1] = false;
            for (int i = 2; i * i <= x; i++) {
                if (is_prime[i]) {
                    for (int j = i * i; j <= x; j += i) {
                        is_prime[j] = false;
                    }
                }
            }
            int count = 0;
            for (int i = 2; i <= x; i++) {
                if (is_prime[i]) {
                    count++;
                }
            }
            cout << "There are " << count << " prime numbers less than or equal to " << x << endl;
        }
        else if (cmd == "sum") {
            int x;
            ss2 >> x;
            int sum = 0;
            for (int i = 1; i <= x; i++) {
                sum += i;
            }
            cout << "The sum of 1 to " << x << " is " << (sum % 1000000) << endl;
        }
        else {
            cout << "Unknown command: " << cmd << endl;
        }
    }
}

int main() {
    ProcessManager pm(10);
    ifstream file("commands.txt");
    string line;
    int interval = 5;

    thread shellThread([&]() {
        while (true) {
            char command[100];
            cout << "$ ";
            cin.getline(command, 100);
            char** args = parse(command);
            pm.createProcess(true, 5);
            exec(args);
            this_thread::sleep_for(chrono::seconds(5));
        }
        while (getline(file, line)) {
            processCommand(line);
            this_thread::sleep_for(chrono::seconds(interval));
        }
     });

    thread monitorThread([&]() {
        while (true) {
            this_thread::sleep_for(chrono::seconds(2));
            mtx.lock();
            pm.printDQ();
            pm.printWQ();
            mtx.unlock();
        }
    });

    pm.runProcesses();

    shellThread.join();
    monitorThread.join();

    return 0;
}