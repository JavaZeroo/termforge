#include "render.hpp"
#include <cmath>

static void expect_rejected(const std::string& backend, int dpi, int zoom) {
    bool rejected = false;
    try { Renderer renderer(backend, dpi, zoom); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "invalid renderer settings accepted");
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
        const auto& audit = renderer.layout(cell.text, cell.style.bold);
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
    require(std::abs(renderer.layout("M", false).width-baseline_width*zoom/100.0f) < 0.02f,
            "layout width did not retain DIP units");
    require(renderer.layout("\xf4\x8f\xbf\xbf", false).missing > 0, "missing-glyph control failed");

    termforge::Parser parser(4,24,0);
    parser.feed("ASCII e\xcc\x81 \xe4\xb8\xad\xe6\x96\x87\r\n\x1b[1;31;44mColor\x1b[0m\r\nCursor\r\nTail");
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

int main() {
    try {
        for (const int dpi : {-1,0,47,769}) expect_rejected("dwrite", dpi, 100);
        for (const int zoom : {-1,0,49,201}) expect_rejected("gdi", 96, zoom);
        expect_rejected("invalid", 96, 100);
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
