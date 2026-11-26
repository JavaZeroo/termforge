// Layered DSR 6n reply evidence for the formal C++/Qt/fixed-ConPTY path.
//
// The confirmed core contract (experiments/core/reply_test.cpp) answers a cursor
// position query with ESC[row;colR, both 1-based. This test fixes one bounded
// primary-screen scenario (write two printable ASCII cells from home, then ask
// ESC[6n) and separates four layers:
//
//   1. raw fixed-ConPTY transport with no host reply: does conhost answer by
//      itself? (it must not; the pinned source's ConPTY path suppresses replies)
//   2. raw fixed-ConPTY transport with a distinct diagnostic reply: do bytes
//      written by the host reach the real child unchanged?
//   3. the product SessionModel path: core reply -> Qt write-back -> ConPTY ->
//      the real child receives the contract bytes ESC[1;3R.
//
// The child is this same executable re-entered through --query-child. It only
// enables the console's VT input/output modes, writes the fixed source once and
// records the exact UTF-16 input units it receives, so the observed bytes do not
// depend on the implementation under test. Test-only: no product behavior,
// default entry or query support is changed.
#include "terminal_session.hpp"
#include "../../experiments/conpty/session.hpp"
#include "../../experiments/core/text_projection.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using termforge::qt::SessionModel;

// Fixed bounded scenario: a fresh 80x24 primary screen writes two printable
// ASCII cells from home and then asks for the cursor position. The independent
// literal expectation follows the existing reply contract, not this test's
// implementation: 1-based row 1, column 3 => ESC[1;3R.
constexpr std::string_view query_source = "AB\x1b[6n";
constexpr std::string_view contract_reply = "\x1b[1;3R";
// A deliberately wrong position used only by the raw diagnostic control; it
// proves the host-written bytes reach the child and are not produced by conhost.
constexpr std::string_view distinct_reply = "\x1b[9;9R";
constexpr DWORD child_reply_timeout_ms = 4000;
constexpr ULONGLONG query_observe_timeout_ms = 5000;
constexpr ULONGLONG child_exit_timeout_ms = 8000;
constexpr ULONGLONG product_finish_timeout_ms = 12000;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string hex(std::string_view bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const unsigned char byte : bytes) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}

std::string read_binary(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(bool(input), "expected child result file is missing");
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void write_binary(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(bool(output), "child result file could not be created");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(bool(output), "child result file write failed");
}

// Minimal field extraction for the child's own bounded metadata envelope. The
// raw received bytes are also written separately and cross-checked; this parser
// never feeds the expected reply value.
std::string metadata_field(const std::string& json, std::string_view key) {
    const std::string quoted = "\"" + std::string(key) + "\":";
    const auto begin = json.find(quoted);
    require(begin != std::string::npos, "child metadata field is missing");
    auto value = begin + quoted.size();
    auto end = json.find_first_of(",}", value);
    require(end != std::string::npos, "child metadata field is unterminated");
    return json.substr(value, end - value);
}

std::string metadata_hex(const std::string& json, std::string_view key) {
    const auto field = metadata_field(json, key);
    require(field.size() >= 2 && field.front() == '"' && field.back() == '"',
            "child metadata hex field is not quoted");
    return field.substr(1, field.size() - 2);
}

bool metadata_bool(const std::string& json, std::string_view key) {
    const auto field = metadata_field(json, key);
    require(field == "true" || field == "false", "child metadata boolean field is invalid");
    return field == "true";
}

unsigned long metadata_number(const std::string& json, std::string_view key) {
    const auto field = metadata_field(json, key);
    require(!field.empty() && field.find_first_not_of("0123456789") == std::string::npos,
            "child metadata number field is invalid");
    return std::stoul(field);
}

struct ChildObservation {
    std::string raw;
    std::string metadata;
    unsigned long pid = 0;
    bool timed_out = false;
    bool complete = false;
};

ChildObservation read_child(const std::filesystem::path& result) {
    ChildObservation observation;
    observation.raw = read_binary(result);
    observation.metadata = read_binary(result.string() + ".json");
    observation.pid = metadata_number(observation.metadata, "pid");
    observation.timed_out = metadata_bool(observation.metadata, "timed_out");
    observation.complete = metadata_bool(observation.metadata, "complete");
    // The independently written raw bytes and the metadata projection must agree.
    require(metadata_hex(observation.metadata, "received_hex") == hex(observation.raw),
            "child metadata disagrees with its raw received bytes");
    require(metadata_hex(observation.metadata, "source_hex") == hex(query_source),
            "child metadata source differs from the fixed source");
    require(metadata_hex(observation.metadata, "contract_reply_hex") == hex(contract_reply),
            "child metadata contract expectation differs from the confirmed reply contract");
    return observation;
}

std::string exchange(HANDLE input, HANDLE output, std::string_view source, bool& timed_out, bool& complete) {
    DWORD written = 0;
    require(WriteFile(output, source.data(), static_cast<DWORD>(source.size()), &written,
                      nullptr) &&
                written == source.size(),
            "query child source write failed");

    std::string received;
    timed_out = false;
    complete = false;
    const ULONGLONG deadline = GetTickCount64() + child_reply_timeout_ms;
    while (!complete) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            timed_out = true;
            break;
        }
        const DWORD state = WaitForSingleObject(input, static_cast<DWORD>(deadline - now));
        if (state == WAIT_TIMEOUT) {
            timed_out = true;
            break;
        }
        require(state == WAIT_OBJECT_0, "query child console wait failed");
        wchar_t units[64];
        DWORD count = 0;
        require(ReadConsoleW(input, units, 64, &count, nullptr) && count,
                "query child console read failed");
        for (DWORD i = 0; i < count; ++i) {
            require(units[i] >= 0 && units[i] <= 127, "query child reply is not ASCII");
            received.push_back(static_cast<char>(units[i]));
            require(received.size() <= 1024, "query child reply exceeds its fixed budget");
            if (units[i] == L'R') {
                require(i + 1 == count, "query child received trailing input after the reply");
                complete = true;
                break;
            }
        }
    }
    return received;
}

