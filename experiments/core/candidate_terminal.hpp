#pragma once
#include "screen.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace termforge {
struct CandidateEvent {
    uint32_t kind = 0;
    std::string bytes;
};

class CandidateTerminal {
    void* impl_ = nullptr;
    uint64_t mode_bits_ = 0;
    std::unordered_map<uint64_t, uint64_t> versions_;
    std::vector<CandidateEvent> events_;

    void synchronize();
    static void check(int status, const char* message);

public:
    Screen screen;

    CandidateTerminal(int rows = 24, int columns = 80, size_t history = 100);
    ~CandidateTerminal();
    CandidateTerminal(const CandidateTerminal&) = delete;
    CandidateTerminal& operator=(const CandidateTerminal&) = delete;

    void feed(std::string_view bytes);
    void finish();
    void resize(int rows, int columns);
    uint64_t mode_bits() const { return mode_bits_; }
    std::vector<CandidateEvent> take_events();
};
} // namespace termforge
