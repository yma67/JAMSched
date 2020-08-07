#ifndef JAMSCRIPT_JAMSCRIPT_REMOTE_HH
#define JAMSCRIPT_JAMSCRIPT_REMOTE_HH
#include <remote/mqtt/mqtt_adapter.h>
#include <remote/mqtt/nvoid.h>
#include <exception/exception.hpp>
#include <concurrency/future.hpp>
#include <nlohmann/json.hpp>
#include <boost/compute/detail/lru_cache.hpp>
#include <unordered_map>
#include <cstdint>
#include <cstring>
#include <string>
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
        std::vector<char> bArray(reinterpret_cast<char *>(value->data), reinterpret_cast<char *>(value->data) + value->len);
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
        std::vector<char> bArray(reinterpret_cast<char *>(value->data), reinterpret_cast<char *>(value->data) + value->len);
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
                    std::vector<char> bArray(reinterpret_cast<char *>(nVoid->data), reinterpret_cast<char *>(nVoid->data) + nVoid->len);
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
                    std::vector<char> bArray(reinterpret_cast<char *>(nVoid->data), reinterpret_cast<char *>(nVoid->data) + nVoid->len);
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
        private:
            std::string message_;

        public:
            explicit HeartbeatFailureException() : message_("Cancelled due to bad remote connection") {};
            virtual const char *what() const throw() { return message_.c_str(); }
        };

    } // namespace RExecDetails

    class RIBScheduler;
    class Remote
    {
    public:
        friend class RIBScheduler;
        friend class LogManager;
        friend class BroadcastManager;
        void CancelAllRExecRequests();
        static int RemoteArrivedCallback(void *ctx, char *topicname, int topiclen, MQTTAsync_message *msg);

        template <typename... Args>
        bool CreateRExecAsyncWithCallback(const std::string &eName, const std::string &condstr, uint32_t condvec, 
                                          std::function<void()> failureCallback, Args &&... eArgs)
        {
            nlohmann::json rexRequest = {
                {"cmd", "REXEC-ASY"},
                {"opt", devId},
                {"actname", eName},
                {"args", nlohmann::json::array({std::forward<Args>(eArgs)...})},
                {"cond", condstr},
                {"condvec", condvec},
                {"actarg", "-"}};
            std::unique_lock lk(mRexec);
            if (!isRegistered)
            {
                failureCallback();
                return false;
            }
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
        bool CreateRExecAsync(const std::string &eName, const std::string &condstr, uint32_t condvec, Args &&... eArgs)
        {
            return CreateRExecAsyncWithCallback(eName, condstr, condvec, []{}, std::forward<Args>(eArgs)...);
        }

        template <typename T, typename... Args>
        T CreateRExecSyncWithCallback(const std::string &eName, const std::string &condstr, uint32_t condvec, 
                                      std::function<void()> heartBeatFailCallback, Args &&... eArgs)
        {
            nlohmann::json rexRequest = {
                {"cmd", "REXEC-SYN"},
                {"opt", devId},
                {"actname", eName},
                {"args", nlohmann::json::array({std::forward<Args>(eArgs)...})},
                {"cond", condstr},
                {"condvec", condvec},
                {"actarg", "-"}};
            std::unique_lock lk(mRexec);
            if (!isRegistered)
            {
                heartBeatFailCallback();
                throw RExecDetails::HeartbeatFailureException();
            }
            rexRequest.push_back({"actid", eIdFactory});
            auto vReq = nlohmann::json::to_cbor(rexRequest.dump());
            auto tempEID = eIdFactory;
            eIdFactory++;
            auto& prAck = ackLookup[tempEID] = std::make_unique<Promise<void>>();
            auto futureAck = prAck->GetFuture();
            auto& pr = rLookup[tempEID] = std::make_unique<Promise<nlohmann::json>>();
            auto fuExec = pr->GetFuture();
            lk.unlock();
            int retryNum = 0;
            while (retryNum < 3)
            {
                mqtt_publish(mq, const_cast<char *>(requestUp.c_str()), nvoid_new(vReq.data(), vReq.size()));
                try 
                {
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
                    rLookup.erase(tempEID);
                    lk.unlock();
                    throw InvalidArgumentException("timed out");
                }
            }
            try 
            {
                fuExec.Wait();
            } 
            catch (const RExecDetails::HeartbeatFailureException& e)
            {
                heartBeatFailCallback();
                throw RExecDetails::HeartbeatFailureException();
            }
            return fuExec.Get().template get<T>();
        }

        template <typename T, typename... Args>
        T CreateRExecSync(const std::string &eName, const std::string &condstr, uint32_t condvec, Args &&... eArgs)
        {
            return CreateRExecSyncWithCallback<T>(eName, condstr, condvec, [] { ThisTask::Exit(); }, std::forward<Args>(eArgs)...);
        }

        Remote(RIBScheduler *scheduler, std::string hostAddr, std::string appName, std::string devName);
        ~Remote();

    private:
        
        struct CloudFogInfo
        {
            std::string devIdAlkd;
            std::string appIdAlkd;
            std::string hostAddrAlkd;
            CloudFogInfo(std::string devIdAlkd, std::string appIdAlkd, std::string hostAddrAlkd)
                : devIdAlkd(std::move(devIdAlkd)), appIdAlkd(std::move(appIdAlkd)), hostAddrAlkd(std::move(hostAddrAlkd))
            {}
        };

        bool CreateRetryTask(Future<void> &futureAck, std::vector<unsigned char> &vReq, uint32_t tempEID, std::function<void()> callback);
        std::mutex mRexec;
        mqtt_adapter_t *mq;
        RIBScheduler *scheduler;
        std::atomic<bool> isRegistered;
        const std::string devId, appId, hostAddr;
        std::unique_ptr<CloudFogInfo> cloudFogInfo;
        uint32_t eIdFactory, cloudFogInfoCounter, pongCounter;
        boost::compute::detail::lru_cache<uint32_t, nlohmann::json> cache;
        std::string replyUp, replyDown, requestUp, requestDown, announceDown;
        std::unordered_map<uint32_t, std::unique_ptr<Promise<void>>> ackLookup;
        std::unordered_map<uint32_t, std::unique_ptr<Promise<nlohmann::json>>> rLookup;
    };

} // namespace JAMScript
#endif