// Read glyphs without querying cursor info, which can itself cause a DSR.
std::string native_marker_grid(HANDLE output) {
    constexpr SHORT columns = 91, rows = 20;
    std::array<CHAR_INFO, columns * rows> cells{};
    SMALL_RECT bounds{0, 0, columns - 1, rows - 1};
    require(ReadConsoleOutputW(output, cells.data(), {columns, rows}, {0, 0}, &bounds) &&
            bounds.Left == 0 && bounds.Top == 0 && (bounds.Bottom == rows - 1 || bounds.Bottom == 13) &&
            (bounds.Right == columns - 1 || bounds.Right == 60), "native marker grid read failed");
    std::string grid;
    for (int row = 0; row <= bounds.Bottom; ++row)
        for (int column = 0; column <= bounds.Right; ++column) {
            const auto cell = cells[row * columns + column].Char.UnicodeChar;
            grid.push_back(cell == L'W' || cell == L'X' ? static_cast<char>(cell) : ' ');
        }
    return grid;
}

enum class CursorTrace { single_query, fixed_grid, saved_resize };

int query_child(const std::filesystem::path& result, CursorTrace trace = CursorTrace::single_query) {
    try {
        const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        require(input && input != INVALID_HANDLE_VALUE, "query child console input unavailable");
        require(output && output != INVALID_HANDLE_VALUE, "query child console output unavailable");
        BOOL owned = FALSE;
        require(IsProcessInJob(GetCurrentProcess(), nullptr, &owned) && owned,
                "query child must run inside the owned Session Job");
        require(SetConsoleCP(CP_UTF8) && SetConsoleOutputCP(CP_UTF8),
                "query child UTF-8 codepage setup failed");
        require(SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT),
                "query child VT input mode setup failed");
        require(SetConsoleMode(output, ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT |
                                          ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                                          DISABLE_NEWLINE_AUTO_RETURN),
                "query child VT output mode setup failed");
        if (trace != CursorTrace::single_query) {
            std::ifstream phases("sources.hex");
            std::ofstream replies(result, std::ios::binary);
            require(bool(phases) && bool(replies), "Cursor trace files unavailable");
            std::string line;
            size_t count = 0;
            while (std::getline(phases, line)) {
                const auto phase = std::to_string(count);
                if (trace == CursorTrace::saved_resize) {
                    const auto deadline = GetTickCount64() + child_reply_timeout_ms;
                    while (!std::filesystem::exists("release-" + phase) && GetTickCount64() < deadline) Sleep(2);
                    require(std::filesystem::exists("release-" + phase), "cursor trace resize gate timed out");
                }
                const auto source = QByteArray::fromHex(QByteArray::fromStdString(line)).toStdString() + "\x1b[6n";
                bool timed_out, complete;
                const auto received = exchange(input, output, source, timed_out, complete);
                replies << hex(received) << std::endl;
                require(bool(replies) && complete && !timed_out, "Cursor phase reply incomplete");
                if (trace == CursorTrace::saved_resize) {
                    write_binary("cells-" + phase, native_marker_grid(output));
                    write_binary("done-" + phase, "");
                }
                ++count;
            }
            // The parent validates the exact phase count for the selected reference.
            return phases.eof() && count > 0 ? 0 : 5;
        }
        bool timed_out, complete;
        const auto received = exchange(input, output, query_source, timed_out, complete);
        write_binary(result, received);
        std::ofstream metadata(result.string() + ".json", std::ios::binary | std::ios::trunc);
        require(bool(metadata), "query child metadata file could not be created");
        metadata << "{\"schema\":\"termforge-query-child-v1\",\"pid\":" << GetCurrentProcessId()
                 << ",\"source_hex\":\"" << hex(query_source) << "\",\"contract_reply_hex\":\""
                 << hex(contract_reply) << "\",\"received_hex\":\"" << hex(received)
                 << "\",\"received_bytes\":" << received.size()
                 << ",\"timed_out\":" << (timed_out ? "true" : "false")
                 << ",\"complete\":" << (complete ? "true" : "false") << ",\"vt_input_mode\":true}\n";
        metadata.close();
        require(bool(metadata), "query child metadata write failed");
        return complete ? 0 : (timed_out ? 42 : 43);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "query child: %s\n", error.what());
        return 5;
    }
}

