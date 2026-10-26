#include "render.hpp"
#include "../shell/ui_font.hpp"
#include <cmath>

static void verify_text_conversion() {
    require(utf16("").empty(), "empty UTF-8 must convert to an empty UTF-16 string");
    require(utf16("A中e\xcc\x81😀") == L"A中e\u0301\U0001f600", "valid UTF-8 scalars changed during conversion");
    for (const auto& invalid : {std::string("\xff"), std::string("\xc0\x80"),
                                std::string("\xed\xa0\x80"), std::string("\xe4\xb8")}) {
        bool rejected = false;
        try { (void)utf16(invalid); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "nonempty invalid UTF-8 must remain rejected");
    }
}

static void expect_rejected(const std::string& backend, int dpi, int zoom) {
    bool rejected = false;
    try { Renderer renderer(backend, dpi, zoom); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "invalid renderer settings accepted");
}

static void verify_missing_font() {
    std::vector<wchar_t> module(32768);
    const DWORD count = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    require(count && count < module.size(), "test executable path unavailable");
    const auto directory = std::filesystem::current_path() /
        (".renderer-font-missing-" + std::to_string(GetCurrentProcessId()));
    require(std::filesystem::create_directory(directory), "font-missing fixture directory already exists");
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(directory, error); }
    } cleanup{directory};
    const auto copy = directory / L"renderer_metrics_test.exe";
    require(CopyFileW(std::wstring(module.data(), count).c_str(), copy.c_str(), TRUE),
            "font-missing fixture copy failed");
    std::wstring command = L"\"" + copy.native() + L"\" --expect-missing-font";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    require(CreateProcessW(copy.c_str(), command.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process),
            "font-missing fixture launch failed");
    const DWORD waited = WaitForSingleObject(process.hProcess, 10000);
    if (waited != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
    }
    DWORD exit_code = MAXDWORD;
    const BOOL queried = GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    require(waited == WAIT_OBJECT_0 && queried && exit_code == 42,
            "missing bundled font was not rejected by an asset-free process");
}

static void verify_cursor(const Bitmap& with, const Bitmap& without,
                          const Snapshot& snapshot, const Renderer& renderer) {
    const RECT cell{renderer.x(snapshot.cursor_column), renderer.y(snapshot.cursor_row),
                    renderer.x(snapshot.cursor_column+1), renderer.y(snapshot.cursor_row+1)};
    size_t changed = 0;
    for (int y = 0; y < with.height; ++y) for (int x = 0; x < with.width; ++x) {
        if (with.rgb(x,y) == without.rgb(x,y)) continue;
        require(x >= cell.left && x < cell.right && y >= cell.top && y < cell.bottom,
                "cursor escaped its physical cell");
        require(with.rgb(x,y) == 0xffd050, "cursor color changed");
        ++changed;
    }
    const int thickness = std::max(2, (2*renderer.dpi()*renderer.zoom_percent()+4800)/9600);
    require(changed == static_cast<size_t>(renderer.cell_width()*thickness), "cursor dimensions mismatch");
}

static void verify_ink(const Bitmap& bitmap, const Snapshot& snapshot, Renderer& renderer) {
    for (int r = 0; r < snapshot.rows(); ++r) for (int c = 0; c < snapshot.columns(); ++c) {
        const auto& cell = snapshot.lines[r][c];
        if (cell.width == 0 || cell.text == " ") continue;
        const auto& audit = renderer.layout(cell.text, cell.style.bold, cell.width);
        require(audit.glyphs > 0 && audit.missing == 0 && !audit.faces.empty(), "glyph audit failed");
        const float available_dip = float(renderer.cell_width()*cell.width)*96.0f/renderer.dpi();
        require(audit.width <= available_dip+0.01f, "glyph layout exceeds the physical grid width");
        bool ink = false;
        for (int y = renderer.y(r); y < renderer.y(r+1); ++y)
            for (int x = renderer.x(c); x < renderer.x(c+cell.width); ++x)
                ink = ink || bitmap.rgb(x,y) != color(cell.style.bg,true);
        require(ink, "nonblank cell has no rendered ink");
    }
}

