#include <chrono>
#include <iostream>
#include <shutter/db.hpp>

int main() {
    const auto path = std::filesystem::temp_directory_path() /
                      ("shutter-example-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".shdb");
    try {
        {
            shutter::DB db(path);
            db.put("hello", "world");
            if (auto value = db.get_string("hello"))
                std::cout << *value << '\n';
        }
        {
            shutter::DB reopened(path);
            if (reopened.get_string("hello") != "world")
                return 1;
            reopened.remove("hello");
            reopened.compact();
            if (!reopened.verify().ok())
                return 2;
        }
        std::filesystem::remove(path);
        auto lock = path;
        lock += ".lock";
        std::filesystem::remove(lock);
    } catch (const shutter::Error &e) {
        std::cerr << e.what() << '\n';
        return 3;
    }
}
