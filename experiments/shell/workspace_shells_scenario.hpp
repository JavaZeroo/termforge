// Real local shell launch contracts on owned workspaces and isolated directories.
#pragma once

inline void Workspace::scenario_shells() {
    if (image_.empty() || closing_) return;
    if (ms(started_) >= 30000) {
        Bitmap frame(physical_width_, physical_height_);
        paint(frame.dc);
        const RECT main = physical(main_terminal_), bottom = physical(terminal_);
        selected(Group::main)->render_to(frame.dc, main.left, main.top);
        selected(Group::bottom)->render_to(frame.dc, bottom.left, bottom.top);
        frame.save(image_);
        require(false, ("workspace shells scenario timeout at stage " + std::to_string(stage_)).c_str());
    }
    const auto directory = std::filesystem::path(image_).parent_path();
    const auto expected_directory = directory.wstring();
    const auto native = directory.generic_string();
    require(native.size() >= 3 && native[1] == ':', "WSL test requires a Windows drive path");
    const std::string expected_wsl_directory = "/mnt/" + std::string(1, static_cast<char>(std::tolower(native[0]))) + native.substr(2);
    auto with_directory = [&](tf_shell::ShellLaunch launch) {
        launch.working_directory = expected_directory;
        return launch;
    };
    auto check_size = [&] {
        for (const auto group : {Group::bottom, Group::main}) for (auto& tab : group_tabs(group))
            tab.shell->check_size();
    };
    auto sizes_ready = [&] {
        for (const auto group : {Group::bottom, Group::main}) for (auto& tab : group_tabs(group)) {
            const auto [columns, rows] = tab.shell->grid_dimensions();
            if (!tab.shell->settled() || !tab.shell->contains_line("TF_SIZE_" + std::to_string(columns) + "x" + std::to_string(rows)))
                return false;
        }
        return true;
    };

    if (stage_ == 0 && active()->ready()) {
        active()->command(L"if /I \"%CD%\"==\"" + expected_directory + L"\" echo TF_CMD_CWD_OK\r");
        stage_ = 1;
    } else if (stage_ == 1 && active()->contains_line("TF_CMD_CWD_OK")) {
        add(Group::main, with_directory(tf_shell::powershell(false)));
        active()->command(L"if ($PWD.Path -eq '" + expected_directory + L"') { 'TF_PS51_CWD_OK' }\r");
        stage_ = 2;
    } else if (stage_ == 2 && active()->contains_line("TF_PS51_CWD_OK")) {
        add(Group::bottom, with_directory(tf_shell::powershell(true)));
        active()->command(L"if ($PWD.Path -eq '" + expected_directory + L"') { 'TF_PS7_CWD_OK' }\r");
        stage_ = 3;
    } else if (stage_ == 3 && active()->contains_line("TF_PS7_CWD_OK")) {
        add(Group::main, tf_shell::wsl(L"Ubuntu", expected_directory));
        active()->command(L"printf '\\nTF_WSL\\n'; test \"$PWD\" = '" + std::wstring(expected_wsl_directory.begin(), expected_wsl_directory.end()) + L"' && printf 'TF_WSL_CWD_OK\\n'\r");
        stage_ = 4;
    } else if (stage_ == 4 && active()->contains_line("TF_WSL_CWD_OK")) {
        check_size();
        stage_ = 5;
    } else if (stage_ == 5 && sizes_ready()) {
        require(session_count() == 4 && tabs_.size() == 2 && main_tabs_.size() == 2,
                "local shell targets were not isolated");
        Bitmap frame(physical_width_, physical_height_);
        paint(frame.dc);
        const RECT main = physical(main_terminal_), bottom = physical(terminal_);
        selected(Group::main)->render_to(frame.dc, main.left, main.top);
        selected(Group::bottom)->render_to(frame.dc, bottom.left, bottom.top);
        frame.save(image_);
        std::ofstream out(std::filesystem::path(image_).parent_path() / "shells.json");
        out << "{\"schema\":\"termforge-local-shells-v1\",\"shells\":[\"cmd\",\"powershell\",\"pwsh\",\"wsl-Ubuntu\"],"
            << "\"working_directory\":";
        termforge::Screen::quoted(out, utf8(expected_directory.c_str()));
        out << ",\"main_sessions\":2,\"bottom_sessions\":2,\"normal_exit\":true,\"presented_done\":null}\n";
        require(bool(out), "local shell evidence write failed");
        active()->command(L"exit\r");
        stage_ = 6;
    } else if (stage_ == 6 && collected_ == 1 && session_count() == 3) {
        select(1, Group::bottom);
        active()->command(L"exit\r");
        stage_ = 7;
    } else if (stage_ == 7 && collected_ == 2 && session_count() == 2) {
        select(0, Group::main);
        active()->command(L"exit\r");
        stage_ = 8;
    } else if (stage_ == 8 && collected_ == 3 && session_count() == 1) {
        select(0, Group::bottom);
        active()->command(L"exit\r");
        stage_ = 9;
    } else if (stage_ == 9 && collected_ == 4 && session_count() == 0) {
        stage_ = 100;
        close_all();
    }
}
