#include "render.hpp"
#include <sstream>
#include "desktop.hpp"
struct Window {
    Renderer& renderer;
    std::shared_ptr<const Snapshot> snapshot;
    Bitmap surface;
    HWND hwnd{};
    std::string failure;
    int paints = 0, prints = 0, resizes = 0, painted_revision = 0;
    std::vector<double> draw_samples, submit_samples;
    Window(Renderer& r, std::shared_ptr<const Snapshot> s) : renderer(r), snapshot(std::move(s)), surface(r.x(snapshot->columns()),r.y(snapshot->rows())) {
        WNDCLASSW cls{}; cls.lpfnWndProc = procedure; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"TermForgePresentationProbe";
        require(RegisterClassW(&cls) != 0,"window class registration failed");
        hwnd = CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,L"TermForge synthetic probe",WS_POPUP|WS_VISIBLE,
                              0,0,surface.width,surface.height,nullptr,nullptr,cls.hInstance,this);
        if (!hwnd) { UnregisterClassW(cls.lpszClassName,cls.hInstance); throw std::runtime_error("window creation failed"); }
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); UnregisterClassW(L"TermForgePresentationProbe",GetModuleHandleW(nullptr)); }
    void close() {
        require(DestroyWindow(hwnd) != FALSE,"window destroy failed"); hwnd = nullptr;
        require(UnregisterClassW(L"TermForgePresentationProbe",GetModuleHandleW(nullptr)) != FALSE,"window class cleanup failed");
    }
    Window(const Window&) = delete;
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if (message == WM_NCCREATE) { self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
        if (!self) return DefWindowProcW(hwnd,message,wp,lp);
        try {
            if (message == WM_ERASEBKGND) return 1;
            if (message == WM_SIZE) { ++self->resizes; return 0; }
            if (message == WM_PAINT) {
                PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd,&paint);
                try {
                    require(dc != nullptr,"BeginPaint failed"); const int height = self->renderer.y(1);
                    const int first = std::max(0,int(paint.rcPaint.top)/height);
                    const int last = std::min(self->snapshot->rows(),(int(paint.rcPaint.bottom)+height-1)/height);
                    const auto draw_start = Clock::now(); self->renderer.draw(self->surface,*self->snapshot,first,last);
                    self->draw_samples.push_back(ms(draw_start)); const auto submit_start = Clock::now();
                    require(BitBlt(dc,0,0,self->surface.width,self->surface.height,self->surface.dc,0,0,SRCCOPY) != FALSE,"window blit failed");
                    require(GdiFlush() != FALSE,"window GDI flush failed"); self->submit_samples.push_back(ms(submit_start));
                    ++self->paints; self->painted_revision = self->snapshot->revision;
                } catch (...) { EndPaint(hwnd,&paint); throw; }
                EndPaint(hwnd,&paint); return 0;
            }
            if (message == WM_PRINTCLIENT) {
                require(BitBlt(reinterpret_cast<HDC>(wp),0,0,self->surface.width,self->surface.height,self->surface.dc,0,0,SRCCOPY) != FALSE,"print client blit failed");
                ++self->prints; return 0;
            }
        } catch (const std::exception& error) { self->failure = error.what(); return 0; }
        return DefWindowProcW(hwnd,message,wp,lp);
    }
    void pump() {
        MSG message{}; int count = 0;
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message); require(++count < 10000,"message loop did not quiesce");
        }
        require(failure.empty(),failure.c_str());
    }
    void paint(const RECT* area = nullptr) {
        const int before = paints; require(InvalidateRect(hwnd,area,FALSE) != FALSE,"invalidate failed");
        require(UpdateWindow(hwnd) != FALSE,"UpdateWindow failed"); pump();
        require(paints > before && painted_revision == snapshot->revision,"real WM_PAINT did not consume snapshot");
    }
    void print(Bitmap& destination) {
        const int before = prints;
        SendMessageW(hwnd,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(destination.dc),PRF_CLIENT);
        require(GdiFlush() != FALSE,"capture flush failed"); require(failure.empty() && prints == before+1,"WM_PRINTCLIENT failed");
    }
};
static bool cursor_check(const Bitmap& with, const Bitmap& without, const Snapshot& snapshot, Renderer& renderer) {
    size_t changed = 0;
    const RECT area{renderer.x(snapshot.cursor_column),renderer.y(snapshot.cursor_row),renderer.x(snapshot.cursor_column+1),renderer.y(snapshot.cursor_row+1)};
    for (int y = 0; y < with.height; ++y) for (int x = 0; x < with.width; ++x) if (with.rgb(x,y) != without.rgb(x,y)) {
        if (x < area.left || x >= area.right || y < area.top || y >= area.bottom || with.rgb(x,y) != 0xffd050) return false;
        ++changed;
    }
    return changed >= static_cast<size_t>(renderer.x(1)*2);
}
static void json_strings(std::ostream& out, const std::set<std::string>& strings) {
    out << '['; bool comma = false;
    for (const auto& item : strings) { if (comma) out << ','; termforge::Screen::quoted(out,item); comma = true; } out << ']';
}
static void json_numbers(std::ostream& out, const std::vector<double>& numbers) {
    out << '['; for (size_t i = 0; i < numbers.size(); ++i) { if (i) out << ','; out << numbers[i]; } out << ']';
}
static std::pair<std::string, int> probe(int argc, char** argv) {
    try {
        require(argc == 4 || (argc == 5 && std::string(argv[4]) == "--wait-after-paint"),"presentation_probe gdi|dwrite 96|144 output.bmp [--wait-after-paint]");
        const std::string backend = argv[1], dpi_text = argv[2];
        require((backend == "gdi" || backend == "dwrite") && (dpi_text == "96" || dpi_text == "144"),"invalid arguments");
        const int dpi = std::stoi(dpi_text);
        require(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE,"DPI awareness failed");
        std::ostringstream report;
        bool passed = false;
        { // Destroy all renderer/layout/window resources before detaching the desktop.
        termforge::Parser parser(3,32,0);
        // Row 1 also carries one reverse cell and one underlined cell so both new
        // attributes are checked against real pixels, not only against the model.
        parser.feed("ASCII e\xcc\x81 \xe4\xb8\xad\xe6\x96\x87\r\n\x1b[1;31;44mColor\x1b[0m"
                    "\x1b[7;32mREV\x1b[0m\x1b[4;31mUND\x1b[0m\x1b[9;33mSTR\x1b[0m\r\nCursor"); parser.finish();
        auto initial = std::make_shared<const Snapshot>(parser.screen,1);
        Renderer renderer(backend,dpi); renderer.warm(*initial);
        const Layout missing_probe = renderer.layout("\xf4\x8f\xbf\xbf",false); // U+10FFFF: never passed to the terminal core.
        require(missing_probe.missing > 0,"missing-glyph control was not detected");
        size_t glyphs = 0, missing = 0, overflowing = 0; std::set<std::string> faces;
        for (const auto& row : initial->lines) for (const auto& cell : row) if (cell.width && cell.text != " ") {
            const auto& text = renderer.layout(cell.text,cell.style.bold,cell.width); glyphs += text.glyphs; missing += text.missing;
            faces.insert(text.faces.begin(),text.faces.end()); if (text.width > cell.width*12.0f+0.01f) ++overflowing;
        }
        Window window(renderer,initial); window.paint();
        if (argc == 5) {
            std::cout << "{\"timeout_probe_ready\":true,\"window_painted\":true}\n" << std::flush;
            Sleep(INFINITE); // Runner kills only this owned, childless process after the deadline.
        }
        const UINT actual_dpi = GetDpiForWindow(window.hwnd); require(actual_dpi != 0,"window DPI query failed");
        Bitmap first(window.surface.width,window.surface.height); window.print(first); first.save(std::string(argv[3])+".initial.bmp");
        const bool color_ok = first.rgb(renderer.x(0)+1,renderer.y(2)-1) == color(4,true);
        // "REV" occupies columns 5..7 and "UND" columns 8..10 on row 1.
        const bool reverse_ok = first.rgb(renderer.x(5)+1,renderer.y(1)+1) == color(2,true);
        // Decoration lines are one physical pixel wide, so scan a small band
        // instead of one coordinate: DPI scaling can round them by a pixel.
        const auto band_has = [&](int x, int top, int bottom, unsigned expected) {
            for (int y = top; y <= bottom; ++y) if (first.rgb(x,y) == expected) return true;
            return false;
        };
        const bool underline_ok = band_has(renderer.x(8)+1, renderer.y(2)-2, renderer.y(2)-1, color(1,false));
        // "STR" occupies columns 11..13; its strike line crosses the cell middle.
        const int strike_middle = renderer.y(1) + renderer.cell_height()/2;
        const bool strikethrough_ok = band_has(renderer.x(11)+1, strike_middle-2, strike_middle+2, color(3,false));
        size_t text_cells_with_ink = 0, red_pixels = 0;
        for (int r = 0; r < initial->rows(); ++r) for (int c = 0; c < initial->columns(); ++c) {
            const auto& cell = initial->lines[r][c]; if (!cell.width || cell.text == " ") continue;
            bool ink = false;
            for (int y = renderer.y(r); y < renderer.y(r+1)-3*dpi/96; ++y)
                for (int x = renderer.x(c); x < renderer.x(c+cell.width); ++x) {
                    const unsigned pixel = first.rgb(x,y); ink = ink || pixel != color(cell.style.bg,true);
                    if (r == 1 && pixel == color(1,false)) ++red_pixels;
                }
            if (ink) ++text_cells_with_ink;
        }
        // Same base letter without the mark must produce a different glyph image.
        termforge::Parser unaccented(3,32,0);
        unaccented.feed("ASCII e \xe4\xb8\xad\xe6\x96\x87\r\n\x1b[1;31;44mColor\x1b[0m"
                        "\x1b[7;32mREV\x1b[0m\x1b[4;31mUND\x1b[0m\x1b[9;33mSTR\x1b[0m\r\nCursor"); unaccented.finish();
        Bitmap accent_control(first.width,first.height); renderer.draw(accent_control,Snapshot(unaccented.screen,0),0,3);
        bool mark_changed_pixels = false;
        for (int y = 0; y < renderer.y(1); ++y) for (int x = renderer.x(6); x < renderer.x(7); ++x)
            mark_changed_pixels = mark_changed_pixels || first.rgb(x,y) != accent_control.rgb(x,y);
        Bitmap no_cursor(first.width,first.height); renderer.draw(no_cursor,*initial,0,initial->rows(),false);
        const bool initial_cursor_ok = cursor_check(first,no_cursor,*initial,renderer);
        parser.feed("\x1b[2;1H\x1b[2Kchanged\x1b[3;11H"); parser.finish();
        auto final = std::make_shared<const Snapshot>(parser.screen,2);
        const auto changed = changed_rows(*initial,*final); require(changed == std::vector<int>({1,2}),"dirty row contract mismatch");
        require(initial->lines[1][0].text == "C" && initial->cursor_column == 6,"snapshot changed with producer state");
        renderer.warm(*final); window.snapshot = final;
        RECT dirty{0,renderer.y(changed.front()),first.width,renderer.y(changed.back()+1)}; window.paint(&dirty);
        Bitmap capture(first.width,first.height), full(first.width,first.height);
        const auto capture_start = Clock::now(); window.print(capture); const double capture_ms = ms(capture_start);
        renderer.draw(full,*final,0,final->rows());
        const bool equal = capture.equals(full), untouched = first.equals(capture,0,renderer.y(1));
        renderer.draw(no_cursor,*final,0,final->rows(),false);
        const bool final_cursor_ok = cursor_check(capture,no_cursor,*final,renderer);
        capture.save(argv[3]);
        // This HWND only, on the private desktop. A match is not compositor visibility.
        Bitmap client(first.width,first.height); HDC client_dc = GetDC(window.hwnd); bool client_captured = false;
        if (client_dc) { client_captured = BitBlt(client.dc,0,0,client.width,client.height,client_dc,0,0,SRCCOPY) != FALSE; ReleaseDC(window.hwnd,client_dc); }
        GdiFlush(); const bool client_equal = client_captured && client.equals(capture);
        if (client_captured) client.save(std::string(argv[3])+".client.bmp");
        const int old_resizes = window.resizes;
        require(SetWindowPos(window.hwnd,nullptr,0,0,first.width+48,first.height+26,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER) != FALSE,"resize larger failed"); window.pump();
        require(SetWindowPos(window.hwnd,nullptr,0,0,first.width,first.height,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER) != FALSE,"resize restore failed"); window.pump(); window.paint();
        RECT client_rect{}; require(GetClientRect(window.hwnd,&client_rect) != FALSE,"client rect failed");
        const bool resize_ok = window.resizes >= old_resizes+2 && client_rect.right == first.width && client_rect.bottom == first.height;
        Bitmap after_resize(first.width,first.height); window.print(after_resize); const bool resize_equal = after_resize.equals(full);
        // Warm samples: cached resources, no capture/serialization in each sample.
        window.draw_samples.clear(); window.submit_samples.clear();
        for (int i = 0; i < 24; ++i) window.paint(&dirty);
        std::vector<double> timer_samples;
        for (int i = 0; i < 128; ++i) { const auto start = Clock::now(); timer_samples.push_back(ms(start)); }
        window.close();
        const bool valid = equal && untouched && initial_cursor_ok && final_cursor_ok && color_ok && resize_ok && resize_equal &&
                           missing == 0 && overflowing == 0 && text_cells_with_ink == 28 && red_pixels > 0 && mark_changed_pixels &&
               reverse_ok && underline_ok && strikethrough_ok;
        report << "{\"backend\":\"" << backend << "\",\"logical_dpi\":" << dpi << ",\"window_dpi\":" << actual_dpi
                  << ",\"window_verified\":true,\"desktop\":\"private-non-input\",\"capture_layer\":\"WM_PRINTCLIENT from WM_PAINT retained DIB\""
                  << ",\"owned_window_destroyed\":true,\"private_desktop_closed\":true,\"paint_trigger\":\"InvalidateRect + UpdateWindow and message pump\""
                  << ",\"wm_paint_count\":" << window.paints << ",\"wm_printclient_count\":" << window.prints
                  << ",\"snapshot_revision\":" << window.painted_revision << ",\"snapshot_isolation_verified\":true,\"dirty_rows\":[1,2]"
                  << ",\"dirty_equals_full\":" << (equal?"true":"false") << ",\"unchanged_row_preserved\":" << (untouched?"true":"false")
                  << ",\"cursor_verified\":" << (initial_cursor_ok&&final_cursor_ok?"true":"false") << ",\"background_color_verified\":" << (color_ok?"true":"false")
                  << ",\"foreground_color_verified\":" << (red_pixels>0?"true":"false")
            << ",\"reverse_video_verified\":" << (reverse_ok?"true":"false")
            << ",\"underline_verified\":" << (underline_ok?"true":"false")
            << ",\"strikethrough_verified\":" << (strikethrough_ok?"true":"false") << ",\"text_cells_with_ink\":" << text_cells_with_ink
                  << ",\"combining_mark_changes_pixels\":" << (mark_changed_pixels?"true":"false")
                  << ",\"resize_verified\":" << (resize_ok&&resize_equal?"true":"false")
                  << ",\"client_dc_capture_succeeded\":" << (client_captured?"true":"false") << ",\"client_dc_equals_printclient\":" << (client_equal?"true":"false")
                  << ",\"dwrite_glyph_count\":" << glyphs << ",\"dwrite_missing_glyphs\":" << missing << ",\"dwrite_overflowing_cells\":" << overflowing
                  << ",\"missing_glyph_control_detected\":true,\"dwrite_resolved_faces\":";
        json_strings(report,faces);
        report << ",\"dwrite_text_audit\":[";
        bool audit_comma = false;
        for (const auto& value : {std::string("e\xcc\x81"),std::string("\xe4\xb8\xad"),std::string("\xe6\x96\x87")}) {
            if (audit_comma) report << ',';
            audit_comma = true; const auto& text = renderer.layout(value,false);
            report << "{\"text\":"; termforge::Screen::quoted(report,value);
            report << ",\"glyphs\":" << text.glyphs << ",\"missing\":" << text.missing << ",\"layout_width_dip\":" << text.width << ",\"faces\":";
            json_strings(report,text.faces); report << '}';
        }
        report << ']';
        report << ",\"gdi_resolved_fallback_faces\":null,\"layout_cache_entries\":" << renderer.layout_count()
                  << ",\"capture_ms\":" << capture_ms << ",\"draw_samples_ms\":"; json_numbers(report,window.draw_samples);
        report << ",\"frame_submit_samples_ms\":"; json_numbers(report,window.submit_samples);
        report << ",\"timer_pair_samples_ms\":"; json_numbers(report,timer_samples);
        report << ",\"presented_done\":null,\"valid\":" << (valid?"true":"false") << "}\n";
        passed = valid;
        }
        return {report.str(), passed ? 0 : 1};
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return {"", 2}; }
}

int main(int argc, char** argv) {
    try {
        OwnedDesktop desktop(L"TermForgeProbe-");
        const auto result = desktop.run([&] { return probe(argc, argv); });
        std::cout << result.first;
        return result.second;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
