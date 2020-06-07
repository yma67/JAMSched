/// Copyright 2020 Yuxiang Ma, Muthucumaru Maheswaran 
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
#include "jamscript-impl/jamscript-remote.hh"

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
        if (return_val.contains("return_val") && status == ack_finished) {
            f->set_value(return_val);
        } else if (return_val.contains("return_val") && 
                   status != ack_finished) {
            f->set_exception(
                std::make_exception_ptr(
                    invalid_argument_exception("cancelled or failed")
                )
            );
        } else {
            f->set_exception(
                std::make_exception_ptr(
                    invalid_argument_exception("format of response incorrect")
                )
            );
        }
        to_wait_pool.erase(id);
        return true;
    } else {
        throw invalid_argument_exception("not found");
    }
    return false;
}