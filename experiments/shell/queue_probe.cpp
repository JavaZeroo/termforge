// Bounded queue contracts and owned-child, data-only ConPTY worker transitions.
#include "queues.hpp"
#include "resize.hpp"
#include <future>
#include <iostream>

static void check(bool condition, const char* detail) {
    if (!condition) throw std::runtime_error(detail);
}
template<class Predicate> static void await(Predicate predicate, const char* detail) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!predicate()) {
        check(std::chrono::steady_clock::now() < deadline, detail);
        std::this_thread::yield();
    }
}
static std::wstring executable_path() {
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size == path.size()) tf_conpty::fail("GetModuleFileName");
    path.resize(size); return path;
}
static int child(const wchar_t* ready_name, bool prime_input) {
    tf_conpty::Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, ready_name));
    if (!ready.get()) tf_conpty::fail("OpenEvent(queue child)");
    const std::wstring query_name = std::wstring(ready_name) + L"-query";
    tf_conpty::Handle query(OpenEventW(SYNCHRONIZE, FALSE, query_name.c_str()));
    if (!query.get()) tf_conpty::fail("OpenEvent(queue query)");
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE), output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT) ||
        !SetConsoleMode(output, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        tf_conpty::fail("SetConsoleMode(queue child)");
    if (!SetEvent(ready.get())) tf_conpty::fail("SetEvent(queue child)");
    size_t prefix_read = 0;
    if (prime_input) {
        tf_conpty::Handle primed(OpenEventW(EVENT_MODIFY_STATE, FALSE, (std::wstring(ready_name) + L"-primed").c_str()));
        tf_conpty::Handle flood(OpenEventW(SYNCHRONIZE, FALSE, (std::wstring(ready_name) + L"-flood").c_str()));
        check(primed.get() && flood.get(), "open queue input phase gates");
        // A host's unread-input capacity is not a portable 16 KiB guarantee.
        // Consume and verify exactly that prefix, then never read again. The
        // sender still submits the original 4 MiB through the real InputQueue.
        while (prefix_read < 16384) {
            char bytes[256]; DWORD count = 0;
            check(ReadFile(input, bytes, sizeof bytes, &count, nullptr) && count && count <= 16384 - prefix_read,
                  "read queue priming prefix");
            check(std::string_view(bytes, count).find_first_not_of('a') == std::string_view::npos,
                  "queue priming prefix differs from independent ASCII oracle");
            prefix_read += count;
        }
        check(SetEvent(primed.get()) != FALSE, "signal verified queue priming prefix");
        check(WaitForSingleObject(flood.get(), 3000) == WAIT_OBJECT_0, "queue flood phase release");
    }
    // From this point input is never read. Output fills while the parent reader
    // is held at its explicit gate; the old observation mode never primes input.
    const std::string bytes(65536, 'F');
    for (;;) {
        if (WaitForSingleObject(query.get(), 0) == WAIT_OBJECT_0) {
            CONSOLE_SCREEN_BUFFER_INFO dimensions{};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            for (;;) {
                if (!GetConsoleScreenBufferInfo(output, &dimensions)) tf_conpty::fail("GetConsoleScreenBufferInfo(queue child)");
                if ((dimensions.dwSize.X == 102 && dimensions.dwSize.Y == 42) || std::chrono::steady_clock::now() >= deadline) break;
                std::this_thread::yield();
            }
            const std::string reply = "\r\nTFQUEUE-SIZE:" + std::to_string(dimensions.dwSize.X) + "x" +
                                      std::to_string(dimensions.dwSize.Y) + ":END\r\nTFQUEUE-PRIMED:" +
                                      std::to_string(prefix_read) + ":END\r\n";
            DWORD count = 0;
            if (!WriteFile(output, reply.data(), static_cast<DWORD>(reply.size()), &count, nullptr) || count != reply.size())
                tf_conpty::fail("WriteFile(queue size)");
            return 0; // Keep the queried line stable instead of flooding over it.
        }
        DWORD count = 0;
        if (!WriteFile(output, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) || !count)
            tf_conpty::fail("WriteFile(queue child)");
    }
}
static void bounded_queues() {
    OutputQueue output;
    const auto empty = output.snapshot();
    check(!empty.queued_bytes && !empty.high_water_bytes && !empty.stopped, "new output state");
    for (int i = 0; i < 4; ++i) output.push(std::string(65536, 'x'));
    auto pending = std::async(std::launch::async, [&] { output.push(std::string(65536, 'y')); });
    check(pending.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout, "full output blocks producer");
    const auto full = output.snapshot();
    check(full.queued_bytes == OutputQueue::capacity && full.high_water_bytes == OutputQueue::capacity && !full.stopped,
          "live full output snapshot");
    check(output.take(8192) == std::string(8192, 'x'), "output first prefix");
    check(output.snapshot().queued_bytes == OutputQueue::capacity - 8192, "output partial drain snapshot");
    check(pending.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout, "partial room still blocks producer");
    check(output.take(57344) == std::string(57344, 'x'), "output remaining prefix");
    pending.get();
    check(output.take(OutputQueue::capacity) == std::string(3 * 65536, 'x') + std::string(65536, 'y'), "output FIFO");
    check(output.snapshot().queued_bytes == 0 && output.high_water == OutputQueue::capacity, "drained output preserves high water");
    for (int i = 0; i < 4; ++i) output.push(std::string(65536, 'x'));
    auto cancelled = std::async(std::launch::async, [&] { output.push("z"); });
    output.stop(); cancelled.get();
    check(output.snapshot().stopped && output.snapshot().queued_bytes == OutputQueue::capacity, "stop preserves queued output");
    check(output.take(OutputQueue::capacity) == std::string(OutputQueue::capacity, 'x'), "stopped output drains existing bytes");
    check(!output.snapshot().queued_bytes, "stopped drained output empty");
    tf_conpty::Session unstarted;
    InputQueue input(unstarted);
    check(!input.snapshot().started && !input.snapshot().joined, "unstarted input is not joined");
    for (int i = 0; i < 16; ++i) check(input.send(std::string(1024, 'a')), "input accepts bounded chunks");
    check(!input.send("b"), "full input rejects");
    const auto queued = input.snapshot();
    check(queued.queued_bytes == InputQueue::capacity && !queued.in_flight_bytes && queued.rejected == 1 &&
          queued.high_water_bytes == InputQueue::capacity && !queued.started, "queued input snapshot");
    input.stop(); check(!input.send("c"), "stopped input rejects");
    const auto stopped = input.snapshot();
    check(!stopped.queued_bytes && !stopped.in_flight_bytes && stopped.rejected == 2 && stopped.stopping &&
          !stopped.started && !stopped.joined && !stopped.failed, "unstarted stopped input snapshot");
    std::cout << "{\"case\":\"bounded\",\"output_high_water\":" << full.high_water_bytes
              << ",\"input_high_water\":" << queued.high_water_bytes << ",\"rejected\":2}\n";
}
static void lifecycle_edges() {
    tf_conpty::Session unstarted;
    ResizeWorker abandoned(unstarted);
    for (int i = 0; i < 3; ++i) abandoned.request(40 + i, 100 + i);
    const auto pending = abandoned.snapshot();
    check(!pending.working && pending.pending && pending.pending_generation == 3 && !pending.started && !pending.joined,
          "unstarted resize coalesces pending requests");
    abandoned.stop(); abandoned.request(99, 199);
    const auto stopped = abandoned.snapshot();
    check(stopped.stopping && !stopped.working && !stopped.pending && stopped.requested_generation == 3 &&
          !stopped.completed_generation && !stopped.started && !stopped.joined && !stopped.failed && !abandoned.settled(),
          "stopped unstarted resize is abandoned not complete");
    InputQueue input(unstarted);
    check(input.send("unavailable"), "unavailable input admission differs from write"); input.start();
    await([&] { return input.snapshot().failed; }, "real unavailable input failure");
    const auto failed_input = input.snapshot();
    check(failed_input.started && !failed_input.joined && !failed_input.queued_bytes && !failed_input.in_flight_bytes,
          "failed input releases worker payload before join");
    bool rethrown = false;
    try { input.send("x"); } catch (const std::exception&) { rethrown = true; }
    check(rethrown, "input snapshot does not consume stored write error");
    input.stop(); check(input.snapshot().joined && input.snapshot().failed, "input join preserves error");
    ResizeWorker invalid(unstarted); invalid.request(40, 100); invalid.start();
    await([&] { return invalid.snapshot().failed; }, "real unavailable resize failure");
    const auto failed_resize = invalid.snapshot();
    check(failed_resize.started && !failed_resize.joined && !failed_resize.working && !failed_resize.pending &&
          failed_resize.requested_generation == 1 && !failed_resize.completed_generation,
          "failed resize is neither pending nor complete");
    rethrown = false;
    try { invalid.settled(); } catch (const std::exception&) { rethrown = true; }
    check(rethrown, "resize snapshot does not consume stored error");
    invalid.stop(); check(invalid.snapshot().joined && invalid.snapshot().failed, "resize join preserves error");
    std::cout << "{\"case\":\"lifecycle-edges\",\"stopped_uncompleted_generation\":3,\"input_error_preserved\":true,"
                 "\"resize_error_preserved\":true}\n";
}
enum class WriterAction { stop, cancel, cancel_stop_race };
static void real_workers(bool fence_sender, WriterAction action) {
    std::mutex gate_mutex; std::condition_variable gate_changed;
    bool reader_entered = false, reader_released = false, resize_entered=false, resize_released=false;
    bool reader_gate_enabled = !fence_sender;
    OutputQueue output;
    tf_conpty::Session session;
    InputQueue input(session);
    ResizeWorker resize(session,[&] {
        std::unique_lock<std::mutex> lock(gate_mutex);resize_entered=true;gate_changed.notify_all();
        gate_changed.wait(lock,[&] {return resize_released;});
    });
    const auto release_gates = [&] {
        std::lock_guard<std::mutex> lock(gate_mutex); reader_released = resize_released = true; gate_changed.notify_all();
    };
    bool cleanup_started = false;
    const auto cleanup = [&] {
        if (cleanup_started) return;
        cleanup_started = true;
        release_gates(); output.stop(); input.stop();
        if (session.pid) session.cancel();
        resize.stop(); session.close();
    };
    const std::wstring name = L"Local\\TermForgeQueueReady-" + std::to_wstring(GetCurrentProcessId());
    tf_conpty::Handle ready(CreateEventW(nullptr, TRUE, FALSE, name.c_str()));
    if (!ready.get()) tf_conpty::fail("CreateEvent(queue ready)");
    tf_conpty::Handle query(CreateEventW(nullptr, FALSE, FALSE, (name + L"-query").c_str()));
    if (!query.get()) tf_conpty::fail("CreateEvent(queue query)");
    tf_conpty::Handle primed(CreateEventW(nullptr, TRUE, FALSE, (name + L"-primed").c_str()));
    tf_conpty::Handle flood(CreateEventW(nullptr, TRUE, FALSE, (name + L"-flood").c_str()));
    check(primed.get() && flood.get(), "create queue input phase gates");
    try {
        session.start(executable_path(), (fence_sender ? L"--queue-child-primed \"" : L"--queue-child \"") + name + L"\"", [&](std::string_view bytes) {
            { std::unique_lock<std::mutex> lock(gate_mutex);
              if (reader_gate_enabled) {
                  reader_entered = true; gate_changed.notify_all();
                  gate_changed.wait(lock, [&] { return reader_released; });
              } }
            output.push(bytes);
        });
        check(WaitForSingleObject(ready.get(), 3000) == WAIT_OBJECT_0, "real console child ready");
        if (!fence_sender) { std::unique_lock<std::mutex> lock(gate_mutex);
          check(gate_changed.wait_for(lock, std::chrono::seconds(3), [&] { return reader_entered; }), "real output reader reached gate"); }
        tf_shell::PreparedPaste prepared; prepared.bytes.assign(4 * 1024 * 1024, 'a');
        auto transfer = std::make_shared<tf_shell::PasteTransfer>(1, std::move(prepared));
        const auto observation_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        auto fence = fence_sender ? std::make_shared<tf_shell_test::SenderFence>(transfer, 1, observation_deadline) : nullptr;
        check(input.begin_paste(transfer, fence), "real input transfer accepted"); input.start();
        InputQueue::Snapshot held{};
        try {
            while (true) {
                held = input.snapshot();
                const bool held_fence = !fence || fence->snapshot(fence->ticket()).state == tf_shell_test::SenderFence::State::held;
                if (held_fence && held.in_flight_bytes && transfer->progress().sent >= 16384) break;
                check(std::chrono::steady_clock::now() < observation_deadline, "real input writer holds a chunk");
                std::this_thread::yield();
            }
        } catch (...) {
            const auto observed = transfer->progress();
            bool observed_reader_entered = false, observed_reader_released = false;
            { std::lock_guard<std::mutex> lock(gate_mutex);
              observed_reader_entered = reader_entered; observed_reader_released = reader_released; }
            std::cerr << "{\"case\":\"writer-observation-rejected\",\"total\":" << observed.total
                      << ",\"accepted\":" << observed.accepted << ",\"sent\":" << observed.sent
                      << ",\"paste_in_flight\":" << observed.in_flight << ",\"queue_in_flight\":" << held.in_flight_bytes
                      << ",\"state\":" << static_cast<unsigned>(observed.state)
                      << ",\"reader_entered\":" << observed_reader_entered << ",\"reader_released\":" << observed_reader_released << "}\n";
            throw;
        }
        if (fence_sender) {
            while (WaitForSingleObject(primed.get(), 0) != WAIT_OBJECT_0) {
                check(std::chrono::steady_clock::now() < observation_deadline, "real child verified the 16 KiB prefix");
                std::this_thread::yield();
            }
            { std::lock_guard<std::mutex> lock(gate_mutex); reader_gate_enabled = true; }
            check(SetEvent(flood.get()) != FALSE, "release real queue child output flood");
            std::unique_lock<std::mutex> lock(gate_mutex);
            check(gate_changed.wait_until(lock, observation_deadline, [&] { return reader_entered; }), "real output reader reached gate");
        }
        check(!held.queued_bytes && held.in_flight_bytes <= 1024 && held.started && !held.joined && !held.stopping && !held.failed,
              "dequeued input remains in flight");
        const auto before_stop = transfer->progress();
        if (action == WriterAction::cancel) {
            input.cancel_paste();
            while (!tf_shell::paste_terminal(transfer->progress().state)) {
                check(std::chrono::steady_clock::now() < observation_deadline, "real queue cancellation finishes its claimed chunk");
                std::this_thread::yield();
            }
            const auto cancelled = transfer->progress();
            check(cancelled.state == tf_shell::PasteState::cancelled && cancelled.sent == before_stop.accepted &&
                  cancelled.accepted == before_stop.accepted && !cancelled.in_flight && cancelled.retained_capacity <= 32,
                  "real queue cancellation uses send acknowledgement for its claimed chunk");
        } else if (action == WriterAction::cancel_stop_race) {
            std::promise<void> start;
            const auto go = start.get_future().share();
            std::thread cancel([&] { go.wait(); input.cancel_paste(); });
            start.set_value(); input.stop(); cancel.join();
        }
        input.stop(); const auto joined_input = input.snapshot();
        check(joined_input.started && joined_input.joined && joined_input.stopping && !joined_input.queued_bytes &&
              !joined_input.in_flight_bytes && !joined_input.failed, "input stop joins and releases held payload");
        if (fence_sender) {
            const auto stopped_fence = fence->snapshot(fence->ticket());
            const auto stopped_paste = transfer->progress();
            check(stopped_fence.ever_held && stopped_fence.wait_finished &&
                  stopped_fence.state == tf_shell_test::SenderFence::State::aborted &&
                  stopped_fence.abort_reason == tf_shell_test::SenderFence::AbortReason::stop,
                  "stop wakes the held writer without a control GO");
            check(tf_shell::paste_terminal(stopped_paste.state) && !stopped_paste.in_flight &&
                  stopped_paste.state != tf_shell::PasteState::completed &&
                  stopped_paste.sent >= before_stop.sent && stopped_paste.sent <= before_stop.accepted &&
                  stopped_paste.accepted == before_stop.accepted &&
                  stopped_paste.retained_capacity <= 32,
                  "stopped pre-send chunk is released without a fabricated write acknowledgement");
            if (action == WriterAction::stop)
                check(stopped_paste.state == tf_shell::PasteState::failed && stopped_paste.sent == before_stop.sent,
                      "stop never submits its pre-send held chunk");
        }
        resize.request(40, 100); resize.start();
        {std::unique_lock<std::mutex> lock(gate_mutex);
         check(gate_changed.wait_for(lock,std::chrono::seconds(3),[&] {return resize_entered;}),"resize worker reaches explicit pre-API gate");}
        // The old probe tried to catch a transient OS call with a polling loop.
        // Delayed observation is now deterministic, even when the API is fast.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto working=resize.snapshot();check(working.working,"gated worker owns resize generation");
        resize.request(41, 101); resize.request(42, 102);
        const auto coalesced = resize.snapshot();
        check(coalesced.working && coalesced.pending && coalesced.working_generation == 1 &&
              coalesced.pending_generation == 3 && coalesced.requested_generation == 3 && !coalesced.completed_generation,
              "one worker-owned resize plus latest coalesced request before real API");
        release_gates();
        await([&] { output.take(65536); return resize.settled(); }, "real resize settles after output gate release");
        const auto complete = resize.snapshot();
        check(!complete.working && !complete.pending && complete.requested_generation == 3 &&
              complete.completed_generation == 3 && !complete.failed, "latest resize generation completes");
        resize.stop(); const auto joined_resize = resize.snapshot();
        check(joined_resize.started && joined_resize.joined && joined_resize.stopping && !joined_resize.working && !joined_resize.pending,
              "resize stop joins worker");
        if (!SetEvent(query.get())) tf_conpty::fail("SetEvent(queue size query)");
        std::string tail; bool size_seen = false, prefix_seen = false;
        await([&] {
            tail += output.take(65536);
            size_seen = size_seen || tail.find("TFQUEUE-SIZE:102x42:END") != std::string::npos;
            prefix_seen = prefix_seen || tail.find(fence_sender ? "TFQUEUE-PRIMED:16384:END" : "TFQUEUE-PRIMED:0:END") != std::string::npos;
            if (tail.size() > 128) tail.erase(0, tail.size() - 128);
            return size_seen && prefix_seen;
        }, "real child observes latest requested dimensions");
        check(session.wait_exit(3000) && session.exit_code == 0, "real queue child exits normally after size report");
        cleanup();
        check(session.cleanup_complete && !session.active_after && session.child_signaled && session.exit_code == 0,
              "real queue fixture Job cleanup");
        const char* action_name = action == WriterAction::stop ? "stop" :
                                  action == WriterAction::cancel ? "cancel" : "cancel-stop-race";
        const auto after = transfer->progress();
        std::cout << "{\"case\":\"real-workers\",\"action\":\"" << action_name << "\",\"input_queued\":" << held.queued_bytes
                  << ",\"input_in_flight\":" << held.in_flight_bytes << ",\"input_joined\":true,\"working_generation\":"
                  << coalesced.working_generation << ",\"pending_generation\":" << coalesced.pending_generation
                  << ",\"completed_generation\":" << complete.completed_generation
                  << ",\"child_columns\":102,\"child_rows\":42,\"input_submitted_before_stop\":" << before_stop.sent
                  << ",\"input_accepted_before_stop\":" << before_stop.accepted
                  << ",\"paste_in_flight_before_stop\":" << before_stop.in_flight
                  << ",\"total\":" << before_stop.total << ",\"child_prefix_read\":" << (fence_sender ? 16384 : 0)
                  << ",\"sender_fence_held\":" << (fence_sender ? "true" : "false")
                  << ",\"sent_after\":" << after.sent << ",\"accepted_after\":" << after.accepted
                  << ",\"in_flight_after\":" << after.in_flight << ",\"retained_capacity_after\":" << after.retained_capacity
                  << ",\"state_after\":" << static_cast<unsigned>(after.state)
                  << ",\"job_active_after\":0}\n";
    } catch (...) {
        const auto original = std::current_exception();
        try { cleanup(); } catch (const std::exception& error) { std::cerr << "fixture cleanup failed: " << error.what() << '\n'; }
        std::rethrow_exception(original);
    }
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring(argv[1]) == L"--queue-child") return child(argv[2], false);
        if (argc == 3 && std::wstring(argv[1]) == L"--queue-child-primed") return child(argv[2], true);
        bool fence_sender = true;
        if (argc == 2 && std::wstring(argv[1]) == L"--ungated-writer-observation") { fence_sender = false; argc = 1; }
        if (argc == 2 && std::wstring(argv[1]) == L"--supervised") {
            std::string command;
            check(static_cast<bool>(std::getline(std::cin, command)) && command == "GO", "owned supervisor gate");
            argc = 1;
        }
        check(argc == 1, "unknown queue probe arguments");
        bounded_queues(); lifecycle_edges(); real_workers(fence_sender, WriterAction::stop);
        if (fence_sender) {
            real_workers(true, WriterAction::cancel);
            real_workers(true, WriterAction::cancel_stop_race);
        }
        std::cout << "bounded queues, backpressure, ordering, in-flight resize and stop verified\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
