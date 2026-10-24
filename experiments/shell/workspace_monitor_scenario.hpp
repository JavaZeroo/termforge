// Real Win32 monitor moves on owned HWNDs; physical presentation is not observed.
#pragma once
#include <shellscalingapi.h>

namespace tf_shell_test {
struct MonitorArea {
    HMONITOR handle{};
    RECT monitor{}, work{};
    int dpi = 0;
};

inline int monitor_effective_dpi(HMONITOR monitor) {
    using GetDpi = HRESULT(WINAPI*)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);
    const HMODULE shcore = LoadLibraryW(L"shcore.dll");
    require(shcore != nullptr, "monitor DPI library unavailable");
    const auto get_dpi = reinterpret_cast<GetDpi>(GetProcAddress(shcore, "GetDpiForMonitor"));
    require(get_dpi != nullptr, "monitor DPI API unavailable");
    UINT x = 0, y = 0;
    const HRESULT result = get_dpi(monitor, MDT_EFFECTIVE_DPI, &x, &y);
    FreeLibrary(shcore);
    require(SUCCEEDED(result) && x == y && x != 0, "monitor effective DPI query failed");
    return static_cast<int>(x);
}

inline std::vector<MonitorArea> monitor_areas() {
    std::vector<MonitorArea> result;
    const auto callback = [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        MONITORINFO info{sizeof(info)};
        require(GetMonitorInfoW(monitor, &info) != FALSE, "monitor information query failed");
        auto& areas = *reinterpret_cast<std::vector<MonitorArea>*>(data);
        areas.push_back({monitor, info.rcMonitor, info.rcWork, monitor_effective_dpi(monitor)});
        return TRUE;
    };
    require(EnumDisplayMonitors(nullptr, nullptr, callback, reinterpret_cast<LPARAM>(&result)) != FALSE,
            "monitor enumeration failed");
    return result;
}

inline std::string monitor_rect(RECT rect) {
    return "[" + std::to_string(rect.left) + "," + std::to_string(rect.top) + "," +
        std::to_string(rect.right) + "," + std::to_string(rect.bottom) + "]";
}
}

