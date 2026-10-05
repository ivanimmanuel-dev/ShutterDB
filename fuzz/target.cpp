#include "common.hpp"
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
    const auto bytes = std::as_bytes(std::span(data, size));
#ifdef SHUTTER_FUZZ_record
    try {
        if (size >= shutter::detail::record_header_size) {
            auto h = shutter::detail::decode_header(bytes.first(shutter::detail::record_header_size), 0);
            if (h.total_size <= size) {
                (void)shutter::detail::payload(bytes, h, 0);
            }
        } else
            (void)shutter::detail::plausible_partial_header(bytes);
    } catch (const shutter::Error &) {
    }
#else
    shutter::fuzz::exercise(bytes);
#endif
    return 0;
}
