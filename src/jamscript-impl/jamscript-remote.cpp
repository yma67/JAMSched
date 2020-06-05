#include "jamscript-impl/jamscript-remote.h"

jamscript::remote_handler::remote_handler() : exec_id_generator(0) {

}

void jamscript::remote_handler::
operator()(const std::function<bool()>& predicate) {
    while (predicate()) {
        std::unique_lock<std::recursive_mutex> lock(pool_mutex);
        if (!to_send_pool.empty()) {
            std::cout << to_send_pool.front() << std::endl;
            uint64_t id = to_send_pool.front()["exec_id"].get<uint64_t>();
            notify_remote(
                id, ack_finished, { { 
                        "return_val", 
                        "command, condvec, and fmask!" 
                    } 
            });
            to_send_pool.pop_front();
        }
    }
}

bool jamscript::remote_handler::
notify_remote(uint64_t id, ack_types status, 
              const nlohmann::json& return_val) {
    std::lock_guard<std::recursive_mutex> lock(pool_mutex);
    if (to_wait_pool.find(id) != to_wait_pool.end()) {
        auto f = to_wait_pool[id];
        f->data = new nlohmann::json(return_val);
        f->status = status;
        notify_future(f.get());
        to_wait_pool.erase(id);
        return true;
    } else {
        throw jamscript::invalid_argument_exception("not found");
    }
    return false;
}