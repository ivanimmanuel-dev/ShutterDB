#include "jpeg_decode.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <openssl/evp.h>
#include <png.h>
#include <shutter/cache.hpp>

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
    std::string key = "preview-v2:128:";
    for (unsigned i = 0; i < size; ++i) {
        key += "0123456789abcdef"[digest[i] >> 4];
        key += "0123456789abcdef"[digest[i] & 15];
    }
    return key;
}
struct Png {
    png_image image{};
    Png() { image.version = PNG_IMAGE_VERSION; }
    ~Png() { png_image_free(&image); }
};
struct Image {
    unsigned width = 0, height = 0;
    std::vector<unsigned char> pixels;
    explicit Image(std::string_view bytes) {
        const auto *data = reinterpret_cast<const unsigned char *>(bytes.data());
        if (bytes.size() >= 8 && png_sig_cmp(data, 0, 8) == 0) {
            Png png;
            if (!png_image_begin_read_from_memory(&png.image, data, bytes.size()))
                throw std::runtime_error(png.image.message);
            width = png.image.width;
            height = png.image.height;
            if (!width || !height || width > 4096 || height > 4096)
                throw std::runtime_error("expected dimensions 1..4096");
            png.image.format = PNG_FORMAT_RGBA;
            pixels.resize(PNG_IMAGE_SIZE(png.image));
            if (!png_image_finish_read(&png.image, nullptr, pixels.data(), 0, nullptr))
                throw std::runtime_error(png.image.message);
        } else if (bytes.size() >= 2 && data[0] == 0xff && data[1] == 0xd8) {
            std::array<char, 256> message{};
            const std::unique_ptr<unsigned char, decltype(&std::free)> decoded(
                shutter_decode_jpeg(data, bytes.size(), &width, &height, message.data(), message.size()),
                &std::free);
            if (!decoded)
                throw std::runtime_error(message.data());
            pixels.assign(decoded.get(), decoded.get() + static_cast<std::size_t>(width) * height * 4);
        } else
            throw std::runtime_error("expected PNG or JPEG image bytes");
    }
    std::string preview() const {
        const auto longest = std::max({width, height, 128U});
        const auto out_width = std::max(1U, width * 128 / longest);
        const auto out_height = std::max(1U, height * 128 / longest);
        std::vector<unsigned char> output(static_cast<std::size_t>(out_width) * out_height * 4);
        for (unsigned y = 0; y < out_height; ++y) {
            for (unsigned x = 0; x < out_width; ++x) {
                const auto x0 = x * width / out_width, x1 = (x + 1) * width / out_width;
                const auto y0 = y * height / out_height, y1 = (y + 1) * height / out_height;
                std::array<std::uint64_t, 4> sums{};
                for (auto row = y0; row < y1; ++row)
                    for (auto column = x0; column < x1; ++column) {
                        const auto offset = (static_cast<std::size_t>(row) * width + column) * 4;
                        const auto alpha = pixels[offset + 3];
                        for (std::size_t channel = 0; channel < 3; ++channel)
                            sums[channel] += static_cast<unsigned>(pixels[offset + channel]) * alpha;
                        sums[3] += alpha;
                    }
                const auto samples = (x1 - x0) * (y1 - y0);
                assert(samples > 0); // Output dimensions never exceed the source dimensions.
                const auto offset = (static_cast<std::size_t>(y) * out_width + x) * 4;
                for (std::size_t channel = 0; channel < 3; ++channel)
                    output[offset + channel] =
                        static_cast<unsigned char>(sums[3] ? sums[channel] / sums[3] : 0);
                output[offset + 3] = static_cast<unsigned char>(sums[3] / samples);
            }
        }
        Png png;
        png.image.width = out_width;
        png.image.height = out_height;
        png.image.format = PNG_FORMAT_RGBA;
        png_alloc_size_t size = 0;
        if (!png_image_write_to_memory(&png.image, nullptr, &size, 0, output.data(), 0, nullptr))
            throw std::runtime_error(png.image.message);
        std::string encoded(size, '\0');
        if (!png_image_write_to_memory(&png.image, encoded.data(), &size, 0, output.data(), 0, nullptr))
            throw std::runtime_error(png.image.message);
        encoded.resize(size);
        return encoded;
    }
};
std::uint64_t budget(std::string_view input) {
    std::uint64_t mib = 0;
    const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), mib);
    if (error != std::errc{} || end != input.data() + input.size() || !mib ||
        mib > std::numeric_limits<std::uint64_t>::max() / (1024 * 1024))
        throw std::runtime_error("--max-mib requires a positive integer");
    return mib * 1024 * 1024;
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 4 && (argc != 6 || std::string_view(argv[4]) != "--max-mib"))
            throw std::runtime_error(
                "usage: shutter_asset_cache CACHE INPUT_DIRECTORY OUTPUT_DIRECTORY [--max-mib N]");
        shutter::CacheOptions options;
        if (argc == 6)
            options.max_bytes = budget(argv[5]);
        const auto cache_path = std::filesystem::weakly_canonical(argv[1]);
        const auto input = std::filesystem::weakly_canonical(argv[2]);
        const auto output = std::filesystem::weakly_canonical(argv[3]);
        if (input == output || cache_path.parent_path() == output)
            throw std::runtime_error("use a separate output directory for previews");
        std::vector<std::filesystem::path> sources;
        for (const auto &entry : std::filesystem::directory_iterator(input)) {
            auto extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (entry.is_regular_file() &&
                (extension == ".png" || extension == ".jpg" || extension == ".jpeg"))
                sources.push_back(entry.path());
        }
        std::sort(sources.begin(), sources.end());
        std::filesystem::create_directories(output);
        const auto start = std::chrono::steady_clock::now();
        shutter::Cache cache(cache_path, options);
        std::size_t hits = 0, generated = 0;
        for (const auto &path : sources) {
            const auto source = read_file(path);
            const auto key = cache_key(source);
            auto preview = cache.get_string(key);
            if (preview)
                ++hits;
            else {
                preview = Image(source).preview();
                cache.put(key, *preview);
                if (++generated % 128 == 0)
                    cache.sync();
            }
            auto destination = output / path.filename();
            destination += ".png";
            std::ofstream file(destination, std::ios::binary);
            if (!file.write(preview->data(), static_cast<std::streamsize>(preview->size())))
                throw std::runtime_error("preview write failed: " + path.filename().string());
            file.close();
            if (!file)
                throw std::runtime_error("preview close failed: " + path.filename().string());
        }
        if (generated % 128)
            cache.sync();
        const auto stats = cache.stats();
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "{\"generated\":" << generated << ",\"hits\":" << hits
                  << ",\"keys\":" << stats.storage.keys << ",\"cache_bytes\":" << stats.storage.database_bytes
                  << ",\"max_bytes\":" << stats.max_bytes << ",\"evictions\":" << stats.evictions
                  << ",\"seconds\":" << seconds << "}\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
