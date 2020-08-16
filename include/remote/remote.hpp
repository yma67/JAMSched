#ifndef JAMSCRIPT_JAMSCRIPT_REMOTE_HH
#define JAMSCRIPT_JAMSCRIPT_REMOTE_HH
#include <remote/mqtt/mqtt_adapter.h>
#include <remote/mqtt/nvoid.h>
#include <exception/exception.hpp>
#include <concurrency/future.hpp>
#include "remote/threadpool.hpp"
#include <nlohmann/json.hpp>
#include <boost/compute/detail/lru_cache.hpp>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cstring>
#include <string>
#include <memory>
#include <mutex>

// Serializer
// possible to define your own in program files
// but should in consistent with ArgumentGC specialization
// CString
template <>
struct nlohmann::adl_serializer<char *>
{
    static void to_json(json &j, const char *&value)
    {
        j = std::string(value);
    }
    static void from_json(const json &j, char *&value)
    {
        std::string st = j.template get<std::string>();
        value = strdup(st.c_str());
    }
};

template <>
struct nlohmann::adl_serializer<const char *>
{
    static void to_json(json &j, const char *&value)
    {
        j = std::string(value, value + strlen(value));
    }
    static void from_json(const json &j, const char *&value)
    {
        std::string st = j.template get<std::string>();
        value = strdup(st.c_str());
    }
};

// ByteArray
template <>
struct nlohmann::adl_serializer<nvoid_t *>
{
    static void to_json(json &j, const nvoid_t *&value)
    {
        std::vector<char> bArray(reinterpret_cast<char *>(value->data), 
                                 reinterpret_cast<char *>(value->data) + value->len);
        j = bArray;
    }
    static void from_json(const json &j, nvoid_t *&value)
    {
        auto bArray = j.template get<std::vector<char>>();
        value = nvoid_new(bArray.data(), bArray.size());
    }
};

template <>
struct nlohmann::adl_serializer<const nvoid_t *>
{
    static void to_json(json &j, const nvoid_t *&value)
    {
        std::vector<char> bArray(reinterpret_cast<char *>(value->data), 
                                 reinterpret_cast<char *>(value->data) + value->len);
        j = bArray;
    }
    static void from_json(const json &j, const nvoid_t *&value)
    {
        auto bArray = j.template get<std::vector<char>>();
        value = nvoid_new(bArray.data(), bArray.size());
    }
};

namespace JAMScript
{
    namespace RExecDetails
    {

        bool ArgumentGC();

        // Pointer Types Except for CString (char*/const char*) and ByteArray, with destructor
        template <typename R, typename... TArgs>
        bool ArgumentGC(R *gc, TArgs &&... tArgs);
        // CString (char*/const char*)
        template <typename... TArgs>
        bool ArgumentGC(char *gc, TArgs &&... tArgs);
        // ByteArray (Should be passed using nvoid_t)
        template <typename... TArgs>
        bool ArgumentGC(nvoid_t *gc, TArgs &&... tArgs);

        // const versions of the above
        template <typename R, typename... TArgs>
        bool ArgumentGC(const R *gc, TArgs &&... tArgs);
        template <typename... TArgs>
        bool ArgumentGC(const char *gc, TArgs &&... tArgs);
        template <typename... TArgs>
        bool ArgumentGC(const nvoid_t *gc, TArgs &&... tArgs);

        template <typename T, typename... TArgs>
        bool ArgumentGC(T&& gc, TArgs &&... tArgs)
        {
            ArgumentGC(std::forward<TArgs>(tArgs)...);
            return false;
        }

        template <typename R, typename... TArgs>
        [[deprecated("Please Explicitly Specialize For Your Data Type")]] 
        bool ArgumentGC(R *gc, TArgs &&... tArgs)
        {
            ArgumentGC(std::forward<TArgs>(tArgs)...);
            delete gc;
            return true;
        }

        template <typename... TArgs>
        bool ArgumentGC(char *gc, TArgs &&... tArgs) {
            ArgumentGC(std::forward<TArgs>(tArgs)...);
            free(gc);
            return true;
        }

