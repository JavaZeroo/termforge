// Input-byte boundary for the fixed Pinyin workflow; no input/IME APIs.
#include "ime_input_expectation.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool value,const char* message) {
    if(!value) throw std::runtime_error(message);
}
int main() {
    try {
        const tf_shell_test::ImeInputExpectation plain;
        require(plain.before_commit("") && !plain.before_commit("nihao"),"preedit must not enter the raw receiver");
        require(plain.after_commit(u8"你好") && !plain.after_commit(u8"你好你好"),"exactly one fixed Pinyin commit is required");
        const tf_shell_test::ImeInputExpectation pasted(u8"TF_R108_PASTE 中文\rNEXT\r");
        require(pasted.before_commit(u8"TF_R108_PASTE 中文\rNEXT\r") && !pasted.before_commit(""),"the prior paste must remain in the receiver");
        require(pasted.after_commit(u8"TF_R108_PASTE 中文\rNEXT\r你好"),"commit must append to the complete paste prefix");
        require(!pasted.after_commit(u8"你好") && !pasted.after_commit(u8"TF_R108_PASTE 中文\rNEXT\r你好你好") &&
            !pasted.after_commit(u8"TF_R108_PASTE 中文\rNEXT\r你好\x1b"),"lost prefix, duplicate commit and cancellation bytes must be rejected");
        const auto rejected=[](const std::string& prefix) {
            try { const tf_shell_test::ImeInputExpectation value(prefix); return false; }
            catch(const std::invalid_argument&) { return true; }
        };
        const std::string full(4094,'P');
        require(tf_shell_test::ImeInputExpectation(full).after_commit(full+u8"你好"),"4094 UTF-16 prefix units leave exactly two for the fixed commit");
        require(rejected(std::string(4095,'P')),"the prefix must leave capacity for the final two UTF-16 units");
        std::string wide;
        for(int n=0;n<4094;++n) wide+=u8"中";
        require(tf_shell_test::ImeInputExpectation(wide).before_commit(wide),"capacity is counted in UTF-16 units rather than UTF-8 bytes");
        require(rejected("\xff") && rejected("\xed\xa0\x80") && rejected("\xf0\x9f"),"malformed UTF-8 and surrogate scalars must not become an expected prefix");
        std::cout<<"fixed Pinyin input prefix and exact-once boundaries passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