inline void Workspace::scenario_monitors() {
    using tf_shell_test::MonitorArea;
    if (image_.empty() || closing_) return;
    require(ms(started_) < 30000, "workspace monitor scenario timeout");
    static MonitorArea origin, target;
    auto move_to = [&](const MonitorArea& area) {
        RECT frame{0, 0, MulDiv(1360, area.dpi, 96), MulDiv(860, area.dpi, 96)};
        require(AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, area.dpi) != FALSE,
                "monitor frame calculation failed");
        const int width = frame.right - frame.left, height = frame.bottom - frame.top;
        const int x = area.work.left + (area.work.right - area.work.left - width) / 2;
        const int y = area.work.top + (area.work.bottom - area.work.top - height) / 2;
        require(SetWindowPos(window_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                "real monitor window move failed");
    };
    auto record = [&](const char* phase, const MonitorArea& expected) {
        require(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST) == expected.handle,
                "window did not reach the expected real monitor");
        require(GetDpiForWindow(window_) == static_cast<UINT>(expected.dpi) && dpi_ == expected.dpi,
                "window and workspace DPI differ after real monitor move");
        require(rename_ && GetFocus() == rename_, "real monitor move lost rename focus");
        LOGFONTW description{};
        require(GetObjectW(font_, sizeof(description), &description) == sizeof(description) &&
                description.lfHeight == -MulDiv(9, dpi_, 72) && ui_font_->face() == L"Roboto Mono",
                "real monitor move changed the UI font contract");
        RECT editor_expected = physical(rename_rect(rename_group_, selection(rename_group_)));
        POINT editor_origin{editor_expected.left, editor_expected.top};
        require(ClientToScreen(window_, &editor_origin) != FALSE, "monitor editor origin query failed");
        OffsetRect(&editor_expected, editor_origin.x - editor_expected.left, editor_origin.y - editor_expected.top);
        RECT editor_actual{};
        require(GetWindowRect(rename_, &editor_actual) != FALSE && EqualRect(&editor_actual, &editor_expected) != FALSE,
                "real monitor move misplaced the rename editor");
        wchar_t text[65]{};
        GetWindowTextW(rename_, text, 65);
        require(std::wstring(text) == L"Monitor pending", "real monitor move changed rename text");
        RECT outer{};
        require(GetWindowRect(window_, &outer) != FALSE, "monitor window rectangle query failed");
        std::string sample = "{\"phase\":\"" + std::string(phase) + "\",\"window_rect\":" +
            tf_shell_test::monitor_rect(outer) + ",\"window_dpi\":" + std::to_string(GetDpiForWindow(window_)) +
            ",\"rename_focused\":true,\"sessions\":[";
        bool first = true;
        for (const auto group : {Group::bottom, Group::main}) for (size_t i = 0; i < group_tabs(group).size(); ++i) {
            auto& tab = group_tabs(group)[i];
            auto* shell = tab.shell.get();
            const RECT logical_rect = group == Group::main ? main_terminal_ : terminal_;
            const RECT expected_rect = physical(logical_rect);
            RECT actual{}, client{};
            require(GetWindowRect(shell->handle(), &actual) != FALSE, "monitor shell rectangle query failed");
            POINT origin_point{actual.left, actual.top};
            require(ScreenToClient(window_, &origin_point) != FALSE, "monitor shell origin query failed");
            OffsetRect(&actual, origin_point.x - actual.left, origin_point.y - actual.top);
            require(EqualRect(&actual, &expected_rect) != FALSE, "monitor shell geometry mismatch");
            require(shell->display_dpi() == expected.dpi && shell->frame_matches_full(),
                    "monitor shell font or retained frame mismatch");
            require(GetClientRect(shell->handle(), &client) != FALSE, "monitor shell client query failed");
            const auto [columns, rows] = shell->grid_dimensions();
            const auto [cell_width, cell_height] = shell->pixel_cell_size();
            require(columns == client.right / cell_width && rows == client.bottom / cell_height,
                    "monitor shell grid does not fit its real client");
            if (!first) sample += ',';
            first = false;
            sample += "{\"group\":\"" + std::string(group == Group::main ? "main" : "bottom") +
                "\",\"index\":" + std::to_string(i) + ",\"visible\":" +
                (IsWindowVisible(shell->handle()) ? "true" : "false") + ",\"display_dpi\":" +
                std::to_string(shell->display_dpi()) + ",\"grid\":[" + std::to_string(columns) + "," +
                std::to_string(rows) + "],\"cell_pixels\":[" + std::to_string(cell_width) + "," +
                std::to_string(cell_height) + "]}";
        }
        sample += "]}";
        geometry_samples_.push_back(std::move(sample));
    };

    if (stage_ == 0 && active()->ready()) {
        add(Group::main);
        add();
        stage_ = 1;
    } else if (stage_ == 1 && session_count() == 3) {
        bool ready = true;
        for (const auto group : {Group::bottom, Group::main}) for (auto& tab : group_tabs(group)) {
            ready = ready && tab.shell->ready() && tab.shell->settled();
            tab.shell->check_size();
        }
        if (!ready) return;
        const auto areas = tf_shell_test::monitor_areas();
        require(areas.size() >= 2, "real monitor scenario requires at least two monitors");
        const HMONITOR current = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
        const auto current_it = std::find_if(areas.begin(), areas.end(), [&](const auto& area) {
            return area.handle == current;
        });
        require(current_it != areas.end(), "current monitor identity missing");
        origin = *current_it;
        target = areas.front().handle == current ? areas[1] : areas.front();
        begin_rename(selection(Group::main), Group::main);
        SetWindowTextW(rename_, L"Monitor pending");
        record("origin", origin);
        move_to(target);
        stage_ = 2;
    } else if (stage_ == 2 && MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST) == target.handle &&
               GetDpiForWindow(window_) == static_cast<UINT>(target.dpi)) {
        record("target", target);
        move_to(origin);
        stage_ = 3;
    } else if (stage_ == 3 && MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST) == origin.handle &&
               GetDpiForWindow(window_) == static_cast<UINT>(origin.dpi)) {
        record("restored", origin);
        Bitmap frame(physical_width_, physical_height_);
        paint(frame.dc);
        const RECT main = physical(main_terminal_), bottom = physical(terminal_);
        selected(Group::main)->render_to(frame.dc, main.left, main.top);
        selected(Group::bottom)->render_to(frame.dc, bottom.left, bottom.top);
        frame.save(image_);
        std::ofstream out(std::filesystem::path(image_).parent_path() / "monitors.json");
        out << "{\"schema\":\"termforge-ui-monitors-v1\",\"real_monitor_transition\":true,\"distinct_dpi\":"
            << (origin.dpi != target.dpi ? "true" : "false") << ",\"presented_done\":null,\"monitors\":["
            << "{\"role\":\"origin\",\"rect\":" << tf_shell_test::monitor_rect(origin.monitor)
            << ",\"work\":" << tf_shell_test::monitor_rect(origin.work) << ",\"dpi\":" << origin.dpi << "},"
            << "{\"role\":\"target\",\"rect\":" << tf_shell_test::monitor_rect(target.monitor)
            << ",\"work\":" << tf_shell_test::monitor_rect(target.work) << ",\"dpi\":" << target.dpi << "}],"
            << "\"snapshots\":[";
        for (size_t i = 0; i < geometry_samples_.size(); ++i) {
            if (i) out << ',';
            out << geometry_samples_[i];
        }
        out << "]}\n";
        require(bool(out), "monitor evidence write failed");
        end_rename(true);
        stage_ = 100;
        close_all();
    }
}
