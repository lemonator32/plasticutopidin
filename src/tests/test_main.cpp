#include "test_main.h"

std::vector<TestCase>& allTests() {
    static std::vector<TestCase> t;
    return t;
}
int& failureCount() {
    static int n = 0;
    return n;
}

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& t : allTests()) {
        if (filter && std::string(t.name).find(filter) == std::string::npos) continue;
        int before = failureCount();
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf("[ %s ] %s\n", failureCount() == before ? " OK " : "FAIL", t.name);
        ++ran;
    }
    std::printf("%d tests, %d failures\n", ran, failureCount());
    return failureCount() == 0 ? 0 : 1;
}
