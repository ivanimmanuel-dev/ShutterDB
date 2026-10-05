#include "common.hpp"
#include <iostream>
#include <random>

int main() {
    using namespace shutter::detail;
    auto valid = file_header();
    for (std::uint64_t i = 1; i <= 20; ++i) {
        auto record = encode(Kind::put, i, "key" + std::to_string(i), std::as_bytes(std::span("payload", 7)));
        valid.insert(valid.end(), record.begin(), record.end());
    }
    std::mt19937 random(0x53484442);
    for (std::size_t n = 0; n <= valid.size(); ++n)
        shutter::fuzz::exercise(std::span(valid).first(n));
    for (int n = 0; n < 20000; ++n) {
        Bytes bytes;
        if (n % 2 == 0) {
            bytes = valid;
            for (unsigned m = 0; m <= random() % 8; ++m)
                bytes[random() % bytes.size()] ^= std::byte(random() & 255);
            if (random() % 3 == 0)
                bytes.resize(random() % bytes.size());
        } else {
            bytes.resize(random() % 4096);
            for (auto &b : bytes)
                b = std::byte(random() & 255);
        }
        shutter::fuzz::exercise(bytes);
    }
    std::cout << "20000 deterministic mutations plus every seed truncation passed\n";
}
