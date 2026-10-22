// Bounded offscreen presentation probe; no desktop capture or target application automation.
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include "../core/terminal.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("render API failed"); }
struct Bitmap {
    int width, height;
    HDC dc{}; HBITMAP bitmap{}; HGDIOBJ previous{}; void* pixels{};
    Bitmap(int w, int h) : width(w), height(h) {
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!dc || !bitmap || !pixels) {
            if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc);
            throw std::runtime_error("DIB creation failed");
        }
        previous = SelectObject(dc, bitmap);
        std::memset(pixels, 0, static_cast<size_t>(w) * h * 4);
    }
    ~Bitmap() { SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); }
    Bitmap(const Bitmap&) = delete;
    void save(const std::string& path) const {
        BITMAPFILEHEADER file{}; file.bfType = 0x4d42;
        file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER); file.bfSize = file.bfOffBits + width * height * 4;
        BITMAPINFOHEADER header{}; header.biSize = sizeof(header); header.biWidth = width; header.biHeight = -height;
        header.biPlanes = 1; header.biBitCount = 32; header.biCompression = BI_RGB;
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&file), sizeof(file));
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
        output.write(static_cast<const char*>(pixels), static_cast<std::streamsize>(width) * height * 4);
        if (!output) throw std::runtime_error("bitmap write failed");
    }
};
static std::wstring utf16(const std::string& text) {
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size == 0) throw std::runtime_error("invalid text");
    std::wstring value(size, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), value.data(), size);
    return value;
}
static unsigned color(int index, bool background) {
    static const unsigned palette[] = {0x101010,0xc05050,0x50c050,0xc0c050,0x5050c0,0xc050c0,0x50c0c0,0xd0d0d0,
                                      0x707070,0xff7070,0x70ff70,0xffff70,0x7070ff,0xff70ff,0x70ffff,0xffffff};
    return index < 0 ? (background ? 0x101010 : 0xe0e0e0) : palette[index];
}
static COLORREF gdi_color(unsigned rgb) { return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
static void draw(Bitmap& bitmap, const termforge::Screen& screen, const std::string& backend, int dpi, int first, int last) {
    const float scale = dpi / 96.0f;
    if (backend == "gdi") {
        SetBkMode(bitmap.dc, TRANSPARENT);
        for (int r = first; r < last; ++r) for (int c = 0; c < screen.width(); ++c) {
            const auto& cell = screen.lines[r][c]; if (cell.width == 0) continue;
            RECT rect{int(c*12*scale),int(r*26*scale),int((c+cell.width)*12*scale),int((r+1)*26*scale)};
            HBRUSH brush = CreateSolidBrush(gdi_color(color(cell.style.bg, true)));
            if (!brush) throw std::runtime_error("brush failed");
            FillRect(bitmap.dc, &rect, brush); DeleteObject(brush);
            HFONT font = CreateFontW(-int(18*scale),0,0,0,cell.style.bold ? FW_BOLD : FW_NORMAL,FALSE,FALSE,FALSE,
                                    DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,
                                    FIXED_PITCH,L"Consolas");
            if (!font) throw std::runtime_error("font failed");
            const auto old = SelectObject(bitmap.dc, font);
            SetTextColor(bitmap.dc, gdi_color(color(cell.style.fg, false)));
            const auto text = utf16(cell.text);
            DrawTextW(bitmap.dc,text.data(),static_cast<int>(text.size()),&rect,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX);
            SelectObject(bitmap.dc,old); DeleteObject(font);
        }
        GdiFlush(); return;
    }
    ComPtr<ID2D1Factory> factory; check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
    ComPtr<IDWriteFactory> write;
    check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write.GetAddressOf())));
    ComPtr<ID2D1DCRenderTarget> target;
    const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),float(dpi),float(dpi));
    check(factory->CreateDCRenderTarget(&properties,target.GetAddressOf()));
    RECT area{0,0,bitmap.width,bitmap.height}; check(target->BindDC(bitmap.dc,&area));
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    ComPtr<ID2D1SolidColorBrush> brush; check(target->CreateSolidColorBrush(D2D1::ColorF(0),brush.GetAddressOf()));
    target->BeginDraw();
    for (int r = first; r < last; ++r) for (int c = 0; c < screen.width(); ++c) {
        const auto& cell = screen.lines[r][c]; if (cell.width == 0) continue;
        const auto rect = D2D1::RectF(float(c*12),float(r*26),float((c+cell.width)*12),float((r+1)*26));
        brush->SetColor(D2D1::ColorF(color(cell.style.bg,true))); target->FillRectangle(rect,brush.Get());
        ComPtr<IDWriteTextFormat> format;
        check(write->CreateTextFormat(L"Consolas",nullptr,cell.style.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                     DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,18,L"zh-CN",format.GetAddressOf()));
        check(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
        const auto text = utf16(cell.text);
        brush->SetColor(D2D1::ColorF(color(cell.style.fg,false)));
        target->DrawText(text.data(),static_cast<UINT32>(text.size()),format.Get(),rect,brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    check(target->EndDraw());
}
int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("presentation_probe gdi|dwrite 96|144 output.bmp");
        const std::string backend = argv[1]; const int dpi = std::stoi(argv[2]);
        if ((backend != "gdi" && backend != "dwrite") || (dpi != 96 && dpi != 144)) throw std::runtime_error("invalid arguments");
        termforge::Parser parser(3,32,0);
        parser.feed("ASCII e\xcc\x81 \xe4\xb8\xad\xe6\x96\x87\r\n\x1b[1;31;44mColor\x1b[0m\r\nCursor_"); parser.finish();
        Bitmap partial(32*12*dpi/96,3*26*dpi/96), full(partial.width,partial.height);
        const auto start = std::chrono::steady_clock::now();
        draw(partial,parser.screen,backend,dpi,0,3);
        GdiFlush(); partial.save(std::string(argv[3]) + ".initial.bmp");
        parser.feed("\x1b[2;1H\x1b[2Kchanged\x1b[3;8H"); parser.finish();
        draw(partial,parser.screen,backend,dpi,1,2);
        draw(full,parser.screen,backend,dpi,0,3);
        GdiFlush();
        bool equal = true; size_t lit = 0;
        const auto* a = static_cast<const unsigned char*>(partial.pixels);
        const auto* b = static_cast<const unsigned char*>(full.pixels);
        for (size_t i = 0; i < size_t(full.width)*full.height*4; i += 4) {
            if (std::memcmp(a+i,b+i,3)) equal = false; // Alpha is unspecified for GDI.
            if (b[i] > 40 || b[i+1] > 40 || b[i+2] > 40) ++lit;
        }
        full.save(argv[3]);
        std::cout << "{\"backend\":\"" << backend << "\",\"dpi\":" << dpi << ",\"dirty_equals_full\":" << (equal?"true":"false")
                  << ",\"lit_pixels\":" << lit << ",\"draw_probe_ms\":"
                  << std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()
                  << ",\"window_verified\":false,\"presented_done\":null}\n";
        return equal && lit > 100 ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
