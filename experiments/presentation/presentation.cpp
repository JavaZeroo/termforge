// Original, bounded HWND experiment. Never switches the user's input desktop.
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include "../core/terminal.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <utility>
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void check(HRESULT hr) { require(SUCCEEDED(hr), "render API failed"); }
static std::wstring utf16(const std::string& text) {
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    require(n != 0, "invalid text"); std::wstring value(n, 0);
    require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), value.data(), n) == n, "UTF-16 conversion failed");
    return value;
}
static std::string utf8(const wchar_t* text) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    require(n > 0, "UTF-8 conversion failed"); std::string value(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, text, -1, value.data(), n, nullptr, nullptr); value.pop_back(); return value;
}
struct Bitmap {
    int width, height; HDC dc{}; HBITMAP bitmap{}; HGDIOBJ previous{}; void* pixels{};
    Bitmap(int w, int h) : width(w), height(h) {
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr); bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!dc || !bitmap || !pixels) {
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
            throw std::runtime_error("DIB creation failed");
        }
        previous = SelectObject(dc, bitmap); std::memset(pixels, 0, static_cast<size_t>(w)*h*4);
    }
    ~Bitmap() { SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); }
    Bitmap(const Bitmap&) = delete;
    void save(const std::string& path) const {
        BITMAPFILEHEADER file{}; file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER); file.bfSize = file.bfOffBits + width*height*4;
        BITMAPINFOHEADER header{}; header.biSize = sizeof(header); header.biWidth = width; header.biHeight = -height;
        header.biPlanes = 1; header.biBitCount = 32; header.biCompression = BI_RGB;
        std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(&file), sizeof(file));
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(static_cast<const char*>(pixels), static_cast<std::streamsize>(width)*height*4);
        require(bool(out), "bitmap write failed");
    }
    bool equals(const Bitmap& other, int top = 0, int bottom = -1) const {
        if (width != other.width || height != other.height) return false;
        if (bottom < 0) bottom = height;
        const auto* a = static_cast<const unsigned char*>(pixels); const auto* b = static_cast<const unsigned char*>(other.pixels);
        for (int y = top; y < bottom; ++y) for (int x = 0; x < width; ++x) {
            const size_t i = (static_cast<size_t>(y)*width+x)*4;
            if (std::memcmp(a+i,b+i,3)) return false; // GDI alpha is unspecified.
        }
        return true;
    }
    unsigned rgb(int x, int y) const {
        const auto* p = static_cast<const unsigned char*>(pixels)+(static_cast<size_t>(y)*width+x)*4;
        return (unsigned(p[2])<<16)|(unsigned(p[1])<<8)|p[0];
    }
};
static unsigned color(int index, bool background) {
    static const unsigned palette[] = {0x101010,0xc05050,0x50c050,0xc0c050,0x5050c0,0xc050c0,0x50c0c0,0xd0d0d0,
                                      0x707070,0xff7070,0x70ff70,0xffff70,0x7070ff,0xff70ff,0x70ffff,0xffffff};
    return index < 0 ? (background ? 0x101010 : 0xe0e0e0) : palette[index];
}
static COLORREF gdi_color(unsigned rgb) { return RGB((rgb>>16)&255, (rgb>>8)&255, rgb&255); }
// Copy once at the ownership boundary. No concurrent publication or reflow claim.
struct Snapshot {
    const std::vector<termforge::Line> lines;
    const int cursor_row, cursor_column, revision;
    explicit Snapshot(const termforge::Screen& s, int rev) : lines(s.lines), cursor_row(s.row), cursor_column(s.column), revision(rev) {}
    int rows() const { return static_cast<int>(lines.size()); }
    int columns() const { return static_cast<int>(lines.front().size()); }
};
static std::vector<int> changed_rows(const Snapshot& a, const Snapshot& b) {
    std::set<int> changed;
    for (int r = 0; r < b.rows(); ++r) for (int c = 0; c < b.columns(); ++c) {
        const auto& x = a.lines[r][c]; const auto& y = b.lines[r][c];
        if (x.text != y.text || x.width != y.width || x.style.fg != y.style.fg || x.style.bg != y.style.bg || x.style.bold != y.style.bold) changed.insert(r);
    }
    if (a.cursor_row != b.cursor_row || a.cursor_column != b.cursor_column) { changed.insert(a.cursor_row); changed.insert(b.cursor_row); }
    return {changed.begin(),changed.end()};
}
// Inspect the same IDWriteTextLayout that is drawn. Zero glyph index is .notdef.
class GlyphAudit final : public IDWriteTextRenderer {
    ULONG refs_ = 1;
    ComPtr<IDWriteGdiInterop> interop_;
public:
    size_t glyphs = 0, missing = 0;
    std::set<std::string> faces;
    explicit GlyphAudit(IDWriteFactory* factory) { check(factory->GetGdiInterop(interop_.GetAddressOf())); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWritePixelSnapping) || iid == __uuidof(IDWriteTextRenderer)) {
            *value = static_cast<IDWriteTextRenderer*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* value) override { *value = TRUE; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* value) override { *value = {1,0,0,1,0,0}; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* value) override { *value = 1; return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT, FLOAT, DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* run,
                                          const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override {
        try {
            LOGFONTW font{}; check(interop_->ConvertFontFaceToLOGFONT(run->fontFace, &font)); faces.insert(utf8(font.lfFaceName));
            glyphs += run->glyphCount;
            for (UINT32 i = 0; i < run->glyphCount; ++i) if (!run->glyphIndices[i]) ++missing;
            return S_OK;
        } catch (...) { return E_FAIL; }
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override { return E_NOTIMPL; }
};
struct Layout {
    ComPtr<IDWriteTextLayout> text;
    size_t glyphs = 0, missing = 0;
    std::set<std::string> faces;
    float width = 0;
};
class Renderer {
    std::string backend_;
    int dpi_;
    HFONT normal_{}, bold_{};
    std::map<unsigned,HBRUSH> brushes_;
    ComPtr<ID2D1Factory> factory_;
    ComPtr<IDWriteFactory> write_;
    ComPtr<ID2D1DCRenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteTextFormat> formats_[2];
    std::map<std::pair<std::string,bool>,Layout> layouts_;
    HBRUSH brush(unsigned value) {
        auto it = brushes_.find(value);
        if (it == brushes_.end()) { HBRUSH b = CreateSolidBrush(gdi_color(value)); require(b != nullptr, "brush failed"); it = brushes_.emplace(value,b).first; }
        return it->second;
    }
public:
    Renderer(const std::string& backend, int dpi) : backend_(backend), dpi_(dpi) {
        check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write_.GetAddressOf())));
        for (int bold = 0; bold < 2; ++bold) {
            check(write_->CreateTextFormat(L"Consolas",nullptr,bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,18,L"zh-CN",formats_[bold].GetAddressOf()));
            check(formats_[bold]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
        }
        if (backend == "dwrite") {
            check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory_.GetAddressOf()));
            const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),float(dpi),float(dpi));
            check(factory_->CreateDCRenderTarget(&properties,target_.GetAddressOf()));
            target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            check(target_->CreateSolidColorBrush(D2D1::ColorF(0),brush_.GetAddressOf()));
        } else {
            normal_ = CreateFontW(-18*dpi/96,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FIXED_PITCH,L"Consolas");
            bold_ = CreateFontW(-18*dpi/96,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FIXED_PITCH,L"Consolas");
            if (!normal_ || !bold_) {
                if (normal_) DeleteObject(normal_);
                if (bold_) DeleteObject(bold_);
                normal_ = bold_ = nullptr; throw std::runtime_error("font failed");
            }
        }
    }
    ~Renderer() { for (const auto& b : brushes_) DeleteObject(b.second); if (normal_) DeleteObject(normal_); if (bold_) DeleteObject(bold_); }
    Renderer(const Renderer&) = delete;
    int x(int cell) const { return cell*12*dpi_/96; }
    int y(int row) const { return row*26*dpi_/96; }
    const Layout& layout(const std::string& text, bool bold) {
        const auto key = std::make_pair(text,bold); auto found = layouts_.find(key);
        if (found != layouts_.end()) return found->second;
        Layout result; const auto wide = utf16(text);
        check(write_->CreateTextLayout(wide.data(),static_cast<UINT32>(wide.size()),formats_[bold].Get(),64,26,result.text.GetAddressOf()));
        ComPtr<GlyphAudit> audit; audit.Attach(new GlyphAudit(write_.Get())); check(result.text->Draw(nullptr,audit.Get(),0,0));
        result.glyphs = audit->glyphs; result.missing = audit->missing; result.faces = audit->faces;
        DWRITE_TEXT_METRICS metrics{}; check(result.text->GetMetrics(&metrics)); result.width = metrics.widthIncludingTrailingWhitespace;
        return layouts_.emplace(key,std::move(result)).first->second;
    }
    void draw(Bitmap& bitmap, const Snapshot& snapshot, int first, int last, bool cursor = true) {
        if (backend_ == "gdi") {
            SetBkMode(bitmap.dc,TRANSPARENT);
            for (int r = first; r < last; ++r) for (int c = 0; c < snapshot.columns(); ++c) {
                const auto& cell = snapshot.lines[r][c]; if (cell.width == 0) continue;
                RECT rect{x(c),y(r),x(c+cell.width),y(r+1)}; require(FillRect(bitmap.dc,&rect,brush(color(cell.style.bg,true))) != 0,"fill failed");
                const auto old = SelectObject(bitmap.dc,cell.style.bold ? bold_ : normal_);
                SetTextColor(bitmap.dc,gdi_color(color(cell.style.fg,false))); const auto text = utf16(cell.text);
                DrawTextW(bitmap.dc,text.data(),static_cast<int>(text.size()),&rect,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX);
                SelectObject(bitmap.dc,old);
            }
        } else {
            RECT area{0,0,bitmap.width,bitmap.height}; check(target_->BindDC(bitmap.dc,&area)); target_->BeginDraw();
            for (int r = first; r < last; ++r) for (int c = 0; c < snapshot.columns(); ++c) {
                const auto& cell = snapshot.lines[r][c]; if (cell.width == 0) continue;
                const auto rect = D2D1::RectF(float(c*12),float(r*26),float((c+cell.width)*12),float((r+1)*26));
                brush_->SetColor(D2D1::ColorF(color(cell.style.bg,true))); target_->FillRectangle(rect,brush_.Get());
                const auto& text = layout(cell.text,cell.style.bold); brush_->SetColor(D2D1::ColorF(color(cell.style.fg,false)));
                target_->PushAxisAlignedClip(rect,D2D1_ANTIALIAS_MODE_ALIASED);
                target_->DrawTextLayout(D2D1::Point2F(float(c*12),float(r*26)),text.text.Get(),brush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
                target_->PopAxisAlignedClip();
            }
            check(target_->EndDraw());
        }
        if (cursor && snapshot.cursor_row >= first && snapshot.cursor_row < last) {
            RECT caret{x(snapshot.cursor_column),y(snapshot.cursor_row+1)-std::max(2,2*dpi_/96),x(snapshot.cursor_column+1),y(snapshot.cursor_row+1)};
            require(FillRect(bitmap.dc,&caret,brush(0xffd050)) != 0,"cursor fill failed");
        }
        require(GdiFlush() != FALSE,"GDI flush failed");
    }
    void warm(const Snapshot& snapshot) { for (const auto& row : snapshot.lines) for (const auto& cell : row) if (cell.width) layout(cell.text,cell.style.bold); }
    size_t layout_count() const { return layouts_.size(); }
};
struct Desktop {
    HDESK previous{}, owned{};
    Desktop() {
        previous = GetThreadDesktop(GetCurrentThreadId());
        const std::wstring name = L"TermForgeProbe-"+std::to_wstring(GetCurrentProcessId());
        owned = CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
        require(owned != nullptr,"private desktop creation failed");
        if (!SetThreadDesktop(owned)) { CloseDesktop(owned); owned = nullptr; throw std::runtime_error("private desktop attachment failed"); }
    }
    ~Desktop() { if (owned) { SetThreadDesktop(previous); CloseDesktop(owned); } }
    void close() {
        require(SetThreadDesktop(previous) != FALSE,"private desktop detach failed");
        require(CloseDesktop(owned) != FALSE,"private desktop close failed"); owned = nullptr;
    }
    Desktop(const Desktop&) = delete;
};
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
int main(int argc, char** argv) {
    try {
        require(argc == 4 || (argc == 5 && std::string(argv[4]) == "--wait-after-paint"),"presentation_probe gdi|dwrite 96|144 output.bmp [--wait-after-paint]");
        const std::string backend = argv[1], dpi_text = argv[2];
        require((backend == "gdi" || backend == "dwrite") && (dpi_text == "96" || dpi_text == "144"),"invalid arguments");
        const int dpi = std::stoi(dpi_text);
        require(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE,"DPI awareness failed");
        Desktop desktop; // Before any HWND/resources; never SwitchDesktop.
        termforge::Parser parser(3,32,0);
        parser.feed("ASCII e\xcc\x81 \xe4\xb8\xad\xe6\x96\x87\r\n\x1b[1;31;44mColor\x1b[0m\r\nCursor"); parser.finish();
        auto initial = std::make_shared<const Snapshot>(parser.screen,1);
        Renderer renderer(backend,dpi); renderer.warm(*initial);
        const Layout missing_probe = renderer.layout("\xf4\x8f\xbf\xbf",false); // U+10FFFF: never passed to the terminal core.
        require(missing_probe.missing > 0,"missing-glyph control was not detected");
        size_t glyphs = 0, missing = 0, overflowing = 0; std::set<std::string> faces;
        for (const auto& row : initial->lines) for (const auto& cell : row) if (cell.width && cell.text != " ") {
            const auto& text = renderer.layout(cell.text,cell.style.bold); glyphs += text.glyphs; missing += text.missing;
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
        unaccented.feed("ASCII e \xe4\xb8\xad\xe6\x96\x87\r\n\x1b[1;31;44mColor\x1b[0m\r\nCursor"); unaccented.finish();
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
        window.close(); desktop.close();
        const bool valid = equal && untouched && initial_cursor_ok && final_cursor_ok && color_ok && resize_ok && resize_equal &&
                           missing == 0 && overflowing == 0 && text_cells_with_ink == 19 && red_pixels > 0 && mark_changed_pixels;
        std::cout << "{\"backend\":\"" << backend << "\",\"logical_dpi\":" << dpi << ",\"window_dpi\":" << actual_dpi
                  << ",\"window_verified\":true,\"desktop\":\"private-non-input\",\"capture_layer\":\"WM_PRINTCLIENT from WM_PAINT retained DIB\""
                  << ",\"owned_window_destroyed\":true,\"private_desktop_closed\":true,\"paint_trigger\":\"InvalidateRect + UpdateWindow and message pump\""
                  << ",\"wm_paint_count\":" << window.paints << ",\"wm_printclient_count\":" << window.prints
                  << ",\"snapshot_revision\":" << window.painted_revision << ",\"snapshot_isolation_verified\":true,\"dirty_rows\":[1,2]"
                  << ",\"dirty_equals_full\":" << (equal?"true":"false") << ",\"unchanged_row_preserved\":" << (untouched?"true":"false")
                  << ",\"cursor_verified\":" << (initial_cursor_ok&&final_cursor_ok?"true":"false") << ",\"background_color_verified\":" << (color_ok?"true":"false")
                  << ",\"foreground_color_verified\":" << (red_pixels>0?"true":"false") << ",\"text_cells_with_ink\":" << text_cells_with_ink
                  << ",\"combining_mark_changes_pixels\":" << (mark_changed_pixels?"true":"false")
                  << ",\"resize_verified\":" << (resize_ok&&resize_equal?"true":"false")
                  << ",\"client_dc_capture_succeeded\":" << (client_captured?"true":"false") << ",\"client_dc_equals_printclient\":" << (client_equal?"true":"false")
                  << ",\"dwrite_glyph_count\":" << glyphs << ",\"dwrite_missing_glyphs\":" << missing << ",\"dwrite_overflowing_cells\":" << overflowing
                  << ",\"missing_glyph_control_detected\":true,\"dwrite_resolved_faces\":";
        json_strings(std::cout,faces);
        std::cout << ",\"dwrite_text_audit\":[";
        bool audit_comma = false;
        for (const auto& value : {std::string("e\xcc\x81"),std::string("\xe4\xb8\xad"),std::string("\xe6\x96\x87")}) {
            if (audit_comma) std::cout << ',';
            audit_comma = true; const auto& text = renderer.layout(value,false);
            std::cout << "{\"text\":"; termforge::Screen::quoted(std::cout,value);
            std::cout << ",\"glyphs\":" << text.glyphs << ",\"missing\":" << text.missing << ",\"layout_width_dip\":" << text.width << ",\"faces\":";
            json_strings(std::cout,text.faces); std::cout << '}';
        }
        std::cout << ']';
        std::cout << ",\"gdi_resolved_fallback_faces\":null,\"layout_cache_entries\":" << renderer.layout_count()
                  << ",\"capture_ms\":" << capture_ms << ",\"draw_samples_ms\":"; json_numbers(std::cout,window.draw_samples);
        std::cout << ",\"frame_submit_samples_ms\":"; json_numbers(std::cout,window.submit_samples);
        std::cout << ",\"timer_pair_samples_ms\":"; json_numbers(std::cout,timer_samples);
        std::cout << ",\"presented_done\":null,\"valid\":" << (valid?"true":"false") << "}\n";
        return valid ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
