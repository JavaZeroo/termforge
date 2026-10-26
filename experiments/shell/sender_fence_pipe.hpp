// Bounded test-only control pipe. Neither commands nor replies carry paste bytes.
#pragma once
#include "../conpty/session.hpp"
#include "sender_fence.hpp"
#include <array>
#include <optional>

namespace tf_shell_test {
class SenderFencePipe {
    std::shared_ptr<SenderFence> fence_;
    tf_conpty::Handle request_read_,request_write_,reply_read_,reply_write_;
    uint64_t sent_=0,dispatched_=0,received_=0;
    bool eof_=false;
    static constexpr uint64_t command_limit=8;
    static void check(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
    static void create(tf_conpty::Handle& reader,tf_conpty::Handle& writer,DWORD required) {
        HANDLE read=nullptr,write=nullptr;
        // On failure CreatePipe's output handles are unspecified. Adopt only
        // successful handles, then verify the actual inbound server buffer.
        check(CreatePipe(&read,&write,nullptr,4096)!=FALSE,"fence control pipe creation failed");
        reader.reset(read);writer.reset(write);
        DWORD flags=0,capacity=0;
        check(GetNamedPipeInfo(reader.get(),&flags,nullptr,&capacity,nullptr) &&
              (flags&PIPE_SERVER_END) && capacity>=required,"fence control pipe capacity cannot hold its command budget");
    }
    template<size_t N> static void write(HANDLE pipe,const std::array<uint64_t,N>& message) {
        DWORD size=0;
        check(WriteFile(pipe,message.data(),sizeof(message),&size,nullptr) && size==sizeof(message),"fence control write failed");
    }
    template<size_t N> static std::array<uint64_t,N> read(HANDLE pipe) {
        std::array<uint64_t,N> message{};DWORD size=0;
        check(ReadFile(pipe,message.data(),sizeof(message),&size,nullptr) && size==sizeof(message),"fence control frame truncated");
        return message;
    }
public:
    struct Ack { uint64_t generation;bool accepted; };
    explicit SenderFencePipe(std::shared_ptr<SenderFence> fence):fence_(std::move(fence)) {
        check(fence_!=nullptr,"fence control requires a transfer binding");
        create(request_read_,request_write_,static_cast<DWORD>(command_limit*2*sizeof(uint64_t)));
        create(reply_read_,reply_write_,static_cast<DWORD>(command_limit*3*sizeof(uint64_t)));
    }
    ~SenderFencePipe() {
        const auto state=fence_->snapshot(fence_->ticket()).state;
        if(state==SenderFence::State::armed || state==SenderFence::State::held)
            fence_->abort(fence_->ticket(),SenderFence::AbortReason::stop);
    }
    void send_go(uint64_t generation) {
        check(request_write_.get() && sent_<command_limit,"fence control command budget or connection ended");
        write(request_write_.get(),std::array<uint64_t,2>{++sent_,generation});
    }
    void disconnect() { request_write_.reset(); }
    bool eof_observed() const { return eof_; }
    // The owned UI/test message pump dispatches only complete, already buffered
    // frames. Eight commands and replies fit in each pipe without blocking.
    void dispatch() {
        if(eof_) return;
        DWORD available=0;
        if(!PeekNamedPipe(request_read_.get(),nullptr,0,nullptr,&available,nullptr)) {
            check(GetLastError()==ERROR_BROKEN_PIPE,"fence control peek failed");
            char byte=0;DWORD count=0;
            check(!ReadFile(request_read_.get(),&byte,1,&count,nullptr) && GetLastError()==ERROR_BROKEN_PIPE && !count,
                  "fence control EOF was not confirmed by ReadFile");
            eof_=true;fence_->abort(fence_->ticket(),SenderFence::AbortReason::control_eof);return;
        }
        if(!available) return;
        check(available>=2*sizeof(uint64_t),"fence control partial request");
        const auto request=read<2>(request_read_.get());
        check(request[0]==++dispatched_ && dispatched_<=command_limit,"fence control request sequence invalid");
        auto ticket=fence_->ticket();ticket.generation=request[1];
        const bool accepted=fence_->release(ticket);
        write(reply_write_.get(),std::array<uint64_t,3>{request[0],request[1],accepted?1ull:0ull});
    }
    std::optional<Ack> acknowledgement() {
        DWORD available=0;
        check(PeekNamedPipe(reply_read_.get(),nullptr,0,nullptr,&available,nullptr)!=FALSE,"fence control reply peek failed");
        if(!available) return std::nullopt;
        check(available>=3*sizeof(uint64_t),"fence control partial reply");
        const auto reply=read<3>(reply_read_.get());
        check(reply[0]==++received_ && received_<=sent_ && reply[2]<=1,"fence control reply sequence invalid");
        return Ack{reply[1],reply[2]!=0};
    }
};
} // namespace tf_shell_test