static void scenario(const std::string& backend, int dpi, int zoom, float baseline_width) {
    Renderer renderer(backend, dpi, zoom);
    const int width = static_cast<int>(std::lround(12.0*dpi*zoom/9600.0));
    const int height = static_cast<int>(std::lround(26.0*dpi*zoom/9600.0));
    require(renderer.dpi() == dpi && renderer.zoom_percent() == zoom, "settings getters mismatch");
    require(renderer.cell_width() == width && renderer.cell_height() == height, "cell metrics mismatch");
    for (int n = 0; n <= 1000; ++n) {
        require(renderer.x(n) == n*width && renderer.y(n) == n*height, "grid accumulated fractional-cell drift");
    }
    // Text layout audit widths stay in DIPs; monitor DPI changes pixel density,
    // while terminal zoom changes the requested font size itself.
    const auto& ascii = renderer.layout("M", false);
    require(std::abs(ascii.width-baseline_width*zoom/100.0f) < 0.02f,
            "layout width did not retain DIP units");
    require(ascii.faces == std::set<std::string>{"Consolas"}, "ASCII font identity changed");
    require(renderer.layout("e\xcc\x81", false).faces == std::set<std::string>{"Consolas"},
            "combining text did not retain the requested font");
    const auto& fallback = renderer.layout("\xe4\xb8\xad", false);
    require(fallback.missing == 0 && !fallback.faces.empty() && !fallback.faces.count("Consolas"),
            "CJK fallback identity was not resolved");
    if (backend == "dwrite") {
        for (const std::string text : {"👩\xe2\x80\x8d💻", "1\xef\xb8\x8f\xe2\x83\xa3"}) {
            const auto& emoji = renderer.layout(text, false, 2);
            require(emoji.missing == 0 && emoji.glyphs == 1 &&
                    emoji.faces == std::set<std::string>{"Segoe UI Emoji"},
                    "whole emoji cluster was split across glyphs or fonts");
        }
    }
    const auto& missing = renderer.layout("\xf4\x8f\xbf\xbf", false);
    require(missing.missing > 0 && !missing.faces.empty(), "missing-glyph control failed");

    termforge::Parser parser(4,24,0);
    parser.feed("ASCII e\xcc\x81 \xe4\xb8\xad\xe6\x96\x87\r\n\x1b[1;31;44mColor\x1b[0m\r\nCursor\r\nTail\xf0\x9f\x99\x82");
    parser.finish();
    const Snapshot initial(parser.screen, 1);
    Bitmap partial(renderer.x(initial.columns()), renderer.y(initial.rows()));
    Bitmap no_cursor(partial.width, partial.height);
    renderer.draw(partial, initial, 0, initial.rows());
    renderer.draw(no_cursor, initial, 0, initial.rows(), false);
    verify_cursor(partial, no_cursor, initial, renderer);
    verify_ink(no_cursor, initial, renderer);

    parser.feed("\x1b[2;1H\x1b[2Kchanged\x1b[3;11H"); parser.finish();
    const Snapshot final(parser.screen, 2);
    const auto dirty = changed_rows(initial, final);
    require(dirty == std::vector<int>({1,2,3}), "dirty row setup mismatch");
    for (const int row : dirty) renderer.draw(partial, final, row, row+1);
    Bitmap full(partial.width, partial.height);
    renderer.draw(full, final, 0, final.rows());
    require(partial.equals(full), "dirty update differs from full redraw");
    require(partial.equals(no_cursor, 0, renderer.y(1)), "dirty update changed untouched row");
    renderer.draw(no_cursor, final, 0, final.rows(), false);
    verify_cursor(partial, no_cursor, final, renderer);
    verify_ink(no_cursor, final, renderer);

    // Plain colored rows exercise the first and last pixel of each rounded
    // cell. This catches background gaps that text/cursor tests could miss.
    termforge::Parser bands(4,4,0);
    bands.feed("\x1b[41m\x1b[1;1H\x1b[2K\x1b[42m\x1b[2;1H\x1b[2K"
               "\x1b[44m\x1b[3;1H\x1b[2K\x1b[45m\x1b[4;1H\x1b[2K");
    bands.finish();
    Bitmap band_pixels(renderer.x(4), renderer.y(4));
    renderer.draw(band_pixels, Snapshot(bands.screen, 1), 0, 4, false);
    const int colors[]{1,2,4,5};
    for (int y = 0; y < band_pixels.height; ++y) for (int x = 0; x < band_pixels.width; ++x)
        require(band_pixels.rgb(x,y) == color(colors[y/height],true), "rounded row background has gap or overlap");
}