std::wstring child_arguments(const std::filesystem::path& result) {
    return L"--query-child \"" + result.wstring() + L"\"";
}

struct RawStage {
    std::string backend;
    DWORD creation_flags = 0;
    std::string transport_hex;
    std::string received_hex;
    bool query_seen = false;
    bool timed_out = false;
    bool complete = false;
    DWORD exit_code = 0;
    DWORD child_pid = 0;
    bool cleanup_verified = false;
};

// Direct fixed-ConPTY run without SessionModel. Passing no reply keeps the host
// silent (does conhost answer the child itself?); passing distinct_reply proves
// the host->ConPTY->child byte path. Neither run uses the product reply code.
RawStage run_raw(const std::filesystem::path& executable, const std::filesystem::path& directory,
                 const std::filesystem::path& result, std::optional<std::string> reply) {
    RawStage stage;
    tf_conpty::Session session(tf_conpty::Backend::configured());
    stage.backend = session.backend_identity();
    stage.creation_flags = session.backend_creation_flags();
    require(stage.backend != "system", "raw control must load the fixed bundled ConPTY backend");
    session.start(executable.wstring(), child_arguments(result), {}, directory.wstring(), {80, 24});
    stage.child_pid = session.pid;
    const ULONGLONG observe_deadline = GetTickCount64() + query_observe_timeout_ms;
    while (GetTickCount64() < observe_deadline) {
        if (session.output().find(query_source) != std::string::npos) {
            stage.query_seen = true;
            break;
        }
        if (session.wait_exit(2)) break;
        Sleep(2);
    }
    require(stage.query_seen, "raw control did not observe the child's fixed source on the ConPTY transport");
    if (reply) session.send(*reply);
    bool exited = false;
    const ULONGLONG exit_deadline = GetTickCount64() + child_exit_timeout_ms;
    while (GetTickCount64() < exit_deadline) {
        if (session.wait_exit(5)) {
            exited = true;
            break;
        }
    }
    stage.transport_hex = hex(session.output());
    require(exited, "raw control child did not exit within its bounded deadline");
    stage.exit_code = session.exit_code;
    session.close();
    session.check_reader();
    require(session.cleanup_complete && session.active_after == 0 && !session.capture_overflow,
            "raw control did not reclaim its owned child, host and pipes");
    stage.cleanup_verified = true;
    const auto child = read_child(result);
    require(child.pid == session.pid, "raw control child identity mismatch");
    stage.received_hex = hex(child.raw);
    stage.timed_out = child.timed_out;
    stage.complete = child.complete;
    return stage;
}

