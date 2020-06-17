#include <catch2/catch.hpp>
#include <scheduler/scheduler.hh>
#include <scheduler/tasklocal.hh>



TEST_CASE("Task Local", "[tasklocal]") {
    {
        BenchSchedXS bSched2(1024 * 128);
        bSched2();
        REQUIRE(coro_count > 30);
    }
}