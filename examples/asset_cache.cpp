#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <openssl/evp.h>
#include <shutter/db.hpp>

namespace {
std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("cannot open input: " + path.string());
    const auto size = input.tellg();
    if (size < 0 || size > 64 * 1024 * 1024)
        throw std::runtime_error("input exceeds 64 MiB: " + path.string());
    std::string bytes(static_cast<std::size_t>(size), '\0');
    input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("input read failed: " + path.string());
    return bytes;
}
std::string cache_key(std::string_view bytes) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned size = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), digest.data(), &size, EVP_sha256(), nullptr) != 1 ||
        size != 32)
        throw std::runtime_error("SHA-256 failed");
    std::string key = "preview-v1:128:";
    for (unsigned i = 0; i < size; ++i) {
        key += "0123456789abcdef"[digest[i] >> 4];
        key += "0123456789abcdef"[digest[i] & 15];
    }
    return key;
}
class Image {
    std::string_view bytes_;
    std::size_t position_ = 0;
    std::string_view token() {
        for (;;) {
            while (position_ < bytes_.size() && std::isspace(static_cast<unsigned char>(bytes_[position_])))
                ++position_;
            if (position_ == bytes_.size() || bytes_[position_] != '#')
                break;
            while (position_ < bytes_.size() && bytes_[position_] != '\n')
                ++position_;
        }
        const auto start = position_;
        while (position_ < bytes_.size() && !std::isspace(static_cast<unsigned char>(bytes_[position_])))
            ++position_;
        return bytes_.substr(start, position_ - start);
    }
    std::size_t number() {
        const auto text = token();
        std::size_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size())
            throw std::runtime_error("invalid PPM header");
        return value;
    }

  public:
    explicit Image(std::string_view bytes) : bytes_(bytes) {}
    std::string preview() {
        if (token() != "P6")
            throw std::runtime_error("expected a binary P6 PPM image");
        const auto width = number(), height = number(), maximum = number();
        if (!width || !height || width > 4096 || height > 4096 || maximum != 255)
            throw std::runtime_error("expected dimensions 1..4096 and 8-bit RGB");
        if (position_ == bytes_.size())
            throw std::runtime_error("missing PPM pixels");
        const auto separator = bytes_[position_++];
        if (separator == '\r' && bytes_.size() - position_ == width * height * 3 + 1 &&
            bytes_[position_] == '\n')
            ++position_;
        if (bytes_.size() - position_ != width * height * 3)
            throw std::runtime_error("PPM pixel count does not match its dimensions");
        const auto longest = std::max({width, height, std::size_t{128}});
        const auto out_width = std::max(std::size_t{1}, width * 128 / longest);
        const auto out_height = std::max(std::size_t{1}, height * 128 / longest);
        std::string result =
            "P6\n" + std::to_string(out_width) + " " + std::to_string(out_height) + "\n255\n";
        for (std::size_t y = 0; y < out_height; ++y) {
            for (std::size_t x = 0; x < out_width; ++x) {
                const auto x0 = x * width / out_width, x1 = (x + 1) * width / out_width;
                const auto y0 = y * height / out_height, y1 = (y + 1) * height / out_height;
                std::array<std::uint32_t, 3> sums{};
                for (auto row = y0; row < y1; ++row)
                    for (auto column = x0; column < x1; ++column)
                        for (std::size_t channel = 0; channel < 3; ++channel)
                            sums[channel] += static_cast<unsigned char>(
                                bytes_[position_ + (row * width + column) * 3 + channel]);
                const auto samples = (x1 - x0) * (y1 - y0);
                assert(samples > 0); // Downscaling assigns at least one source pixel to each output pixel.
                for (const auto sum : sums)
                    result += static_cast<char>(sum / samples);
            }
        }
        return result;
    }
};
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 4)
            throw std::runtime_error("usage: shutter_asset_cache CACHE INPUT_DIRECTORY OUTPUT_DIRECTORY");
        const auto cache = std::filesystem::weakly_canonical(argv[1]);
        const auto input = std::filesystem::weakly_canonical(argv[2]);
        const auto output = std::filesystem::weakly_canonical(argv[3]);
        if (input == output || cache.parent_path() == output)
            throw std::runtime_error("use a separate output directory for previews");
        std::vector<std::filesystem::path> sources;
        for (const auto &entry : std::filesystem::directory_iterator(input))
            if (entry.is_regular_file() && entry.path().extension() == ".ppm")
                sources.push_back(entry.path());
        std::sort(sources.begin(), sources.end());
        std::filesystem::create_directories(output);
        const auto start = std::chrono::steady_clock::now();
        shutter::DB db(cache, {.sync_writes = false});
        std::size_t hits = 0, generated = 0;
        for (const auto &path : sources) {
            const auto source = read_file(path);
            const auto key = cache_key(source);
            auto preview = db.get_string(key);
            if (preview)
                ++hits;
            else {
                preview = Image(source).preview();
                db.put(key, *preview);
                if (++generated % 128 == 0)
                    db.sync();
            }
            std::ofstream file(output / path.filename(), std::ios::binary);
            if (!file.write(preview->data(), static_cast<std::streamsize>(preview->size())))
                throw std::runtime_error("preview write failed: " + path.filename().string());
            file.close();
            if (!file)
                throw std::runtime_error("preview close failed: " + path.filename().string());
        }
        if (generated % 128)
            db.sync();
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "{\"generated\":" << generated << ",\"hits\":" << hits << ",\"keys\":" << db.stats().keys
                  << ",\"cache_bytes\":" << db.stats().database_bytes << ",\"seconds\":" << seconds << "}\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