struct ProductStage {
    std::string backend;
    std::string transport_hex;
    size_t query_offset = 0;
    bool da1_startup_observed = false;
    int core_row = 0;
    int core_column = 0;
    std::string core_row0_hex;
    std::string expected_hex = hex(contract_reply);
    std::string received_hex;
    bool timed_out = false;
    bool complete = false;
    DWORD exit_code = 0;
    DWORD child_pid = 0;
    bool natural_exit = false;
    bool cleanup_verified = false;
    bool output_eof = false;
    DWORD active_after = 0;
};

// Product path: the real SessionModel feeds parsed transport into the recovery
// Parser and writes take_replies() back through its shared bounded input queue.
ProductStage run_product(const std::filesystem::path& executable, const std::filesystem::path& directory,
                         const std::filesystem::path& result) {
    SessionModel model;
    model.enable_transport_capture();
    require(model.start(executable.wstring(), child_arguments(result), directory.wstring()),
            "product SessionModel did not start the fixed ConPTY child");
    QElapsedTimer lifecycle;
    lifecycle.start();
    while (!model.finished() && lifecycle.elapsed() < static_cast<qint64>(product_finish_timeout_ms)) {
        QTest::qWait(5);
        if (!model.error().isEmpty()) throw std::runtime_error(model.error().toStdString());
    }
    require(model.finished(), "product SessionModel did not finish within its bounded deadline");

    ProductStage stage;
    stage.backend = model.configuration_identity().toStdString();
    const auto transport = model.transport_capture();
    stage.transport_hex = hex(transport);
    stage.da1_startup_observed = transport.find("\x1b[c") != std::string::npos;
    const auto query = transport.find(query_source);
    require(query != std::string::npos,
            "product transport did not carry the child's exact fixed source");
    stage.query_offset = query;
    const auto& screen = model.screen();
    stage.core_row = screen.row;
    stage.core_column = screen.column;
    require(screen.width() == 80 && screen.height() == 24,
            "product core grid is not the fixed 80x24 primary screen");
    const auto row0 = termforge::project_line(screen.visible_lines()[0]).text;
    stage.core_row0_hex = hex(row0);
    require(row0 == "AB", "product core primary row does not contain the fixed AB source");
    require(stage.core_row == 0 && stage.core_column == 2,
            "product core protocol cursor after the fixed source is not row 1 column 3");
    stage.exit_code = model.exit_code();
    stage.child_pid = model.resize_state().child_pid;
    stage.natural_exit = model.natural_exit();
    stage.cleanup_verified = model.cleanup_verified();
    stage.output_eof = model.output_eof();
    stage.active_after = model.active_processes_after();
    require(model.natural_exit() && model.cleanup_verified() && model.active_processes_after() == 0,
            "product SessionModel natural exit or owned cleanup was not verified");

    const auto child = read_child(result);
    require(child.pid == stage.child_pid, "product child identity mismatch");
    stage.received_hex = hex(child.raw);
    stage.timed_out = child.timed_out;
    stage.complete = child.complete;
    std::cerr << "{\"schema\":\"termforge-qt-query-reply-product-v1\",\"backend\":\""
              << stage.backend << "\",\"transport_hex\":\"" << stage.transport_hex
              << "\",\"query_offset\":" << stage.query_offset << ",\"da1_startup_observed\":"
              << (stage.da1_startup_observed ? "true" : "false") << ",\"core_cursor\":["
              << stage.core_row << ',' << stage.core_column << "],\"core_row0_hex\":\""
              << stage.core_row0_hex << "\",\"expected_hex\":\"" << stage.expected_hex
              << "\",\"received_hex\":\"" << stage.received_hex << "\",\"timed_out\":"
              << (stage.timed_out ? "true" : "false") << ",\"complete\":"
              << (stage.complete ? "true" : "false") << ",\"child_pid\":" << stage.child_pid
              << ",\"exit_code\":" << stage.exit_code << ",\"natural_exit\":"
              << (stage.natural_exit ? "true" : "false") << ",\"cleanup_verified\":"
              << (stage.cleanup_verified ? "true" : "false") << ",\"active_after\":"
              << stage.active_after << ",\"presented_done\":null}\n";
    return stage;
}