        template <typename... TArgs>
        bool ArgumentGC(nvoid_t *gc, TArgs &&... tArgs)
        {
            ArgumentGC(std::forward<TArgs>(tArgs)...);
            nvoid_free(const_cast<nvoid_t *>(gc));
            return true;
        }

        template <typename R, typename... TArgs>
        [[deprecated("Please Explicitly Specialize For Your Data Type")]] 
        bool ArgumentGC(const R *gc, TArgs &&... tArgs)
        {
            ArgumentGC(std::forward<TArgs>(tArgs)...);
            delete gc;
            return true;
        }

        template <typename... TArgs>
        bool ArgumentGC(const char *gc, TArgs &&... tArgs) {
            ArgumentGC(std::forward<TArgs>(tArgs)...);
            free(const_cast<char *>(gc));
            return true;
        }

        template <typename... TArgs>
        bool ArgumentGC(const nvoid_t *gc, TArgs &&... tArgs)
        {
            ArgumentGC(std::forward<TArgs>(tArgs)...);
            nvoid_free(const_cast<nvoid_t *>(gc));
            return true;
        }

        struct RoutineInterface
        {
            virtual nlohmann::json Invoke(const nlohmann::json &vaList) const = 0;
            virtual ~RoutineInterface() {}
        };

        template <typename T>
        struct RoutineRemote;

        template <typename R, typename... Args>
        struct RoutineRemote<std::function<R(Args...)>> : public RoutineInterface
        {
            RoutineRemote(std::function<R(Args...)> f) : fn(std::move(f)) {}
            ~RoutineRemote() override {}
            nlohmann::json Invoke(const nlohmann::json &vaList) const override
            {
                try
                {
                    auto vaTuple = vaList.get<std::tuple<Args...>>();
                    nlohmann::json jxr = nlohmann::json::object({{"args", std::apply(fn, vaTuple)}});
                    std::apply([](auto &&... xarg) { ArgumentGC(xarg...); }, std::move(vaTuple));
                    return jxr;
                }
                catch (const std::exception &e)
                {
                    return nlohmann::json::object({{"args", {"exception", std::string(e.what())}}});
                }
            }
            std::function<R(Args...)> fn;
        };

        template <typename... Args>
        struct RoutineRemote<std::function<void(Args...)>> : public RoutineInterface
        {
            RoutineRemote(std::function<void(Args...)> f) : fn(std::move(f)) {}
            ~RoutineRemote() override {}
            nlohmann::json Invoke(const nlohmann::json &vaList) const override
            {
                try
                {
                    auto vaTuple = vaList.get<std::tuple<Args...>>();
                    std::apply(fn, vaTuple);
                    std::apply([](auto &&... xarg) { ArgumentGC(xarg...); }, std::move(vaTuple));
                    return nlohmann::json::object({{"args", {}}});
                }
                catch (const std::exception &e)
                {
                    return nlohmann::json::object({{"args", {"exception", std::string(e.what())}}});
                }
            }
            std::function<void(Args...)> fn;
        };

        template <typename... Args>
        struct RoutineRemote<std::function<char *(Args...)>> : public RoutineInterface
        {
            RoutineRemote(std::function<char *(Args...)> f) : fn(std::move(f)) {}
            ~RoutineRemote() override {}
            nlohmann::json Invoke(const nlohmann::json &vaList) const override
            {
                try
                {
                    auto vaTuple = vaList.get<std::tuple<Args...>>();
                    char * cString = std::apply(fn, vaTuple);
                    nlohmann::json jxr = nlohmann::json::object({{"args", std::string(cString)}});
                    free(cString);
                    std::apply([](auto &&... xarg) { ArgumentGC(xarg...); }, std::move(vaTuple));
                    return jxr;
                }
                catch (const std::exception &e)
                {
                    return nlohmann::json::object({{"args", {"exception", std::string(e.what())}}});
                }
            }
            std::function<char *(Args...)> fn;
        };

