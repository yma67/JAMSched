#include <scheduler/scheduler.hpp>
#include <scheduler/tasklocal.hpp>

#include <catch2/catch.hpp>

struct A {
    static JAMScript::TaskLS<int> i;
    int b, c;
    A(int b_, int c_) : b(b_), c(c_) {}
};

JAMScript::TaskLS<int> A::i = CreateTaskLS(int, 8);

auto xglb = CreateTaskLS(int, 84);

void CheckTLS() {
    WARN(xglb);
    REQUIRE(xglb == 84);
    int r = rand() % 10000, org = A::i;
    A::i += r;
    REQUIRE(A::i == (org + r));
}

int CiteLabAdditionFunctionInteractive(int a, char b, float c, short d, double e, long f, std::string validator) {
    auto xa = CreateTaskLS(A, 3, 2);
    REQUIRE(A::i == 8);
    A::i += 10;
    REQUIRE(A::i == 18);
    int rr = rand() % 10;
    for (int x = 0; x < rr; x++) CheckTLS();
    REQUIRE(1 == a);
    REQUIRE(2 == b);
    REQUIRE(Approx(0.5) == c);
    REQUIRE(3 == d);
    REQUIRE(Approx(1.25) == e);
    REQUIRE(4 == f);
    REQUIRE(validator == "citelab loves java interactive");
    return a + b + d + f;
}

int CiteLabAdditionFunctionRealTime(int a, char b, float c, short d, double e, long f, std::string validator) {
    auto xa = CreateTaskLS(A, 4, 5);
    REQUIRE(A::i == 8);
    A::i += 13;
    REQUIRE(A::i == 21);
    int rr = rand() % 10;
    for (int x = 0; x < rr; x++) CheckTLS();
    REQUIRE(1 == a);
    REQUIRE(2 == b);
    REQUIRE(Approx(0.5) == c);
    REQUIRE(3 == d);
    REQUIRE(Approx(1.25) == e);
    REQUIRE(4 == f);
    REQUIRE(validator == "citelab loves java real time");
    return a + b + d + f;
}

int CiteLabAdditionFunctionBatch(int a, char b, float c, short d, double e, long f, std::string validator) {
    int &xa = CreateTaskLS(int, 5), &xb = CreateTaskLS(int, 6), &xc = CreateTaskLS(int, 7);
    REQUIRE(5 == xa);
    REQUIRE(6 == xb);
    REQUIRE(7 == xc);
    REQUIRE(1 == a);
    REQUIRE(2 == b);
    REQUIRE(Approx(0.5) == c);
    REQUIRE(3 == d);
    REQUIRE(Approx(1.25) == e);
    REQUIRE(4 == f);
    REQUIRE(validator == "citelab loves java batch");
    return a + b + d + f;
}

TEST_CASE("Task Local", "[tasklocal]") {
    JAMScript::RIBScheduler ribScheduler(1024 * 256);
    ribScheduler.SetSchedule({{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}},
                             {{std::chrono::milliseconds(0), std::chrono::milliseconds(100), 0}});
    ribScheduler.CreateBatchTask({false, 1024 * 256}, std::chrono::milliseconds(90), [&]() {
        ribScheduler
            .CreateBatchTask({false, 1024 * 256}, std::chrono::milliseconds(90), CiteLabAdditionFunctionInteractive, 1,
                             2, float(0.5), 3, double(1.25), 4, std::string("citelab loves java interactive"))
            ->Join();
        ribScheduler
            .CreateBatchTask({true, 0}, std::chrono::milliseconds(90), CiteLabAdditionFunctionRealTime, 1, 2,
                             float(0.5), 3, double(1.25), 4, std::string("citelab loves java real time"))
            ->Join();
        ribScheduler
            .CreateInteractiveTask(
                {true, 0}, std::chrono::milliseconds(9000), std::chrono::milliseconds(90), []() {},
                CiteLabAdditionFunctionBatch, 1, 2, float(0.5), 3, double(1.25), 4,
                std::string("citelab loves java batch"))
            ->Join();
        ribScheduler.ShutDown();
    });
    ribScheduler.RunSchedulerMainLoop();
}
