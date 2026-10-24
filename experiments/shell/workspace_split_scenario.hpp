// One real left/right main split with independent sessions and focus.
#pragma once

inline void Workspace::scenario_split() {
    if (image_.empty() || closing_) return;
    require(ms(started_) < 20000, ("workspace split scenario timeout at stage " + std::to_string(stage_)).c_str());
    auto rect_text = [](RECT rect) {
        return "[" + std::to_string(rect.left) + "," + std::to_string(rect.top) + "," +
            std::to_string(rect.right) + "," + std::to_string(rect.bottom) + "]";
    };
    auto actual_rect = [&](HWND child) {
        RECT rect{};
        require(GetWindowRect(child, &rect) != FALSE, "split child window query failed");
        POINT origin{rect.left, rect.top};
        require(ScreenToClient(window_, &origin) != FALSE, "split child origin query failed");
        OffsetRect(&rect, origin.x - rect.left, origin.y - rect.top);
        return rect;
    };
    auto record = [&](const char* phase) {
        const RECT left = actual_rect(main_tabs_[0].shell->handle());
        const RECT right = actual_rect(main_tabs_[1].shell->handle());
        const LONG middle = (main_terminal_.left + main_terminal_.right) / 2;
        const RECT expected_left{main_terminal_.left, main_terminal_.top, middle, main_terminal_.bottom};
        const RECT expected_right{middle, main_terminal_.top, main_terminal_.right, main_terminal_.bottom};
        require(EqualRect(&left, &expected_left) && EqualRect(&right, &expected_right),
                "split physical rectangles mismatch");
        const bool left_visible = IsWindowVisible(main_tabs_[0].shell->handle()) != FALSE;
        const bool right_visible = IsWindowVisible(main_tabs_[1].shell->handle()) != FALSE;
        require(left_visible && right_visible, "split panes are not both visible");
        geometry_samples_.push_back("{\"phase\":\"" + std::string(phase) + "\",\"split_active\":true,\"right_focus\":" +
            (split_right_focus_ ? "true" : "false") + ",\"left_rect\":" + rect_text(left) +
            ",\"right_rect\":" + rect_text(right) + ",\"left_visible\":true,\"right_visible\":true}");
    };

    if (stage_ == 0 && active()->ready()) {
        add(Group::main);
        stage_ = 1;
    } else if (stage_ == 1 && active()->ready()) {
        split();
        require(split_active_ && split_right_focus_ && main_tabs_.size() == 2 && split_index_ == 1,
                "split did not create a second real pane");
        active()->command(L"echo TF_RIGHT\r");
        focus_split(false);
        active()->command(L"echo TF_LEFT\r");
        focus_split(true);
        stage_ = 2;
    } else if (stage_ == 2 && main_tabs_[0].shell->contains_line("TF_LEFT") &&
               main_tabs_[1].shell->contains_line("TF_RIGHT")) {
        require(!main_tabs_[0].shell->contains_line("TF_RIGHT") && !main_tabs_[1].shell->contains_line("TF_LEFT"),
                "split output crossed sessions");
        record("active");
        zoom_split();
        require(!split_active_ && !IsWindowVisible(main_tabs_[1].shell->handle()), "zoom did not hide the right pane");
        main_tabs_[1].shell->command(L"echo TF_SPLIT_BACKGROUND\r");
        stage_ = 3;
    } else if (stage_ == 3 && main_tabs_[1].shell->contains_line("TF_SPLIT_BACKGROUND")) {
        zoom_split();
        require(split_active_ && split_right_focus_, "zoom did not restore split focus");
        record("restored");
        focus_split(false);
        active()->command(L"echo TF_AFTER_SPLIT\r");
        stage_ = 4;
    } else if (stage_ == 4 && main_tabs_[0].shell->contains_line("TF_AFTER_SPLIT")) {
        Bitmap split_frame(physical_width_, physical_height_);
        paint(split_frame.dc);
        const LONG middle = (main_terminal_.left + main_terminal_.right) / 2;
        main_tabs_[0].shell->render_to(split_frame.dc, physical(main_terminal_.left), physical(main_terminal_.top));
        main_tabs_[1].shell->render_to(split_frame.dc, physical(middle), physical(main_terminal_.top));
        split_frame.save(std::filesystem::path(image_).parent_path().replace_filename("split.bmp").string());
        focus_split(true);
        shortcut('X', false, false, true);
        stage_ = 5;
    } else if (stage_ == 5 && main_tabs_.size() == 1 && !split_active_ && collected_ == 1) {
        active()->command(L"echo TF_LEFT_ALIVE\r");
        stage_ = 6;
    } else if (stage_ == 6 && active()->contains_line("TF_LEFT_ALIVE")) {
        Bitmap frame(physical_width_, physical_height_);
        paint(frame.dc);
        const RECT main = physical(main_terminal_), bottom = physical(terminal_);
        selected(Group::main)->render_to(frame.dc, main.left, main.top);
        selected(Group::bottom)->render_to(frame.dc, bottom.left, bottom.top);
        frame.save(image_);
        std::ofstream out(std::filesystem::path(image_).parent_path() / "split.json");
        out << "{\"schema\":\"termforge-main-split-v1\",\"independent_output\":true,\"background_output\":true,"
            << "\"restored\":true,\"right_closed\":true,\"left_alive\":true,\"snapshots\":[";
        for (size_t i = 0; i < geometry_samples_.size(); ++i) {
            if (i) out << ',';
            out << geometry_samples_[i];
        }
        out << "],\"sessions_closed\":3,\"presented_done\":null}\n";
        require(bool(out), "split evidence write failed");
        stage_ = 100;
        close_all();
    }
}