        template <typename... Args>
        struct RoutineRemote<std::function<const char *(Args...)>> : public RoutineInterface
        {
            RoutineRemote(std::function<const char *(Args...)> f) : fn(std::move(f)) {}
            ~RoutineRemote() override {}
            nlohmann::json Invoke(const nlohmann::json &vaList) const override
            {
                try
                {
                    auto vaTuple = vaList.get<std::tuple<Args...>>();
                    char* cString = std::apply(fn, vaTuple);
                    nlohmann::json jxr = nlohmann::json::object({{"args", std::string(cString)}});
                    free(const_cast<char*>(cString));
                    std::apply([](auto &&... xarg) { ArgumentGC(xarg...); }, std::move(vaTuple));
                    return jxr;
                }
                catch (const std::exception &e)
                {
                    return nlohmann::json::object({{"args", {"exception", std::string(e.what())}}});
                }
            }
            std::function<const char *(Args...)> fn;
        };

        template <typename... Args>
        struct RoutineRemote<std::function<nvoid_t *(Args...)>> : public RoutineInterface
        {
            RoutineRemote(std::function<nvoid_t *(Args...)> f) : fn(std::move(f)) {}
            ~RoutineRemote() override {}
            nlohmann::json Invoke(const nlohmann::json &vaList) const override
            {
                try
                {
                    auto vaTuple = vaList.get<std::tuple<Args...>>();
                    nvoid_t* nVoid = std::apply(fn, vaTuple);
                    std::vector<char> bArray(reinterpret_cast<char *>(nVoid->data), 
                                             reinterpret_cast<char *>(nVoid->data) + nVoid->len);
                    nlohmann::json jxr = nlohmann::json::object({{"args", bArray }});
                    nvoid_free(nVoid);
                    std::apply([](auto &&... xarg) { ArgumentGC(xarg...); }, std::move(vaTuple));
                    return jxr;
                }
                catch (const std::exception &e)
                {
                    return nlohmann::json::object({{"args", {"exception", std::string(e.what())}}});
                }
            }
            std::function<nvoid_t *(Args...)> fn;
        };

        template <typename... Args>
        struct RoutineRemote<std::function<const nvoid_t *(Args...)>> : public RoutineInterface
        {
            RoutineRemote(std::function<const nvoid_t *(Args...)> f) : fn(std::move(f)) {}
            ~RoutineRemote() override {}
            nlohmann::json Invoke(const nlohmann::json &vaList) const override
            {
                try
                {
                    auto vaTuple = vaList.get<std::tuple<Args...>>();
                    const nvoid_t* nVoid = std::apply(fn, vaTuple);
                    std::vector<char> bArray(reinterpret_cast<char *>(nVoid->data), 
                                             reinterpret_cast<char *>(nVoid->data) + nVoid->len);
                    nlohmann::json jxr = nlohmann::json::object({{"args", bArray }});
                    nvoid_free(const_cast<nvoid_t*>(nVoid));
                    std::apply([](auto &&... xarg) { ArgumentGC(xarg...); }, std::move(vaTuple));
                    return jxr;
                }
                catch (const std::exception &e)
                {
                    return nlohmann::json::object({{"args", {"exception", std::string(e.what())}}});
                }
            }
            std::function<const nvoid_t *(Args...)> fn;
        };

        class HeartbeatFailureException : public std::exception
        {
        friend class Remote;
        private:
            static const std::string message_;

        public:
            virtual const char *what() const throw() override { return message_.c_str(); }
        };

    } // namespace RExecDetails

    class RIBScheduler;
    class Time;
    class Remote;
    class CloudFogInfo
    {
    public:
        friend class Remote;
        std::string devId, appId, hostAddr, replyUp, replyDown, requestUp, requestDown, announceDown;
        bool isRegistered, isExpired;
        std::unordered_set<uint32_t> rExecPending;
        mqtt_adapter_t *mqttAdapter;
        Remote *remote;
        std::uint32_t pongCounter, cloudFogInfoCounter;
        TimePoint prevHearbeat;
        bool SendBuffer(const std::vector<uint8_t> &buffer);
        bool SendBuffer(const std::vector<char> &buffer);
        void Clear();
        CloudFogInfo(Remote *remote, std::string devId, std::string appId, std::string hostAddr);
        CloudFogInfo(CloudFogInfo const &) = delete;
        CloudFogInfo(CloudFogInfo &&) = default;
        CloudFogInfo &operator=(CloudFogInfo const &) = delete;
        CloudFogInfo &operator=(CloudFogInfo &&) = default;
    };
    class Remote
    {
    public:
        friend class BroadcastManager;
        friend class RIBScheduler;
        friend class CloudFogInfo;
        friend class LogManager;
        friend class Time;
        void CancelAllRExecRequests();

