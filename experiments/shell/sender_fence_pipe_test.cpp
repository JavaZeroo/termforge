// The control protocol uses real pipe reads/EOF; payload writes are unit-level here.
#include "sender_fence_pipe.hpp"
#include <future>
#include <iostream>

using tf_shell_test::SenderFence;
using tf_shell_test::SenderFencePipe;
static void check(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
struct HeldTransfer {
    std::shared_ptr<tf_shell::PasteTransfer> transfer=std::make_shared<tf_shell::PasteTransfer>(
        1,tf_shell::prepare_paste(std::u16string(1000000,u'汉')),true);
    std::shared_ptr<SenderFence> fence;
    SenderFencePipe pipe;
    std::future<bool> writer;
    explicit HeldTransfer(std::chrono::milliseconds lifetime=std::chrono::seconds(2))
        :fence(std::make_shared<SenderFence>(transfer,7,SenderFence::Clock::now()+lifetime)),pipe(fence) {
        check(fence->attach(transfer) && transfer->claim_writer(),"admit test writer");
    }
    void hold() {
        while(transfer->progress().sent<16384) {
            auto chunk=transfer->take_chunk();transfer->acknowledge_written(chunk.size());transfer->finish_chunk();
        }
        const auto chunk=transfer->take_chunk();
        writer=std::async(std::launch::async,[this,count=chunk.size()] {
            try {fence->before_send(transfer,count);return true;} catch(const std::runtime_error&) {return false;}
        });
        const auto limit=SenderFence::Clock::now()+std::chrono::seconds(1);
        while(fence->snapshot(fence->ticket()).state==SenderFence::State::armed && SenderFence::Clock::now()<limit)
            Sleep(1);
        check(fence->snapshot(fence->ticket()).state==SenderFence::State::held,"writer reached real CV hold");
    }
    bool go(uint64_t generation) {
        pipe.send_go(generation);pipe.dispatch();const auto ack=pipe.acknowledgement();
        check(ack.has_value() && ack->generation==generation,"pipe GO reply identity");return ack->accepted;
    }
};
int main() {
    try {
        {
            HeldTransfer value;
            check(!value.go(7),"GO before held must fail");value.hold();
            check(!value.go(8),"wrong generation must fail");
            check(value.go(7),"matching held GO must succeed");
            check(!value.go(7),"duplicate GO must fail");check(value.writer.get(),"GO wakes writer");
        }
        {
            HeldTransfer value;value.hold();value.pipe.disconnect();value.pipe.dispatch();
            check(value.pipe.eof_observed(),"actual pipe EOF observed");
            check(!value.writer.get(),"EOF fails held writer");
            const auto state=value.fence->snapshot(value.fence->ticket());
            check(state.state==SenderFence::State::aborted && state.abort_reason==SenderFence::AbortReason::control_eof,
                  "EOF reason retained");
        }
        {
            HeldTransfer value(std::chrono::milliseconds(300));value.hold();
            check(!value.writer.get(),"missing GO fails held writer");
            check(value.fence->snapshot(value.fence->ticket()).state==SenderFence::State::timed_out,"absolute deadline retained");
        }
        std::cout<<"real pipe GO/ack, generation, early/repeated GO, EOF and missing GO passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 2;}
}
