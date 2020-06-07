#include <core/scheduler/task.h>
#include <unordered_map>
#include <functional>
#include <cassert>
#include <string>
#include <iostream>
#include <jamscript-impl/jamscript-scheduler.hh>

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
    jamscript::c_side_scheduler jamc_sched({ { 0, 30 * 1000, 0 } }, { { 0, 30 * 1000, 0 } }, 888, 1024 * 256, nullptr, [] (task_t* self, void* args) {
        std::cout << "aaaa" << std::endl;
        auto* scheduler_ptr = static_cast<jamscript::c_side_scheduler*>(self->scheduler->get_scheduler_data(self->scheduler));
        auto res = scheduler_ptr->add_local_named_task_async<int>(uint64_t(30 * 1000), uint64_t(500), "citelab", 1, 2, 0.5, 3, 1.25, 4, std::string("120.531.254"));
        get_future(res.get());
        assert(res->status == ack_finished);
        std::cout << *static_cast<int*>(res->data) << std::endl;
        assert(*static_cast<int*>(res->data) == 10);
        delete static_cast<int*>(res->data);
        scheduler_ptr->exit();
        finish_task(self, 0);
    });
    jamc_sched.register_named_execution(
        "citelab", 
        reinterpret_cast<void*>(CiteLabAdditionFunction)
    );
    jamc_sched.run();
    return 0;
}