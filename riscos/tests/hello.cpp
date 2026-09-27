// Checks that the C++11 features Vanilla Conquer relies on (std::chrono,
// std::stof, std::snprintf) plus libstdc++ and UnixLib work on the target.
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

int main()
{
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::unique_ptr<std::string>> v;
    for (int i = 0; i < 3; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "item%d", i);
        v.emplace_back(new std::string(buf));
    }
    std::map<std::string, int> m;
    for (auto& s : v) {
        m[*s] = static_cast<int>(s->size());
    }
    auto sum = 0;
    for (const auto& kv : m) {
        sum += kv.second;
    }
    float f = std::stof("2.5");
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    char c = static_cast<char>(0xFF);
    std::printf("hello from RISC OS: sizeof(void*)=%u sizeof(long)=%u sum=%d stof=%.1f char_signed=%d elapsed_us=%ld\n",
                unsigned(sizeof(void*)), unsigned(sizeof(long)), sum, f, c < 0, long(us));
    return sum == 15 && f == 2.5f ? 0 : 1;
}