        bool CreateRExecAsyncWithCallbackNT(std::string hostName, const std::string &eName, 
                                            const std::string &condstr, uint32_t condvec, 
                                            std::function<void()> failureCallback, nlohmann::json rexRequest)
        {
            std::unique_lock lk(Remote::mCallback);
            if (cloudFogInfo.find(hostName) == cloudFogInfo.end() || !cloudFogInfo[hostName]->isRegistered)
            {
                failureCallback();
                return false;
            }
            rexRequest.push_back({"opt", cloudFogInfo[hostName]->devId});
            rexRequest.push_back({"actid", eIdFactory});
            printf("Pushing... actid %d\n", eIdFactory);
            auto tempEID = eIdFactory;
            eIdFactory++;
            auto& pr = ackLookup[tempEID] = std::make_unique<Promise<void>>();
            auto futureAck = pr->GetFuture();
            cloudFogInfo[hostName]->rExecPending.insert(tempEID);
            lk.unlock();
            auto vReq = nlohmann::json::to_cbor(rexRequest.dump());            
            return CreateRetryTask(hostName, futureAck, vReq, tempEID, std::move(failureCallback));
        }

        bool CreateRExecAsyncWithCallbackNT(const std::string &eName, const std::string &condstr, uint32_t condvec, 
                                            std::function<void()> failureCallback, nlohmann::json rexRequest)
        {
            std::unique_lock lk(Remote::mCallback);
            if (mainFogInfo == nullptr || !mainFogInfo->isRegistered)
            {
                failureCallback();
                return false;
            }
            rexRequest.push_back({"opt", mainFogInfo->devId});
            rexRequest.push_back({"actid", eIdFactory});
            printf("Pushing... actid %d\n", eIdFactory);
            auto tempEID = eIdFactory;
            eIdFactory++;
            auto& pr = ackLookup[tempEID] = std::make_unique<Promise<void>>();
            auto futureAck = pr->GetFuture();
            lk.unlock();
            auto vReq = nlohmann::json::to_cbor(rexRequest.dump());            
            return CreateRetryTask(futureAck, vReq, tempEID, std::move(failureCallback));
        }

        template <typename... Args>
        bool CreateRExecAsyncWithCallback(const std::string &eName, const std::string &condstr, uint32_t condvec, 
                                          std::function<void()> failureCallback, Args &&... eArgs)
        {
            nlohmann::json rexRequest = {
                {"cmd", "REXEC-ASY"},
                {"actname", eName},
                {"args", nlohmann::json::array({std::forward<Args>(eArgs)...})},
                {"cond", condstr},
                {"condvec", condvec},
                {"actarg", "-"}};
            std::unique_lock lk(Remote::mCallback);
            if (mainFogInfo == nullptr || !mainFogInfo->isRegistered)
            {
                failureCallback();
                return false;
            }
            rexRequest.push_back({"opt", mainFogInfo->devId});
            rexRequest.push_back({"actid", eIdFactory});
            printf("Pushing... actid %d\n", eIdFactory);
            auto tempEID = eIdFactory;
            eIdFactory++;
            auto& pr = ackLookup[tempEID] = std::make_unique<Promise<void>>();
            auto futureAck = pr->GetFuture();
            lk.unlock();
            auto vReq = nlohmann::json::to_cbor(rexRequest.dump());            
            return CreateRetryTask(futureAck, vReq, tempEID, std::move(failureCallback));
        }

