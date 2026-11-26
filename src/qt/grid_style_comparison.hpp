// WindTerm identifies equal cell rasters across the entire recording. Compare
// these relations in the product's own font/palette, and verify the text
// independently in the Qt model and the child's native console.
struct GridStyleComparison {
    static constexpr int columns = 91, rows = 25;
    std::array<QImage, 21> templates;
    std::array<bool, 21> calibrated{};

    int check(int phase, const QJsonObject& record, const SessionModel& model,
              termforge::qt::TerminalWidget& terminal, const std::string& native) {
        std::vector<int> classes;
        for (const auto run : record.value("runs").toArray()) {
            const auto pair = run.toArray();
            const int id = pair.at(0).toInt(-1), length = pair.at(1).toInt(-1);
            require(pair.size() == 2 && id >= 0 && id <= 21 && length > 0 &&
                    length <= static_cast<int>(columns * rows - classes.size()), "bad reference cell run");
            classes.insert(classes.end(), length, id);
        }
        require(classes.size() == columns * rows && native.size() == classes.size(), "incomplete grid");
        const bool alternate = record.value("alternate").toBool();
        require((model.screen().active_buffer() == termforge::ScreenBuffer::alternate) == alternate,
                "wrong active screen");
        // The original highlights the caret row and the overline bleeds one
        // pixel into the preceding row. Neither adds terminal ink to those cells.
        for (auto& id : classes) if (id == 21 || id == 5) id = 0;
        if (phase == 1 || phase == 3 || phase == 10)
            std::fill(classes.begin(), classes.end(), 0); // show the active empty alternate immediately
        if (phase == 8 || phase == 9 || phase == 12) {
            classes[15 * columns + 4] = classes[15 * columns + 6] = 6; // visible post-RIS writes
        }
        const QImage image = terminal.grab().toImage();
        const int w = terminal.cell_width(), h = terminal.cell_height(), margin = terminal.content_margin();
        require(image.devicePixelRatio() == 1 && image.width() >= margin + columns * w &&
                image.height() >= margin + rows * h, "unexpected comparison surface");
        int pixels = 0, model_cells = 0, native_cells = 0;
        for (int i = 0; i < columns * rows; ++i) {
            const int col = i % columns, row = i / columns, id = classes[i];
            char ch = ' ';
            if (id >= 1 && id <= 4) ch = "GRID"[id - 1];
            if ((id >= 6 && id <= 10) || id == 12 || id == 14 || id == 16 || id == 18 || id == 20) ch = 'M';
            // The hidden controls occupy two M cells despite matching bare paper.
            const bool controls = ((phase == 0 || phase == 5 || (phase >= 7 && phase <= 9) || phase == 12) && row == 3)
                                  || (phase == 2 && row == 9);
            if (controls && (col == 49 || col == 51)) ch = 'M';
            const auto& cell = model.screen().visible_lines().at(row).at(col);
            auto tile = image.copy(margin + col * w, margin + row * h, w, h);
            if (phase == 0 && !calibrated[id]) {
                templates[id] = tile;
                calibrated[id] = true;
            }
            require(calibrated[id], "missing primary calibration cell");
            if (model.cursor_visible() && model.running() && terminal.display_caret_row() == row &&
                terminal.display_caret_column() == col) {
                // Check the caret and its backing cell separately.
                for (int y = h - 2; y < h; ++y) for (int x = 0; x < w; ++x) {
                    require(tile.pixelColor(x,y) == QColor(216,222,228), "unexpected cursor ink");
                    tile.setPixelColor(x,y,templates[id].pixelColor(x,y));
                }
            }
            const bool wrong_image = tile != templates[id], wrong_model = cell.text != std::string(1, ch);
            const bool wrong_native = native[i] != ch;
            if ((wrong_image || wrong_model || wrong_native) && pixels + model_cells + native_cells < 8)
                std::cerr << "phase " << phase << " row " << row+1 << " col " << col+1 << " class " << id
                          << " text " << cell.text << " native " << native[i] << " raster_diff " << wrong_image << '\n';
            pixels += wrong_image;
            model_cells += wrong_model;
            native_cells += wrong_native;
        }
        if (phase == 0) {
            for (size_t i = 0; i < templates.size(); ++i) {
                if (i == 5) continue;
                require(calibrated[i], "missing style control");
                for (size_t j = 0; j < i; ++j) if (j != 5 && templates[i] == templates[j]) {
                    std::cerr << "collapsed style classes " << i << " and " << j << '\n';
                    ++pixels;
                }
            }
            // Spaces anchor the directions of the decorations, including reverse and palette fills.
            const auto paper = templates[0].pixelColor(0,0);
            const bool styled_spaces = templates[11].pixelColor(w/2,h-1) != paper && templates[17].pixelColor(w/2,0) != paper &&
                    templates[15].pixelColor(w/2,h/2) != paper && templates[13].pixelColor(0,0) == QColor(216,222,228) &&
                    templates[19].pixelColor(0,0) == QColor(89,155,218);
            if (!styled_spaces) { std::cerr << "incorrect styled spaces\n"; ++pixels; }
        }
        std::cerr << "style grid phase=" << phase << " cells=" << classes.size() << " pixel_diffs=" << pixels
                  << " text_diffs=" << model_cells << " native_diffs=" << native_cells << '\n';
        return pixels + model_cells + native_cells;
    }
};
