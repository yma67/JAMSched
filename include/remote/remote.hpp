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

        bool GarbageCollect();
        template <typename R, typename... TArgs>
        bool GarbageCollect(R *gc, TArgs &&... tArgs);
        template <typename... TArgs>
        bool GarbageCollect(nvoid_t *gc, TArgs &&... tArgs);
        template <typename R, typename... TArgs>
        bool GarbageCollect(const R *gc, TArgs &&... tArgs);
        template <typename... TArgs>
        bool GarbageCollect(const nvoid_t *gc, TArgs &&... tArgs);

        template <typename T, typename... TArgs>
        bool GarbageCollect(T gc, TArgs &&... tArgs)
        {
            GarbageCollect(std::forward<TArgs>(tArgs)...);
            return false;
        }

        template <typename R, typename... TArgs>
        bool GarbageCollect(R *gc, TArgs &&... tArgs)
        {
            GarbageCollect(std::forward<TArgs>(tArgs)...);
            delete gc;
            return true;
        }

        template <typename... TArgs>
        bool GarbageCollect(nvoid_t *gc, TArgs &&... tArgs)
        {
            GarbageCollect(std::forward<TArgs>(tArgs)...);
            nvoid_free(const_cast<nvoid_t *>(gc));
            return true;
        }

        template <typename R, typename... TArgs>
        bool GarbageCollect(const R *gc, TArgs &&... tArgs)
        {
            GarbageCollect(std::forward<TArgs>(tArgs)...);
            delete[] gc;
            return true;
        }

        template <typename... TArgs>
        bool GarbageCollect(const nvoid_t *gc, TArgs &&... tArgs)
        {
            GarbageCollect(std::forward<TArgs>(tArgs)...);
            nvoid_free(const_cast<nvoid_t *>(gc));
            return true;
        }

        struct InvokerInterface
        {
            virtual nlohmann::json Invoke(nlohmann::json vaList)
            {
                return nlohmann::json{"result", {}};
            }
        };

        template <typename T>
        struct Invoker;

        template <typename R, typename... Args>
        struct Invoker<std::function<R(Args...)>> : public InvokerInterface
        {
            Invoker(std::function<R(Args...)> f) : fn(std::move(f)) {}
            nlohmann::json Invoke(nlohmann::json vaList) override
            {
                try
                {
                    auto vaTuple = vaList.get<std::tuple<Args...>>();
                    nlohmann::json jxr = {"result", std::apply(fn, vaTuple)};
                    std::apply([](auto &&... xarg) { GarbageCollect(xarg...); }, std::move(vaTuple));
                    return std::move(jxr);
                }
                catch (const std::exception &e)
                {
                    return nlohmann::json{"exception", std::string(e.what())};
                }
            }
            std::function<R(Args...)> fn;
        };

        template <typename... Args>
        struct Invoker<std::function<void(Args...)>> : public InvokerInterface
        {
            Invoker(std::function<void(Args...)> f) : fn(std::move(f)) {}
            nlohmann::json Invoke(nlohmann::json vaList) override
            {
                try
                {
                    auto vaTuple = vaList.get<std::tuple<Args...>>();
                    std::apply(fn, vaTuple);
                    std::apply([](auto &&... xarg) { GarbageCollect(xarg...); }, std::move(vaTuple));
                    return nlohmann::json{"result", {}};
                }
                catch (const std::exception &e)
                {
                    return nlohmann::json{"exception", std::string(e.what())};
                }
            }
            std::function<void(Args...)> fn;
        };

    } // namespace RExecDetails

    class RIBScheduler;
    class Remote
    {
    public:

        static int RemoteArrivedCallback(void *ctx, char *topicname, int topiclen, MQTTAsync_message *msg);

        template <typename... Args>
        Future<nlohmann::json> CreateRExec(const std::string &eName, const std::string &condstr, uint32_t condvec, Args &&... eArgs)
        {
            nlohmann::json rexRequest = {
                {"funcName", eName},
                {"args", nlohmann::json::array({std::forward<Args>(eArgs)...})},
                {"condstr", condstr},
                {"condvec", condvec}};
            std::unique_lock lk(mRexec);
            rexRequest.push_back({"execId", eIdFactory});
            auto pr = std::make_shared<Promise<nlohmann::json>>();
            rLookup[eIdFactory++] = pr;
            lk.unlock();
            auto vReq = nlohmann::json::to_cbor(rexRequest);
            for (int i = 0; i < 3; i++)
            {
                if (mqtt_publish(mq, const_cast<char *>("/rexec-request"), nvoid_new(vReq.data(), vReq.size())))
                {
                    break;
                }
                if (i == 2)
                {
                    throw InvalidArgumentException("Rexec Failed After 3 Retries\n");
                }
            }
            return pr->GetFuture();
        }

        Remote(RIBScheduler *scheduler, const std::string &hostAddr,
               const std::string &appName, const std::string &devName);
        ~Remote();

    public:

        RIBScheduler *scheduler;
        std::mutex mRexec;
        uint32_t eIdFactory;
        mqtt_adapter_t *mq;
        std::unordered_map<uint32_t, std::shared_ptr<Promise<nlohmann::json>>> rLookup;
        
    };

} // namespace JAMScript
#endif