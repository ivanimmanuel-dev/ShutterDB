#pragma once
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <shutter/options.hpp>
#include <shutter/result.hpp>
#include <shutter/stats.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shutter {
inline constexpr std::string_view version = "0.1.0-dev";
class DB {
  public:
    explicit DB(const std::filesystem::path &path, const Options &options = {});
    ~DB();
    DB(const DB &) = delete;
    DB &operator=(const DB &) = delete;
    DB(DB &&) = delete;
    DB &operator=(DB &&) = delete;
    void put(std::string_view key, std::span<const std::byte> value);
    void put(std::string_view key, std::string_view value);
    std::optional<std::vector<std::byte>> get(std::string_view key) const;
    std::optional<std::string> get_string(std::string_view key) const;
    bool remove(std::string_view key);
    bool contains(std::string_view key) const;
    Stats stats() const;
    void sync();
    void compact();
    VerifyReport verify() const;
    // Never modifies the database file or performs recovery. Takes a shared lock.
    static VerifyReport inspect(const std::filesystem::path &path, const Options &options = {});

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace shutter