        template <typename... Args>
        bool CreateRExecAsyncWithCallbackToEachConnection(const std::string &eName, const std::string &condstr, 
                                                          uint32_t condvec, std::function<void()> failureCallback, 
                                                          Args &&... eArgs)
        {
            nlohmann::json rexRequest = {
                {"cmd", "REXEC-ASY"},
                {"actname", eName},
                {"args", nlohmann::json::array({std::forward<Args>(eArgs)...})},
                {"cond", condstr},
                {"condvec", condvec},
                {"actarg", "-"}};
            std::unique_lock lockGetAllHostNames(Remote::mCallback);
            std::vector<std::string> hostsAvailable;
            for (auto& [hostName, conn]: cloudFogInfo)
            {
                hostsAvailable.push_back(hostName);
            }
            lockGetAllHostNames.unlock();
            for (auto& hostName: hostsAvailable)
            {
                CreateRExecAsyncWithCallbackNT(hostName, eName, condstr, condvec, failureCallback, rexRequest);
            }
            return CreateRExecAsyncWithCallbackNT(eName, condstr, condvec, 
                                                  std::move(failureCallback), std::move(rexRequest));
        }

        template <typename... Args>
        bool CreateRExecAsync(const std::string &eName, const std::string &condstr, uint32_t condvec, Args &&... eArgs)
        {
            return CreateRExecAsyncWithCallback(eName, condstr, condvec, []{}, std::forward<Args>(eArgs)...);
        }

        template <typename... Args>
        bool CreateRExecAsync(std::string hostName, const std::string &eName, 
                              const std::string &condstr, uint32_t condvec, Args &&... eArgs)
        {
            return CreateRExecAsyncWithCallback(hostName, eName, condstr, condvec, []{}, 
                                                std::forward<Args>(eArgs)...);
        }

        template <typename T, typename... Args>
        T CreateRExecSyncWithCallbackToEachConnection(const std::string &eName, const std::string &condstr, 
                                                      uint32_t condvec, std::function<void()> heartBeatFailCallback, 
                                                      Args &&... eArgs)
        {
            nlohmann::json rexRequest = {
                {"cmd", "REXEC-SYN"},
                {"actname", eName},
                {"args", nlohmann::json::array({std::forward<Args>(eArgs)...})},
                {"cond", condstr},
                {"condvec", condvec},
                {"actarg", "-"}};
            auto prCommon = std::make_shared<Promise<nlohmann::json>>();
            auto failureCountCommon = std::make_shared<std::atomic_size_t>(0U);
            auto fuCommon = prCommon->GetFuture();
            {
                std::lock_guard lock(Remote::mCallback);
                auto countCommon = cloudFogInfo.size();
                CreateRetryTaskSync(heartBeatFailCallback, rexRequest, prCommon, countCommon, 
                                    failureCountCommon);
                for (auto& [hostName, cfInfo]: cloudFogInfo)
                {
                    CreateRetryTaskSync(hostName, heartBeatFailCallback, rexRequest, prCommon, 
                                        countCommon, failureCountCommon);
                }
            }
            return fuCommon.Get().template get<T>();
        }

