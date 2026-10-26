// Exception handling for owned clipboard scenarios, independent of Win32 APIs.
#pragma once
#include <exception>
#include <ostream>

namespace tf_shell_test {
// Call from the primary clipboard exception handler. Cleanup failure remains
// secondary; its fixed diagnostic must not disclose an exception's contents.
template<class Cleanup>
[[noreturn]] void rethrow_clipboard_failure(Cleanup&& cleanup, std::ostream& diagnostic) {
    const auto primary = std::current_exception();
    try { cleanup(); }
    catch(...) {
        try { diagnostic << "[TF-SELECTION-CLIPBOARD] owned cleanup failed\n"; }
        catch(...) {} // Even a failed diagnostic stream cannot replace primary.
    }
    std::rethrow_exception(primary);
}
} // namespace tf_shell_test
