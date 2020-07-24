#ifndef JAMSCRIPT_JAMSCRIPT_REMOTE_HH
#define JAMSCRIPT_JAMSCRIPT_REMOTE_HH
#include <remote/mqtt/mqtt_adapter.h>
#include <remote/mqtt/nvoid.h>
#include <exception/exception.hpp>
#include <concurrency/future.hpp>
#include <nlohmann/json.hpp>
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
        auto *px = new char[st.size() + 1];
        px[st.size()] = 0;
        strncpy(px, st.c_str(), st.size());
        value = px;
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
        auto *px = new char[st.size() + 1];
        px[st.size()] = 0;
        strncpy(px, st.c_str(), st.size());
        value = px;
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
        bool ArgumentGC(R *gc, TArgs &&... tArgs)
        {
            ArgumentGC(std::forward<TArgs>(tArgs)...);
            delete gc;
            return true;
        }

        template <typename... TArgs>
        bool ArgumentGC(char *gc, TArgs &&... tArgs) {
            ArgumentGC(std::forward<TArgs>(tArgs)...);
            delete[] gc;
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
        bool ArgumentGC(const R *gc, TArgs &&... tArgs)
        {
            ArgumentGC(std::forward<TArgs>(tArgs)...);
            delete[] gc;
            return true;
        }

        template <typename... TArgs>
        bool ArgumentGC(const char *gc, TArgs &&... tArgs) {
            ArgumentGC(std::forward<TArgs>(tArgs)...);
            delete[] gc;
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

    } // namespace RExecDetails

    class RIBScheduler;
    class Remote
    {
    public:

        void CancelAllRExecRequests();
        static int RemoteArrivedCallback(void *ctx, char *topicname, int topiclen, MQTTAsync_message *msg);

        template <typename... Args>
        void CreateRExecAsync(const std::string &eName, const std::string &condstr, uint32_t condvec, Args &&... eArgs)
        {
            nlohmann::json rexRequest = {
                {"cmd", "REXEC-ASY"},
                {"opt", devId},
                {"actname", eName},
                {"args", nlohmann::json::array({std::forward<Args>(eArgs)...})},
                {"condstr", condstr},
                {"condvec", condvec}};
            std::unique_lock lk(mRexec);
            rexRequest.push_back({"actid", eIdFactory});
            auto tempEID = eIdFactory;
            eIdFactory++;
            auto& pr = ackLookup[tempEID] = std::make_unique<Promise<void>>();
            auto futureAck = pr->GetFuture();
            lk.unlock();
            auto vReq = nlohmann::json::to_cbor(rexRequest.dump());            
            CreateRetryTask(futureAck, vReq, tempEID);
        }

        template <typename T, typename... Args>
        T CreateRExecSync(const std::string &eName, const std::string &condstr, uint32_t condvec, Args &&... eArgs)
        {
            nlohmann::json rexRequest = {
                {"cmd", "REXEC-SYN"},
                {"opt", devId},
                {"actname", eName},
                {"args", nlohmann::json::array({std::forward<Args>(eArgs)...})},
                {"condstr", condstr},
                {"condvec", condvec}};
            std::unique_lock lk(mRexec);
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
                catch (const std::exception &e)
                {
                    if (retryNum < 3)
                    {
                        lk.lock();
                        ackLookup.erase(tempEID);
                        auto& tprAck = ackLookup[tempEID] = std::make_unique<Promise<void>>();
                        lk.unlock();
                        futureAck = tprAck->GetFuture();                        
                        retryNum++;
                        if (retryNum < 3)                         
                            continue;
                        lk.lock();
                        rLookup.erase(tempEID);
                        lk.unlock();
                        throw e;
                    }
                }
            }
            return fuExec.GetFor(std::chrono::seconds(1)).get<T>();
        }
        Remote(RIBScheduler *scheduler, const std::string &hostAddr,
               const std::string &appName, const std::string &devName);
        ~Remote();

    public:

        void CreateRetryTask(Future<void> &futureAck, std::vector<unsigned char> &vReq, uint32_t tempEID);

        RIBScheduler *scheduler;
        std::mutex mRexec;
        uint32_t eIdFactory;
        mqtt_adapter_t *mq;
        const std::string devId, appId;
        std::string replyUp, replyDown, requestUp, requestDown, announceDown;
        std::unordered_map<uint32_t, std::unique_ptr<Promise<nlohmann::json>>> rLookup;
        std::unordered_map<uint32_t, std::unique_ptr<Promise<void>>> ackLookup;
    };

} // namespace JAMScript
#endif