        template <typename T, typename... Args>
        T CreateRExecSyncWithCallback(const std::string &eName, const std::string &condstr, uint32_t condvec, 
                                      std::function<void()> heartBeatFailCallback, Args &&... eArgs)
        {
            nlohmann::json rexRequest = {
                {"cmd", "REXEC-SYN"},
                {"actname", eName},
                {"args", nlohmann::json::array({std::forward<Args>(eArgs)...})},
                {"cond", condstr},
                {"condvec", condvec},
                {"actarg", "-"}};
            std::unique_lock lk(Remote::mCallback);
            if (mainFogInfo == nullptr || !mainFogInfo->isRegistered)
            {
                heartBeatFailCallback();
                throw RExecDetails::HeartbeatFailureException();
            }
            rexRequest.push_back({"opt", mainFogInfo->devId});
            rexRequest.push_back({"actid", eIdFactory});
            auto vReq = nlohmann::json::to_cbor(rexRequest.dump());
            auto tempEID = eIdFactory;
            eIdFactory++;
            auto& prAck = ackLookup[tempEID] = std::make_unique<Promise<void>>();
            auto futureAck = prAck->GetFuture();
            auto& pr = rLookup[tempEID] = std::make_unique<Promise<nlohmann::json>>();
            auto fuExec = pr->GetFuture();
            mainFogInfo->rExecPending.insert(tempEID);
            lk.unlock();
            int retryNum = 0;
            while (retryNum < 3)
            {
                try 
                {
                    {
                        lk.lock();
                        if (mainFogInfo == nullptr || !mainFogInfo->isRegistered)
                        {
                            lk.unlock();
                            heartBeatFailCallback();
                            throw RExecDetails::HeartbeatFailureException();
                        }
                        auto* ptrMqttAdapter = mainFogInfo->mqttAdapter;
                        mqtt_publish(ptrMqttAdapter, const_cast<char *>(mainFogInfo->requestUp.c_str()), 
                                     nvoid_new(vReq.data(), vReq.size()));
                        lk.unlock();
                    }
                    futureAck.GetFor(std::chrono::milliseconds(100));
                    break;
                } 
                catch (const RExecDetails::HeartbeatFailureException &he)
                {
                    heartBeatFailCallback();
                    throw RExecDetails::HeartbeatFailureException();
                }
                catch (const InvalidArgumentException &e)
                {
                    if (retryNum < 3)
                    {
                        retryNum++;
                        continue;
                    }
                    lk.lock();
                    ackLookup.erase(tempEID);
                    rLookup.erase(tempEID);
                    lk.unlock();
                    throw InvalidArgumentException("timed out");
                }
            }
            try 
            {
                return fuExec.Get().template get<T>();
            }
            catch (const InvalidArgumentException& eae)
            {
                if (!strcmp(eae.what(), "Timeout, but value/error not ready. "))
                {
                    lk.lock();
                    rLookup.erase(tempEID);
                    lk.unlock();
                    throw InvalidArgumentException(eae.what());
                }
            }
            catch (const RExecDetails::HeartbeatFailureException& e)
            {
                heartBeatFailCallback();
                throw RExecDetails::HeartbeatFailureException();
            }
            throw InvalidArgumentException("false wakeup");
        }

        template <typename T, typename... Args>
        T CreateRExecSync(const std::string &eName, const std::string &condstr, 
                          uint32_t condvec, Args &&... eArgs)
        {
            return CreateRExecSyncWithCallback<T>(eName, condstr, condvec, []{}, std::forward<Args>(eArgs)...);
        }

        void CheckExpire();

        Remote(RIBScheduler *scheduler, std::string hostAddr, std::string appName, std::string devName);
        ~Remote();

    private:

        bool CreateRetryTaskSync(std::function<void()> heartBeatFailCallback, nlohmann::json rexRequest, 
                                 std::shared_ptr<Promise<nlohmann::json>> prCommon, std::size_t countCommon, 
                                 std::shared_ptr<std::atomic_size_t> failureCountCommon);
        bool CreateRetryTaskSync(std::string hostName, std::function<void()> heartBeatFailCallback, 
                                 nlohmann::json rexRequest, std::shared_ptr<Promise<nlohmann::json>> prCommon, 
                                 std::size_t countCommon, std::shared_ptr<std::atomic_size_t> failureCountCommon);
        bool CreateRetryTask(Future<void> &futureAck, std::vector<unsigned char> &vReq, uint32_t tempEID, 
                             std::function<void()> callback);
        bool CreateRetryTask(std::string hostName, Future<void> &futureAck, std::vector<unsigned char> &vReq, 
                             uint32_t tempEID, std::function<void()> callback);
        static int RemoteArrivedCallback(void *ctx, char *topicname, int topiclen, MQTTAsync_message *msg);
        static SpinMutex mCallback;
        static std::unordered_set<CloudFogInfo *> isValidConnection;
        static ThreadPool callbackThreadPool;
        std::mutex mLoopSleep;
        std::condition_variable cvLoopSleep;
        std::uint32_t eIdFactory;
        RIBScheduler *scheduler;
        std::unique_ptr<CloudFogInfo> mainFogInfo;
        std::string devId, appId, hostAddr;
        boost::compute::detail::lru_cache<uint32_t, nlohmann::json> cache;
        std::unordered_map<uint32_t, std::unique_ptr<Promise<void>>> ackLookup;
        std::unordered_map<std::string, std::unique_ptr<CloudFogInfo>> cloudFogInfo;
        std::unordered_map<uint32_t, std::unique_ptr<Promise<nlohmann::json>>> rLookup;

    };

} // namespace JAMScript
#endif