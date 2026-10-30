// Real local shell launch contracts on owned workspaces and isolated directories.
#pragma once
#include "transport_completion.hpp"

inline void Workspace::scenario_shells() {
    if (image_.empty() || closing_) return;
    if (ms(started_) >= 30000) {
        Bitmap frame(physical_width_, physical_height_);
        paint(frame.dc);
        const RECT main = physical(main_terminal_), bottom = physical(terminal_);
        selected(Group::main)->render_to(frame.dc, main.left, main.top);
        selected(Group::bottom)->render_to(frame.dc, bottom.left, bottom.top);
        frame.save(image_);
        // #80: which readiness condition was actually missing when the deadline hit.
        const auto diagnostic_directory = std::filesystem::path(image_).parent_path();
        for (const auto group : {Group::bottom, Group::main}) for (auto& tab : group_tabs(group)) {
            if (!tab.launch) continue;
            const auto [columns, rows] = tab.shell->grid_dimensions();
            const std::string role(tab.launch->kind.begin(), tab.launch->kind.end());
            // WSL also has to show a fresh prompt after its marker; record that
            // condition explicitly instead of leaving it to inference.
            const bool transport_completed = tab.launch->kind != L"wsl" ||
                tf_shell_test::transport_output_completed(
                    tab.shell->observed_output(),
                    "TF_SIZE_" + std::to_string(columns) + "x" + std::to_string(rows));
            tf_shell_test::local_shell_size_readiness(
                diagnostic_directory, role, static_cast<unsigned long>(columns), static_cast<unsigned long>(rows),
                tab.shell->settled(),
                tab.shell->contains_line("TF_SIZE_" + std::to_string(columns) + "x" + std::to_string(rows)),
                transport_completed,
                static_cast<unsigned long>(tab.shell->observed_output().size()));
        }
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
        wchar_t fence[2]{};
        if(GetEnvironmentVariableW(L"TERMFORGE_TEST_SHELL_SIZE_FENCE",fence,2)==1 && fence[0]==L'1') {
            shell_size_event_=L"Local\\TermForgeShellSize-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
            shell_size_release_.reset(CreateEventW(nullptr,TRUE,FALSE,shell_size_event_.c_str()));
            require(shell_size_release_.get() && GetLastError()!=ERROR_ALREADY_EXISTS,"size helper release event creation failed");
        }
        for (const auto group : {Group::bottom, Group::main}) for (auto& tab : group_tabs(group)) {
            tab.shell->begin_output_observation();
            tab.shell->check_size(tab.launch && tab.launch->kind==L"wsl"?shell_size_event_:L"");
        }
    };
    auto size_markers_ready = [&] {
        for (const auto group : {Group::bottom, Group::main}) for (auto& tab : group_tabs(group)) {
            const auto [columns, rows] = tab.shell->grid_dimensions();
            if (!tab.shell->settled() || !tab.shell->contains_line("TF_SIZE_" + std::to_string(columns) + "x" + std::to_string(rows)))
                return false;
        }
        return true;
    };
    auto sizes_ready = [&] {
        if(!size_markers_ready()) return false;
        for(const auto group : {Group::bottom, Group::main}) for(auto& tab : group_tabs(group)) {
            if(!tab.launch || tab.launch->kind!=L"wsl") continue;
            const auto [columns,rows]=tab.shell->grid_dimensions();
            // WSL's native helper can still own console input after its marker.
            // Wait for its shell's fresh prompt before starting sequential exits.
            if(!tf_shell_test::transport_output_completed(tab.shell->observed_output(),
                    "TF_SIZE_"+std::to_string(columns)+"x"+std::to_string(rows))) return false;
        }
        return true;
    };

    if(stage_==5 && shell_size_release_.get() && !shell_size_fence_observed_ && size_markers_ready()) {
        require(!sizes_ready(),"size output was mistaken for shell input readiness");
        tf_shell_test::local_shell_progress(directory,"wsl","size-helper-held",stage_);
        require(SetEvent(shell_size_release_.get())!=FALSE,"size helper release failed");
        shell_size_fence_observed_=true;
    }

    if (stage_ == 0 && active()->ready()) {
        active()->begin_output_observation();
        tf_shell_test::local_shell_progress(directory,"cmd","started",stage_);
        active()->command(L"if /I \"%CD%\"==\"" + expected_directory + L"\" echo TF_CMD_CWD_OK\r");
        stage_ = 1;
    } else if (stage_ == 1 && active()->contains_line("TF_CMD_CWD_OK")) {
        tf_shell_test::local_shell_progress(directory,"cmd","cwd-verified",stage_);
        add(Group::main, with_directory(tf_shell::powershell(false)));
        active()->begin_output_observation();
        tf_shell_test::local_shell_progress(directory,"powershell","started",stage_);
        active()->command(L"if ($PWD.Path -eq '" + expected_directory + L"') { 'TF_PS51_CWD_OK' }\r");
        stage_ = 2;
    } else if (stage_ == 2 && active()->contains_line("TF_PS51_CWD_OK")) {
        tf_shell_test::local_shell_progress(directory,"powershell","cwd-verified",stage_);
        add(Group::bottom, with_directory(tf_shell::powershell(true)));
        active()->begin_output_observation();
        tf_shell_test::local_shell_progress(directory,"pwsh","started",stage_);
        active()->command(L"if ($PWD.Path -eq '" + expected_directory + L"') { 'TF_PS7_CWD_OK' }\r");
        stage_ = 3;
    } else if (stage_ == 3 && active()->contains_line("TF_PS7_CWD_OK")) {
        tf_shell_test::local_shell_progress(directory,"pwsh","cwd-verified",stage_);
        add(Group::main, tf_shell::wsl(L"Ubuntu", expected_directory));
        active()->begin_output_observation();
        tf_shell_test::local_shell_progress(directory,"wsl","started",stage_);
        active()->command(L"printf '\\nTF_WSL\\n'; test \"$PWD\" = '" + std::wstring(expected_wsl_directory.begin(), expected_wsl_directory.end()) + L"' && printf 'TF_WSL_CWD_OK\\n'\r");
        stage_ = 4;
    } else if (stage_ == 4 && active()->contains_line("TF_WSL_CWD_OK")) {
        tf_shell_test::local_shell_progress(directory,"wsl","cwd-verified",stage_);
        check_size();
        stage_ = 5;
    } else if (stage_ == 5 && sizes_ready()) {
        require(session_count() == 4 && tabs_.size() == 2 && main_tabs_.size() == 2,
                "local shell targets were not isolated");
        tf_shell_test::local_shell_progress(directory,"wsl","sizes-ready",stage_);
        Bitmap frame(physical_width_, physical_height_);
        paint(frame.dc);
        const RECT main = physical(main_terminal_), bottom = physical(terminal_);
        selected(Group::main)->render_to(frame.dc, main.left, main.top);
        selected(Group::bottom)->render_to(frame.dc, bottom.left, bottom.top);
        frame.save(image_);
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
        require(failure_.empty(),"local shell exit verification failed");
        // Only publish normal-exit evidence after every real shell was collected.
        std::ofstream out(directory / "shells.json");
        out << "{\"schema\":\"termforge-local-shells-v1\",\"shells\":[\"cmd\",\"powershell\",\"pwsh\",\"wsl-Ubuntu\"],"
            << "\"working_directory\":";
        termforge::Screen::quoted(out, utf8(expected_directory.c_str()));
        out << ",\"main_sessions\":2,\"bottom_sessions\":2,\"normal_exit\":true,\"presented_done\":null}\n";
        out.close(); require(bool(out), "local shell evidence write failed");
        stage_ = 100;
        close_all();
    }
}
