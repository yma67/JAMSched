#include "remote/remote.hpp"
#include "scheduler/scheduler.hpp"
#include <nlohmann/json.hpp>
#include <MQTTAsync.h>
#include <MQTTClient.h>

static void connected(void *a)
{
    // a is a pointer to a mqtt_adapter_t structure.
    printf("Connected to ... mqtt... \n");
}

JAMScript::Remote::Remote(RIBScheduler *scheduler, const std::string &hostAddr,
                          const std::string &appName, const std::string &devName)
    : scheduler(scheduler), devId(devName), appId(appName), eIdFactory(0U),
      requestUp(std::string("/") + appName + "/requests/up"),
      requestDown(std::string("/") + appName + "/requests/down"), 
      replyUp(std::string("/") + appName + "/replies/up"), 
      replyDown(std::string("/") + appName + "/replies/down"),
      announceDown(std::string("/") + appName + "/announce/down"),
      mq(mqtt_createserver(const_cast<char *>(hostAddr.c_str()), 1,
                           const_cast<char *>(appName.c_str()),
                           const_cast<char *>(devName.c_str()), connected))
{
    MQTTAsync_setMessageArrivedCallback(mq->mqttserv, scheduler, RemoteArrivedCallback);
    mqtt_set_subscription(mq, const_cast<char *>("/requests/up"));
    mqtt_set_subscription(mq, const_cast<char *>("/requests/down"));
    mqtt_set_subscription(mq, const_cast<char *>("/replies/up"));
    mqtt_set_subscription(mq, const_cast<char *>("/replies/down"));
    mqtt_set_subscription(mq, const_cast<char *>("/announce/down"));
    mqtt_connect(mq);
}

JAMScript::Remote::~Remote() 
{ 
    mqtt_deleteserver(mq); 
}

void JAMScript::Remote::CancelAllRExecRequests()
{
    std::lock_guard lk(mRexec);
    if (!rLookup.empty())
    {
        rLookup.clear();
    }
}

#define RegisterTopic(topicName, commandName, ...) {                                                                   \
    if (std::string(topicname) == topicName && rMsg.contains("cmd") && rMsg["cmd"].is_string())                        \
    {                                                                                                                  \
        std::string cmd = rMsg["cmd"].get<std::string>();                                                              \
        if (cmd == commandName)                                                                                        \
        {                                                                                                              \
            __VA_ARGS__                                                                                                \
            goto END_REMOTE;                                                                                           \
        }                                                                                                              \
    }                                                                                                                  \
}

int JAMScript::Remote::RemoteArrivedCallback(void *ctx, char *topicname, int topiclen, MQTTAsync_message *msg)
{
    auto *scheduler = static_cast<RIBScheduler *>(ctx);
    try {
        std::vector<char> cbor_((char *)msg->payload, (char *)msg->payload + msg->payloadlen);
        nlohmann::json rMsg = nlohmann::json::parse(nlohmann::json::from_cbor(cbor_).get<std::string>());
        RegisterTopic(scheduler->remote->requestDown, "PING", {

        });
        RegisterTopic(scheduler->remote->replyDown, "REGISTER-ACK", {

        });
        RegisterTopic(scheduler->remote->announceDown, "PUT-CF-INFO", {

        });
        RegisterTopic(scheduler->remote->announceDown, "KILL", {
            scheduler->ShutDown();
        });
        RegisterTopic(scheduler->remote->requestDown, "REXEC-ASY", {
            if (rMsg.contains("actid")) 
            {
                auto actId = rMsg["actid"].get<std::string>();
                if (scheduler->CreateRPBatchCall(std::move(rMsg))) {
                    nlohmann::json jack = 
                    {
                        {"actid", actId},
                        {"cmd", "REXEC-ACK"}
                    };
                    auto vReq = nlohmann::json::to_cbor(jack.dump());
                    mqtt_publish(scheduler->remote->mq, const_cast<char *>("/replies/up"), nvoid_new(vReq.data(), vReq.size()));
                }
            }
        });
        RegisterTopic(scheduler->remote->requestDown, "REXEC-SYN", {

        });
        RegisterTopic(scheduler->remote->replyDown, "REXEC-RES", {
            if (rMsg.contains("actid") && rMsg.contains("args")) 
            {
                auto actId = rMsg["actid"].get<uint32_t>();
                if (scheduler->remote->rLookup.find(actId) != scheduler->remote->rLookup.end()) 
                {
                    scheduler->remote->rLookup[actId]->SetValue(rMsg["args"]);
                    scheduler->remote->rLookup.erase(actId);
                }
            }
        });
    } catch (const std::exception& e) {
        e.what();
    }
END_REMOTE:
    mqtt_free_topic_msg(topicname, &msg);
    return 1;
}

bool JAMScript::RExecDetails::ArgumentGC()
{
    return false;
}