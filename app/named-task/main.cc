#include <core/scheduler/task.h>
#include <unordered_map>
#include <functional>
#include <cassert>
#include <string>
#include <iostream>
#include <jamscript/scheduler/scheduler.hh>

#define show(_x) std::cout << #_x": " << _x << std::endl;

int CiteLabAdditionFunction(int a, char b, float c, short d, double e, long f, std::string validator) {
    show(a);
    show(b);
    show(c);
    show(d);
    show(e);
    show(f);
    std::cout << validator << std::endl;
    assert(validator == std::to_string(a) + std::to_string(b) + std::to_string(c) + std::to_string(d) + std::to_string(e) + std::to_string(f));
    return a + b + d + f;
}

int main() {
    JAMScript::Scheduler jamc_sched({ { 0, 30 * 1000, 0 } }, { { 0, 30 * 1000, 0 } }, 888, 1024 * 256, nullptr, [] (CTask* self, void* args) {
        std::cout << "aaaa" << std::endl;
        auto* schedulerPointer = static_cast<JAMScript::Scheduler*>(self->scheduler->GetSchedulerData(self->scheduler));
        auto res = schedulerPointer->CreateLocalNamedTaskAsync<int>(uint64_t(30 * 1000), uint64_t(500), "citelab", 1, 2, 0.5, 3, 1.25, 4, std::string("120.531.254"));
        WaitForValueFromFuture(res.get());
        assert(res->status == ACK_FINISHED);
        std::cout << *static_cast<int*>(res->data) << std::endl;
        assert(*static_cast<int*>(res->data) == 10);
        delete static_cast<int*>(res->data);
        schedulerPointer->Exit();
        FinishTask(self, 0);
    });
    jamc_sched.RegisterNamedExecution(
        "citelab", 
        reinterpret_cast<void*>(CiteLabAdditionFunction)
    );
    jamc_sched.Run();
    return 0;
}