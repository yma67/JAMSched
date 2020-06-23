#ifndef JAMSCRIPT_JAMSCRIPT_REMOTE_HH
#define JAMSCRIPT_JAMSCRIPT_REMOTE_HH
#include <remote/mqtt/mqtt_adapter.h>
#include <exception/exception.hpp>
#include <concurrency/future.hpp>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <cstdint>
#include <cstring>
#include <string>
#include <mutex>

namespace JAMScript
{

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