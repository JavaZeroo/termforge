// Immutable raw-byte expectation for the fixed Microsoft Pinyin fixture.
#pragma once
#include <windows.h>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>

namespace tf_shell_test {
class ImeInputExpectation {
    const std::string prefix_,committed_;
    static std::string checked(std::string prefix) {
        if(prefix.size()>4096*4) throw std::invalid_argument("IME prefix exceeds the raw receiver byte bound");
        if(!prefix.empty()) {
            const int units=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,prefix.data(),static_cast<int>(prefix.size()),nullptr,0);
            if(units<=0 || units>4094) throw std::invalid_argument("IME prefix must be valid UTF-8 and leave two of 4096 UTF-16 units for the commit");
        }
        return prefix;
    }
public:
    explicit ImeInputExpectation(std::string prefix={})
        :prefix_(checked(std::move(prefix))),committed_(prefix_+u8"你好") {}
    const std::string& prefix() const noexcept { return prefix_; }
    bool before_commit(std::string_view received) const noexcept { return received==std::string_view(prefix_); }
    bool after_commit(std::string_view received) const noexcept { return received==std::string_view(committed_); }
};
} // namespace tf_shell_test
