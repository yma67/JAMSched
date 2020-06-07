#ifndef JAMSCRIPT_JAMSCRIPT_REMOTE_H
#define JAMSCRIPT_JAMSCRIPT_REMOTE_H
#include <mutex>
#include <queue>
#include <atomic>
#include <memory>
#include <cstdint>
#include <iostream>
#include <functional>
#include <unordered_map>
#include <future/future.h>
#include <nlohmann/json.hpp>
#include <core/scheduler/task.h>
#include "jamscript-impl/jamscript-scheduler.hh"

namespace jamscript {

class invalid_argument_exception: public std::exception {
private:
    std::string message_;
public:
    explicit invalid_argument_exception(const std::string& message) : 
    message_(message) {};
    virtual const char* what() const throw() {
        return message_.c_str();
    }
};

class remote_handler {
public:
    void operator()(const std::function<bool()>& predicate);
    bool notify_remote(uint64_t id, ack_types status, 
                       const nlohmann::json& return_val);
    remote_handler();
private:
    std::recursive_mutex pool_mutex;
    std::uint64_t exec_id_generator;
    std::deque<nlohmann::json> to_send_pool;
    std::unordered_map<std::uint64_t, 
                       std::shared_ptr<jamfuture_t>> to_wait_pool;
public:
    template <typename... Args>
    std::shared_ptr<jamfuture_t> register_remote(const std::string& 
                                                 remote_function_name, 
                                                 Args&& ...args) {
        auto f = std::make_shared<jamfuture_t>();
        make_future(f.get(), this_task(), nullptr, 
                    interactive_task_handle_post_callback);
        nlohmann::json rexec_json = {
            { 
                "func_name", 
                remote_function_name 
            }, 
            { 
                "args", 
                nlohmann::json::array({ std::forward<Args>(args)... }) 
            }
        };
        {
            std::lock_guard<std::recursive_mutex> lock(pool_mutex);
            auto id = exec_id_generator++;
            rexec_json.push_back({ "exec_id", id });
            to_send_pool.push_back(rexec_json);
            to_wait_pool[id] = f;
        }
        return f;
    }
};

template <typename Tr>
const Tr extract_local_named_exec(const std::shared_ptr<jamfuture_t>& f) {
    get_future(f.get());
    if (f->status != ack_finished) {
        throw invalid_argument_exception("value not ready");
    }
    auto* pr = static_cast<Tr*>(f->data);
    const auto r = std::move(*pr);
    delete pr;
    return r;
}

template <typename Tr>
Tr extract_local_named_exec(const std::shared_ptr<future<Tr>>& f) {
    return f->get();
}

template <typename Tr>
Tr extract_remote_named_exec(const std::shared_ptr<jamfuture_t>& f) {
    get_future(f.get());
    if (f->status != ack_finished) {
        throw invalid_argument_exception("value not ready");
    }
    auto* pr = static_cast<nlohmann::json*>(f->data);
    auto r = std::move((*pr)["return_val"].get<Tr>());
    delete pr;
    return r;
}

}
#endif