int main(int argc, char** argv) {
    try {
        verify_text_conversion();
        if (argc == 2 && std::string(argv[1]) == "--expect-missing-font") {
            try { WorkspaceFont font; }
            catch (const std::runtime_error& error) {
                return std::string(error.what()).find("UI font missing:") == 0 ? 42 : 1;
            }
            return 1;
        }
        require(argc == 1, "unexpected renderer metrics arguments");
        verify_missing_font();
        for (const int dpi : {-1,0,47,769}) expect_rejected("dwrite", dpi, 100);
        for (const int zoom : {-1,0,49,201}) expect_rejected("gdi", 96, zoom);
        expect_rejected("invalid", 96, 100);
        for (const int dpi : {47,769}) {
            bool rejected = false;
            try { WorkspaceFont font(dpi); }
            catch (const std::runtime_error&) { rejected = true; }
            require(rejected, "invalid workspace font DPI accepted");
        }
        for (const int dpi : {96,120,144,192}) {
            WorkspaceFont font(dpi);
            require(font.handle() && font.face() == L"Roboto Mono", "workspace font identity mismatch");
            LOGFONTW logical{};
            require(GetObjectW(font.handle(), sizeof(logical), &logical) == sizeof(logical),
                    "workspace font logical metrics unavailable");
            const int expected_height = MulDiv(9, dpi, 72);
            require(logical.lfHeight == -expected_height, "workspace font DPI height mismatch");
            const HDC dc = CreateCompatibleDC(nullptr);
            require(dc != nullptr, "workspace font audit DC creation failed");
            const HGDIOBJ previous = SelectObject(dc, font.handle());
            wchar_t resolved[LF_FACESIZE]{};
            TEXTMETRICW metrics{};
            const bool audited = previous && previous != HGDI_ERROR &&
                GetTextFaceW(dc, LF_FACESIZE, resolved) > 0 && GetTextMetricsW(dc, &metrics);
            if (previous && previous != HGDI_ERROR) SelectObject(dc, previous);
            DeleteDC(dc);
            const int expected_line_height = MulDiv(expected_height, 4, 3);
            if (!audited || std::wstring(resolved) != L"Roboto Mono" || metrics.tmHeight != expected_line_height)
                throw std::runtime_error("workspace font resolved GDI metrics mismatch: dpi=" + std::to_string(dpi) +
                    " em=" + std::to_string(expected_height) + " expected_line=" +
                    std::to_string(expected_line_height) + " actual_line=" + std::to_string(metrics.tmHeight));
        }
        Renderer baseline("dwrite", 96);
        require(baseline.zoom_percent() == 100 && baseline.cell_width() == 12 && baseline.cell_height() == 26,
                "default renderer contract changed");
        const float baseline_width = baseline.layout("M", false).width;
        for (const auto* backend : {"gdi", "dwrite"}) for (const int dpi : {96,120,144,192})
            for (const int zoom : {50,100,125,200}) {
                try { scenario(backend, dpi, zoom, baseline_width); }
                catch (const std::exception& error) {
                    std::cerr << backend << " dpi=" << dpi << " zoom=" << zoom << ": " << error.what() << '\n';
                    return 1;
                }
            }
        std::cout << "renderer metrics: 32 DPI/zoom/backend scenarios passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
