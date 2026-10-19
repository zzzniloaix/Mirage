#include <catch2/catch_test_macros.hpp>

#include "Queue.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;
using Result = Queue<int*>::PushResult;

// Demuxer::read_loop waits for room this way, so that a seek or track switch
// requested meanwhile (paused: nothing drains the queues) is served first.
TEST_CASE("Queue::push_or_give_up: waits for room, gives up when asked, stops on shutdown", "[queue]")
{
    int a = 1, b = 2, c = 3;
    Queue<int*> q{ 1 };
    std::atomic<bool> give_up{ false };
    auto pred = [&] { return give_up.load(); };

    CHECK(q.push_or_give_up(&a, pred, 5ms) == Result::Pushed);   // room: no wait
    SECTION("room made while waiting: pushed") {
        std::thread popper([&] { std::this_thread::sleep_for(30ms); int* p = nullptr; q.pop(p); });
        CHECK(q.push_or_give_up(&b, pred, 5ms) == Result::Pushed);
        popper.join();
        int* p = nullptr;
        REQUIRE(q.try_pop(p));
        CHECK(p == &b);
    }
    SECTION("a request while full: given up, the item not taken") {
        std::thread asker([&] { std::this_thread::sleep_for(30ms); give_up = true; });
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(q.push_or_give_up(&b, pred, 5ms) == Result::GaveUp);
        CHECK(std::chrono::steady_clock::now() - t0 >= 25ms);
        asker.join();
        CHECK(q.size() == 1);
        // Asked already: a full queue gives up at once, one with room still takes it.
        CHECK(q.push_or_give_up(&c, pred, 5ms) == Result::GaveUp);
        int* p = nullptr;
        REQUIRE(q.try_pop(p));
        CHECK(p == &a);
        CHECK(q.push_or_give_up(&c, pred, 5ms) == Result::Pushed);
    }
    SECTION("shutdown while waiting: closed") {
        std::thread closer([&] { std::this_thread::sleep_for(30ms); q.shutdown(); });
        CHECK(q.push_or_give_up(&b, pred, 5ms) == Result::Closed);
        closer.join();
    }
}
