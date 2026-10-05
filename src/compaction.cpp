#include "internal.hpp"
#include <algorithm>

namespace shutter {
using namespace detail;
void DB::Impl::recover_compaction() {
    const auto backup = sibling(path, ".backup");
    if (detail::exists(backup)) {
        bool primary_valid = false;
        if (detail::exists(path)) {
            File primary(path, File::Mode::read_only);
            auto check = scan(primary, options).report;
            // Resource/IO failures must not trigger rollback of a possibly valid primary.
            if (check.issue && check.issue->code != ErrorCode::corruption &&
                check.issue->code != ErrorCode::unsupported_format)
                require_valid(check);
            primary_valid = check.ok();
        }
        if (!primary_valid) {
            {
                File original(backup, File::Mode::read_only);
                require_valid(scan(original, options).report);
            }
            replace(backup, path);
            sync_directory(path);
        } else {
            remove_file(backup);
            sync_directory(path);
        }
    }
    // These names are reserved. The stable lock covers cleanup across replacement.
    remove_file(sibling(path, ".compact"));
    remove_file(sibling(path, ".backup.tmp"));
}
void DB::Impl::compact() {
    hit(Fault::compaction_start);
    require_valid(scan(*file, options).report);
    const auto temp = sibling(path, ".compact");
    const auto backup_temp = sibling(path, ".backup.tmp");
    const auto backup = sibling(path, ".backup");
    try {
        auto compacted = std::make_unique<File>(temp, File::Mode::create_exclusive);
        compacted->write(0, file_header(state.report.stats.last_sequence));
        std::uint64_t offset = file_header_size;
        std::vector<Entry> entries;
        entries.reserve(state.index.size());
        for (const auto &[key, entry] : state.index) {
            (void)key;
            entries.push_back(entry);
        }
        std::sort(entries.begin(), entries.end(),
                  [](const Entry &a, const Entry &b) { return a.sequence < b.sequence; });
        for (const auto &entry : entries) {
            Bytes bytes(entry.total_size);
            file->read(entry.offset, bytes);
            compacted->write(offset, bytes);
            hit(Fault::temporary_write);
            offset += bytes.size();
        }
        hit(Fault::temporary_validation);
        auto next = scan(*compacted, options);
        require_valid(next.report);
        compacted->sync();
        // A durable backup makes failures around replacement explicitly recoverable.
        file->sync();
        copy_durable(*file, backup_temp);
        replace(backup_temp, backup);
        sync_directory(path);
        hit(Fault::before_replacement);
        // The sidecar lock stays held even while Windows data handles are closed.
        compacted.reset();
        file.reset();
        replace(temp, path);
        hit(Fault::after_replacement);
        sync_directory(path);
        hit(Fault::after_directory_sync);
        file = std::make_unique<File>(path, File::Mode::read_write);
        state = std::move(next);
        remove_file(backup);
        sync_directory(path);
    } catch (...) {
        // Keep all evidence for the next open; never continue on a possibly replaced handle.
        poisoned = true;
        throw;
    }
}
} // namespace shutter
