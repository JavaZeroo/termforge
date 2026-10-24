// Launch failure remains local to the failed session and permits recovery.
#pragma once

inline void Workspace::scenario_launch_failure() {
    if (image_.empty() || closing_) return;
    require(ms(started_) < 20000, ("workspace launch failure scenario timeout at stage " + std::to_string(stage_)).c_str());
    if (stage_ == 0 && active()->ready()) {
        const auto directory = std::filesystem::path(image_).parent_path();
        tf_shell::ShellLaunch invalid{L"missing", (directory / "missing-shell.exe").wstring(), {}, directory.wstring()};
        add(Group::main, std::move(invalid));
        stage_ = 1;
    } else if (stage_ == 1 && main_active_ >= 0) {
        auto* failed = active();
        require(failed->launch_failed() && failed->launch_error().find("CreateProcess") != std::string::npos,
                "invalid executable did not produce a visible launch failure");
        require(failed->status_text().find(L"Launch failed:") != std::wstring::npos,
                "launch failure was not presented in workspace status");
        require(tabs_.size() == 1 && tabs_[0].shell->ready(), "launch failure damaged the existing cmd session");
        geometry_samples_.push_back(failed->launch_error());
        action(2);
        stage_ = 2;
    } else if (stage_ == 2 && main_tabs_.empty() && collected_ == 1) {
        tf_shell::ShellLaunch recovered = tf_shell::cmd();
        recovered.working_directory = std::filesystem::path(image_).parent_path().wstring();
        add(Group::main, std::move(recovered));
        active()->command(L"echo TF_RECOVERED\r");
        stage_ = 3;
    } else if (stage_ == 3 && active()->contains_line("TF_RECOVERED")) {
        require(tabs_.size() == 1 && main_tabs_.size() == 1 && collected_ == 1,
                "recovery created or closed an unrelated session");
        Bitmap frame(physical_width_, physical_height_);
        paint(frame.dc);
        const RECT main = physical(main_terminal_), bottom = physical(terminal_);
        selected(Group::main)->render_to(frame.dc, main.left, main.top);
        selected(Group::bottom)->render_to(frame.dc, bottom.left, bottom.top);
        frame.save(image_);
        std::ofstream out(std::filesystem::path(image_).parent_path() / "launch-failure.json");
        out << "{\"schema\":\"termforge-launch-failure-v1\",\"error\":";
        termforge::Screen::quoted(out, geometry_samples_.front());
        out << ",\"recovered\":true,\"sessions_closed\":3,\"presented_done\":null}\n";
        require(bool(out), "launch failure evidence write failed");
        stage_ = 100;
        close_all();
    }
}