// The child receives only the recorded inputs. Collect the entire trajectory
// before comparing its DSR replies with the independent WindTerm observation.
int compare_cursor_trace(const std::filesystem::path& exe, const std::filesystem::path& directory,
                         const std::filesystem::path& reference, std::string_view option) {
    const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(read_binary(reference)));
    const auto record = document.object().value("reference").toObject();
    const bool resized = option == "--saved-cursor-reference";
    const bool margin_case = option == "--duplicate-margin-reference";
    const auto case_id = document.object().value("case_id").toString();
    const bool ris_resize = resized && case_id.startsWith(QStringLiteral("ris-return-"));
    const auto axis_case = case_id.section('-', -1);
    const bool height_case = axis_case == QStringLiteral("short") || axis_case == QStringLiteral("tall");
    if (resized)
        require(case_id == (ris_resize ? QStringLiteral("ris-return-") : QStringLiteral("saved-return-")) + axis_case &&
                    (height_case || axis_case == QStringLiteral("narrow") || axis_case == QStringLiteral("wide")),
                "wrong saved cursor reference file");
    else if (margin_case)
        require(document.object().value("case_id").toString() == QStringLiteral("alternate-duplicate-margin"),
                "wrong duplicate-margin reference file");
    else
        require(document.object().value("schema").toString() == QStringLiteral("termforge-original-alternate-ris-followup-summary-v1"),
                "wrong RIS reference file");
    const auto phases = record.value("phases").toArray();
    require(!phases.isEmpty(), "empty cursor trace");
    const auto grid = resized ? phases.at(0).toObject().value("frozen_grid").toArray() : record.value("grid").toArray();
    require(grid.size() == 2 && grid[0].toInt() == 91 && grid[1].toInt() == (resized ? 20 : 25) &&
            phases.size() == (ris_resize ? 9 : resized ? 6 : 14),
            "invalid cursor reference geometry or phase count");
    std::ofstream sources(directory / "sources.hex");
    for (int index = 0; index < phases.size(); ++index) {
        const auto phase = phases[index].toObject();
        const auto source = phase.value("source_hex").toString().toLatin1();
        require(phase.value("phase").toInt(-1) == index && phase.value("source_hex").isString() &&
                (resized || !source.isEmpty()) &&
                QByteArray::fromHex(source).toHex() == source, "invalid cursor phase input");
        sources << source.toStdString() << '\n';
    }
    sources.close();
    require(bool(sources), "could not write cursor inputs");

    SessionModel model;
    model.enable_transport_capture();
    require(model.resize(91, grid[1].toInt()) &&
            model.start(exe.wstring(), resized ? L"--saved-cursor-child" : L"--cursor-child", directory.wstring()),
            "could not start the cursor trace session");
    QElapsedTimer lifecycle;
    lifecycle.start();
    int differences = 0;
    if (resized) {
        require(model.uses_host_resize_maps(), "saved cursor test requires the product resize path");
        const auto wait = [&](auto ready, const char* failure) {
            while (!ready() && !model.finished() && model.error().isEmpty() &&
                   lifecycle.elapsed() < static_cast<qint64>(product_finish_timeout_ms)) QTest::qWait(3);
            require(ready(), failure);
        };
        for (int index = 0; index < phases.size(); ++index) {
            const auto phase = phases[index].toObject();
            const auto size = phase.value("frozen_grid").toArray();
            require(size.size() == 2, "missing saved cursor grid");
            const int width = size[0].toInt(), height = size[1].toInt();
            require(height_case ? width == 91 && (height == 14 || height == 20)
                                : height == 20 && (width == 61 || width == 91), "invalid saved cursor grid");
            require(model.resize(width, height), "saved cursor resize failed");
            wait([&] {
                const auto state = model.resize_state();
                return !state.pending && state.transport_columns == width && state.transport_rows == height &&
                       model.screen().width() == width && model.screen().height() == height;
            }, "saved cursor resize did not complete");
            write_binary(directory / ("release-" + std::to_string(index)), "");
            wait([&] { return std::filesystem::exists(directory / ("done-" + std::to_string(index))); },
                 "saved cursor child did not receive the phase reply");
            // WindTerm's document API also exposes the hidden primary text;
            // compare cells only when the source has made the primary visible.
            const int restore = ris_resize ? 2 : (axis_case == QStringLiteral("narrow") || axis_case == QStringLiteral("short")) ? 3 : 4;
            const bool primary = index == 0 || index >= restore;
            if ((model.screen().active_buffer() == termforge::ScreenBuffer::primary) != primary) ++differences;
            if (!primary) continue;
            const auto markers = phase.value("markers").toArray();
            std::string expected_native(width * height, ' ');
            int found = 0;
            for (const auto& line : model.screen().visible_lines())
                for (int column = 0; column < width; ++column)
                    if (line[column].text == "X" || line[column].text == "W") ++found;
            if (found != markers.size()) ++differences;
            for (const auto& value : markers) {
                const auto marker = value.toObject();
                const int row = marker.value("document_line").toInt(-1) - (height_case ? phase.value("scroll").toInt() : 0);
                const int col = marker.value("column").toInt(-1);
                require(row >= 0 && row < height && col >= 0 && col < width, "reference marker is outside the primary grid");
                if (height_case)
                    require(marker.value("rect").toObject().value("y").toInt(-1) == row * 18,
                            "reference scroll offset and measured cell rectangle disagree");
                const auto label = marker.value("label").toString();
                require(label == QStringLiteral("W") || label == QStringLiteral("X"), "invalid saved cursor marker");
                expected_native[row * width + col] = label.at(0).toLatin1();
                if (model.screen().visible_lines()[row][col].text != marker.value("label").toString().toStdString()) {
                    ++differences;
                    std::cerr << "saved cursor marker differs at phase " << index << " cell " << row << ',' << col << '\n';
                }
            }
            const auto native = read_binary(directory / ("cells-" + std::to_string(index)));
            if (native != expected_native) {
                ++differences;
                std::cerr << "native marker grid differs at phase " << index << '\n';
                for (size_t cell = 0; cell < native.size(); ++cell)
                    if (native[cell] != ' ') std::cerr << native[cell] << " at " << cell / width << ',' << cell % width << '\n';
            }
        }
    }
    while (!model.finished() && lifecycle.elapsed() < static_cast<qint64>(product_finish_timeout_ms))
        QTest::qWait(5);
    require(model.finished() && model.error().isEmpty() && model.natural_exit() && model.output_eof() &&
            model.cleanup_verified() && model.active_processes_after() == 0, "cursor trace session lifecycle failed");
    std::cerr << "cursor trace transport_hex=" << hex(model.transport_capture())
              << " child_exit=" << model.exit_code() << '\n';
    std::ifstream replies(directory / "replies.hex", std::ios::binary);
    for (int index = 0; index < phases.size(); ++index) {
        const auto cursor = phases[index].toObject().value("cursor_reply").toArray();
        require(cursor.size() == 2, "reference cursor missing");
        const auto expected = hex("\x1b[" + std::to_string(cursor[0].toInt()) + ";" +
                                  std::to_string(cursor[1].toInt()) + "R");
        std::string actual;
        require(bool(std::getline(replies, actual)), "cursor trace child result missing a phase");
        std::cerr << "cursor trace phase=" << index << " expected=" << expected << " child=" << actual << '\n';
        if (actual != expected) ++differences;
    }
    require(replies.peek() == std::char_traits<char>::eof() && model.exit_code() == 0,
            "cursor trace child did not finish the fixed trajectory");
    std::cout << "{\"scenario\":\"" << (resized ? case_id.toStdString() :
              margin_case ? "alternate-duplicate-margin" : "alternate-ris-cursor")
              << "\",\"phases\":" << phases.size() << ",\"differences\":"
              << differences << ",\"cleanup_verified\":true}\n";
    return differences ? 1 : 0;
}

