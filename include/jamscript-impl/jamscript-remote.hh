#ifndef JAMSCRIPT_JAMSCRIPT_REMOTE_H
#define JAMSCRIPT_JAMSCRIPT_REMOTE_H
#include <core/scheduler/task.h>
#include <future/future.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <jamscript-impl/jamscript-future.hh>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <queue>
#include <unordered_map>

namespace JAMScript {

    class InvalidArgumentException : public std::exception {
    private:
        std::string message_;

    public:
        explicit InvalidArgumentException(const std::string& message) : message_(message){};
        virtual const char* what() const throw() { return message_.c_str(); }
    };

    class RemoteExecutionAgent {
    public:
        void operator()(const std::function<bool()>& predicate);
        bool NotifyLocalTaskRemoteFinish(uint64_t id, Ack status,
                                         const nlohmann::json& returnValue);
        RemoteExecutionAgent();

    private:
        std::recursive_mutex poolMutex;
        std::uint64_t executionIdGenerator;
        std::deque<nlohmann::json> toSendPool;
        std::unordered_map<std::uint64_t, std::shared_ptr<Future<nlohmann::json>>> toWaitPool;

    public:
        template <typename... Args>
        std::shared_ptr<Future<nlohmann::json>> RegisterRemoteExecution(
            const std::string& remoteFunctionName, const std::string& condstr, uint32_t condvec,
            Args&&... args) {
            auto f = std::make_shared<Future<nlohmann::json>>();
            nlohmann::json rexecJSON = {
                {"func_name", remoteFunctionName},
                {"args", nlohmann::json::array({std::forward<Args>(args)...})},
                {"condstr", condstr},
                {"condvec", condvec}};
            {
                std::lock_guard<std::recursive_mutex> lock(poolMutex);
                auto id = executionIdGenerator++;
                rexecJSON.push_back({"exec_id", id});
                toSendPool.push_back(rexecJSON);
                toWaitPool.insert({id, f});
            }
            return f;
        }
    };

    template <typename Tr>
    const Tr ExtractValueFromLocalNamedExecution(const std::shared_ptr<CFuture>& f) {
        WaitForValueFromFuture(f.get());
        if (f->status != ACK_FINISHED) {
            throw InvalidArgumentException("value not SetTaskReady");
        }
        auto* pr = static_cast<Tr*>(f->data);
        const auto r = std::move(*pr);
        delete pr;
        return r;
    }

    template <typename Tr>
    Tr ExtractValueFromLocalNamedExecution(const std::shared_ptr<Future<Tr>>& f) {
        return f->Get();
    }

    template <typename Tr>
    Tr ExtractValueFromRemoteNamedExecution(const std::shared_ptr<Future<nlohmann::json>>& f) {
        return f->Get()["returnValue"].get<Tr>();
    }

}  // namespace JAMScript
#endif