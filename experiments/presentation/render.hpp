// Shared original fixed-grid renderer for V1 and A1.
#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include "../core/terminal.hpp"
#include "../core/history_view.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <utility>
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point start) { return std::chrono::duration<double,std::milli>(Clock::now()-start).count(); }
static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void check(HRESULT hr) { require(SUCCEEDED(hr), "render API failed"); }
static std::wstring utf16(const std::string& text) {
    if (text.empty()) return {};
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
    explicit Snapshot(termforge::ViewSnapshot view, int rev)
        : lines(std::move(view.lines)), cursor_row(view.cursor_row), cursor_column(view.cursor_column), revision(rev) {}
    int rows() const { return static_cast<int>(lines.size()); }
    int columns() const { return static_cast<int>(lines.front().size()); }
};
static std::vector<int> changed_rows(const Snapshot& a, const Snapshot& b) {
    std::set<int> changed;
    if (a.rows() != b.rows() || a.columns() != b.columns()) {
        std::vector<int> all;
        for (int r = 0; r < b.rows(); ++r) all.push_back(r);
        return all;
    }
    for (int r = 0; r < b.rows(); ++r) for (int c = 0; c < b.columns(); ++c) {
        const auto& x = a.lines[r][c]; const auto& y = b.lines[r][c];
        if (x.text != y.text || x.width != y.width || x.style.fg != y.style.fg || x.style.bg != y.style.bg || x.style.bold != y.style.bold) changed.insert(r);
    }
    if (a.cursor_row != b.cursor_row || a.cursor_column != b.cursor_column) {
        if (a.cursor_row >= 0 && a.cursor_row < b.rows()) changed.insert(a.cursor_row);
        if (b.cursor_row >= 0 && b.cursor_row < b.rows()) changed.insert(b.cursor_row);
    }
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
    int dpi_, zoom_percent_, cell_width_ = 0, cell_height_ = 0;
    HFONT normal_{}, bold_{};
    std::map<unsigned,HBRUSH> brushes_;
    ComPtr<ID2D1Factory> factory_;
    ComPtr<IDWriteFactory> write_;
    ComPtr<ID2D1DCRenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteTextFormat> formats_[2];
    std::map<std::tuple<std::string,bool,int>,Layout> layouts_;
    // Round the cell once, then use its physical size throughout the grid. This
    // keeps hit testing, dirty rows, clips and glyph origins on the same pixels.
    int scaled_pixels(int value) const { return (value*dpi_*zoom_percent_+4800)/9600; }
    float logical_x(int cell) const { return float(x(cell))*96.0f/dpi_; }
    float logical_y(int row) const { return float(y(row))*96.0f/dpi_; }
    HBRUSH brush(unsigned value) {
        auto it = brushes_.find(value);
        if (it == brushes_.end()) { HBRUSH b = CreateSolidBrush(gdi_color(value)); require(b != nullptr, "brush failed"); it = brushes_.emplace(value,b).first; }
        return it->second;
    }
public:
    Renderer(const std::string& backend, int dpi, int zoom_percent = 100)
        : backend_(backend), dpi_(dpi), zoom_percent_(zoom_percent) {
        require(backend == "gdi" || backend == "dwrite", "invalid renderer backend");
        require(dpi >= 48 && dpi <= 768, "renderer DPI out of range");
        require(zoom_percent >= 50 && zoom_percent <= 200, "renderer zoom out of range");
        cell_width_ = scaled_pixels(12); cell_height_ = scaled_pixels(26);
        check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write_.GetAddressOf())));
        for (int bold = 0; bold < 2; ++bold) {
            check(write_->CreateTextFormat(L"Consolas",nullptr,bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,18.0f*zoom_percent_/100.0f,L"zh-CN",formats_[bold].GetAddressOf()));
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
            normal_ = CreateFontW(-scaled_pixels(18),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,FIXED_PITCH,L"Consolas");
            bold_ = CreateFontW(-scaled_pixels(18),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
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
    int dpi() const { return dpi_; }
    int zoom_percent() const { return zoom_percent_; }
    int cell_width() const { return cell_width_; }
    int cell_height() const { return cell_height_; }
    int x(int cell) const { return cell*cell_width_; }
    int y(int row) const { return row*cell_height_; }
    const Layout& layout(const std::string& text, bool bold, int columns = 1) {
        require(columns == 1 || columns == 2, "glyph layout requires one or two cells");
        const auto key = std::make_tuple(text,bold,columns); auto found = layouts_.find(key);
        if (found != layouts_.end()) return found->second;
        if (layouts_.size() >= 1024) layouts_.clear(); // Fixed memory budget for live streams.
        Layout result; const auto wide = utf16(text);
        check(write_->CreateTextLayout(wide.data(),static_cast<UINT32>(wide.size()),formats_[bold].Get(),
            64.0f*zoom_percent_/100.0f,logical_y(1),result.text.GetAddressOf()));
        const float available = float(cell_width_*columns)*96.0f/dpi_;
        DWRITE_TEXT_METRICS metrics{}; check(result.text->GetMetrics(&metrics));
        if (metrics.widthIncludingTrailingWhitespace > available+0.01f) {
            const DWRITE_TEXT_RANGE range{0,static_cast<UINT32>(wide.size())};
            const float size = 18.0f*zoom_percent_/100.0f*available/metrics.widthIncludingTrailingWhitespace;
            check(result.text->SetFontSize((std::max)(1.0f,size),range));
            check(result.text->GetMetrics(&metrics));
            require(metrics.widthIncludingTrailingWhitespace <= available+0.01f,
                    "glyph cannot fit its existing physical cell");
        }
        ComPtr<GlyphAudit> audit; audit.Attach(new GlyphAudit(write_.Get())); check(result.text->Draw(nullptr,audit.Get(),0,0));
        result.glyphs = audit->glyphs; result.missing = audit->missing; result.faces = audit->faces;
        result.width = metrics.widthIncludingTrailingWhitespace;
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
                const auto rect = D2D1::RectF(logical_x(c),logical_y(r),logical_x(c+cell.width),logical_y(r+1));
                brush_->SetColor(D2D1::ColorF(color(cell.style.bg,true))); target_->FillRectangle(rect,brush_.Get());
                const auto& text = layout(cell.text,cell.style.bold,cell.width); brush_->SetColor(D2D1::ColorF(color(cell.style.fg,false)));
                target_->PushAxisAlignedClip(rect,D2D1_ANTIALIAS_MODE_ALIASED);
                target_->DrawTextLayout(D2D1::Point2F(logical_x(c),logical_y(r)),text.text.Get(),brush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
                target_->PopAxisAlignedClip();
            }
            check(target_->EndDraw());
        }
        if (cursor && snapshot.cursor_row >= first && snapshot.cursor_row < last) {
            RECT caret{x(snapshot.cursor_column),y(snapshot.cursor_row+1)-std::max(2,scaled_pixels(2)),x(snapshot.cursor_column+1),y(snapshot.cursor_row+1)};
            require(FillRect(bitmap.dc,&caret,brush(0xffd050)) != 0,"cursor fill failed");
        }
        require(GdiFlush() != FALSE,"GDI flush failed");
    }
    void warm(const Snapshot& snapshot) { for (const auto& row : snapshot.lines) for (const auto& cell : row) if (cell.width) layout(cell.text,cell.style.bold,cell.width); }
    size_t layout_count() const { return layouts_.size(); }
};