void emit_raw(const char* name, const RawStage& stage) {
    std::cerr << "{\"schema\":\"termforge-qt-query-reply-raw-v1\",\"stage\":\"" << name
              << "\",\"backend\":\"" << stage.backend << "\",\"creation_flags\":" << stage.creation_flags
              << ",\"child_pid\":" << stage.child_pid << ",\"query_seen\":"
              << (stage.query_seen ? "true" : "false") << ",\"timed_out\":"
              << (stage.timed_out ? "true" : "false") << ",\"complete\":"
              << (stage.complete ? "true" : "false") << ",\"exit_code\":" << stage.exit_code
              << ",\"received_hex\":\"" << stage.received_hex << "\",\"transport_hex\":\""
              << stage.transport_hex << "\",\"cleanup_verified\":"
              << (stage.cleanup_verified ? "true" : "false") << ",\"presented_done\":null}\n";
}
}  // namespace

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc == 3 && std::string_view(argv[1]) == "--query-child") {
        return query_child(std::filesystem::path(argv[2]));
    }
    if (argc == 2 && (std::string_view(argv[1]) == "--cursor-child" || std::string_view(argv[1]) == "--ris-child")) return query_child("replies.hex", CursorTrace::fixed_grid);
    if (argc == 2 && std::string_view(argv[1]) == "--saved-cursor-child") return query_child("replies.hex", CursorTrace::saved_resize);
    const bool ris = argc == 3 && std::string_view(argv[1]) == "--ris-reference";
    const bool margin = argc == 3 && std::string_view(argv[1]) == "--duplicate-margin-reference";
    const bool saved = argc == 3 && std::string_view(argv[1]) == "--saved-cursor-reference";
    if (argc != 1 && !ris && !margin && !saved) {
        std::cerr << "usage: qt_query_reply_test [--ris-reference FILE|--duplicate-margin-reference FILE|--saved-cursor-reference FILE]\n";
        return 2;
    }
    char gate[3]{};
    DWORD count = 0;
    BOOL owned = FALSE;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), gate, sizeof(gate), &count, nullptr) || count != 3 ||
        std::string_view(gate, count) != "GO\n" || !IsProcessInJob(GetCurrentProcess(), nullptr, &owned) ||
        !owned)
        return 2;
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    try {
        QTemporaryDir workspace;
        require(workspace.isValid(), "query reply workspace unavailable");
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        require(length && length < 32768, "query reply test executable path unavailable");
        const std::filesystem::path exe(executable);
        const std::filesystem::path directory(workspace.path().toStdWString());

        if (ris || margin || saved) return compare_cursor_trace(exe, directory, argv[2], argv[1]);

        const auto silent = run_raw(exe, directory, directory / "raw-silent.bin", std::nullopt);
        emit_raw("raw-silent", silent);
        require(!silent.complete && silent.received_hex.empty(),
                "fixed ConPTY answered the child's DSR itself instead of leaving it to the host");
        require(silent.exit_code == 42 && silent.timed_out,
                "silent-host control child did not observe its bounded timeout");
        require(silent.cleanup_verified, "silent-host control did not verify cleanup");

        const auto distinct = run_raw(exe, directory, directory / "raw-distinct.bin",
                                      std::string(distinct_reply));
        emit_raw("raw-distinct", distinct);
        require(distinct.complete && distinct.received_hex == hex(distinct_reply),
                "host-written diagnostic bytes did not reach the real child unchanged");
        require(distinct.exit_code == 0, "distinct-reply control child did not complete");

        const auto product = run_product(exe, directory, directory / "product.bin");
        require(product.complete, "product child did not complete its bounded reply wait");
        require(product.exit_code == 0, "product child exited before receiving its reply");
        require(product.received_hex == product.expected_hex,
                "product path did not deliver the literal ESC[1;3R contract reply to the real child");
        std::cout << "{\"schema\":\"termforge-qt-query-reply-v1\",\"scenario\":"
                     "\"dsr-6n-fixed-primary\",\"source_hex\":\""
                  << hex(query_source) << "\",\"contract_reply_hex\":\"" << hex(contract_reply)
                  << "\",\"raw_silent\":{\"received_hex\":\"" << silent.received_hex
                  << "\",\"timed_out\":true,\"exit_code\":" << silent.exit_code
                  << "},\"raw_distinct\":{\"received_hex\":\"" << distinct.received_hex
                  << "\",\"exit_code\":" << distinct.exit_code << "},\"product\":{\"received_hex\":\""
                  << product.received_hex << "\",\"core_cursor\":[" << product.core_row << ','
                  << product.core_column << "],\"query_offset\":" << product.query_offset
                  << ",\"child_pid\":" << product.child_pid << ",\"exit_code\":" << product.exit_code
                  << ",\"cleanup_verified\":true},\"child_count\":3,\"presented_done\":null}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
