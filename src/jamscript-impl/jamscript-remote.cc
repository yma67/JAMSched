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

JAMScript::RemoteExecutionAgent::RemoteExecutionAgent() : executionIdGenerator(0) {}

void JAMScript::RemoteExecutionAgent::operator()(const std::function<bool()> &predicate) {
    while (predicate()) {
        std::unique_lock<std::recursive_mutex> lock(poolMutex);
        if (!toSendPool.empty()) {
            std::cout << toSendPool.front() << std::endl;
            uint64_t id = toSendPool.front()["exec_id"].get<uint64_t>();
            if (toSendPool.front()["condstr"].get<std::string>() == "this == fog" &&
                toSendPool.front()["condvec"].get<uint32_t>() == 1) {
                NotifyLocalTaskRemoteFinish(id, ACK_FINISHED,
                                            {{"returnValue", "command, condvec, and fmask!"}});
            } else {
                NotifyLocalTaskRemoteFinish(
                    id, ACK_FAILED, {{"return_exception", "wrong device, should be fog only"}});
            }
            toSendPool.pop_front();
        }
    }
}

bool JAMScript::RemoteExecutionAgent::NotifyLocalTaskRemoteFinish(
    uint64_t id, Ack status, const nlohmann::json &returnValue) {
    std::lock_guard<std::recursive_mutex> lock(poolMutex);
    if (toWaitPool.find(id) != toWaitPool.end()) {
        auto f = toWaitPool[id];
        if (returnValue.contains("returnValue") && status == ACK_FINISHED) {
            f->SetValue(returnValue);
        } else if (returnValue.contains("return_exception") && status != ACK_FINISHED) {
            f->SetException(std::make_exception_ptr(
                InvalidArgumentException(returnValue["return_exception"].get<std::string>())));
        } else {
            f->SetException(
                std::make_exception_ptr(InvalidArgumentException("format of response incorrect")));
        }
        toWaitPool.erase(id);
        return true;
    } else {
        throw InvalidArgumentException("not found");
    }
    return false;
}