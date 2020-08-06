#include "remote/remote.hpp"
#include "scheduler/scheduler.hpp"
#include <nlohmann/json.hpp>
#include <MQTTAsync.h>
#include <MQTTClient.h>

#ifndef CLOUD_FOG_COUNT_STEP
#define CLOUD_FOG_COUNT_STEP 10
#endif

#ifndef PONG_COUNTER_MAX
#define PONG_COUNTER_MAX 10
#endif

static void connected(void *a)
{
    // a is a pointer to a mqtt_adapter_t structure.
    printf("Connected to ... mqtt... \n");
}

JAMScript::Remote::Remote(RIBScheduler *scheduler, const std::string &hostAddr,
                          const std::string &appName, const std::string &devName)
    : scheduler(scheduler), devId(devName), appId(appName), eIdFactory(0U), isRegistered(false), 
      requestUp(std::string("/") + appName + "/requests/up"), cloudFogInfoCounter(0U), 
      requestDown(std::string("/") + appName + "/requests/down/c"), cloudFogInfo(nullptr),
      replyUp(std::string("/") + appName + "/replies/up"), cache(1024), pongCounter(0U),
      replyDown(std::string("/") + appName + "/replies/down"),
      announceDown(std::string("/") + appName + "/announce/down"),
      mq(mqtt_createserver(const_cast<char *>(hostAddr.c_str()), 1,
                           const_cast<char *>(devName.c_str()), connected))
{
    MQTTAsync_setMessageArrivedCallback(mq->mqttserv, scheduler, RemoteArrivedCallback);
    mqtt_set_subscription(mq, const_cast<char *>(requestUp.c_str()));
    mqtt_set_subscription(mq, const_cast<char *>(requestDown.c_str()));
    mqtt_set_subscription(mq, const_cast<char *>(replyUp.c_str()));
    mqtt_set_subscription(mq, const_cast<char *>(replyDown.c_str()));
    mqtt_set_subscription(mq, const_cast<char *>(announceDown.c_str()));
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

void JAMScript::Remote::CreateRetryTask(Future<void> &futureAck, std::vector<unsigned char> &vReq, uint32_t tempEID)
{
    auto prAck = std::make_unique<Promise<void>>();
    auto fAck = prAck->GetFuture();
    scheduler->CreateBatchTask({true, 0, true}, Duration::max(), 
                               [this, prAck { std::move(prAck) }, vReq { std::move(vReq) }, 
                                futureAck { std::move(futureAck) }, tempEID]() mutable {
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
                    std::unique_lock lk(mRexec);
                    ackLookup.erase(tempEID);
                    auto& tprAck = ackLookup[tempEID] = std::make_unique<Promise<void>>();
                    lk.unlock();
                    futureAck = std::move(tprAck->GetFuture());                        
                    retryNum++;   
                    if (retryNum < 3)
                    {
                        continue;
                    }     
                    prAck->SetException(std::make_exception_ptr(e));
                }
            }
        }
    }).Detach();
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
        RegisterTopic(scheduler->remote->announceDown, "PING", {
            if (!scheduler->remote->isRegistered)
            {
                auto vReq = nlohmann::json::to_cbor(nlohmann::json({{"actid", 0}, {"actarg", scheduler->remote->devId}, {"cmd", "REGISTER"}, {"opt", "DEVICE"}}).dump());
                mqtt_publish(scheduler->remote->mq, const_cast<char *>(scheduler->remote->requestUp.c_str()), nvoid_new(vReq.data(), vReq.size()));
            }
            if (scheduler->remote->cloudFogInfo == nullptr && (scheduler->remote->cloudFogInfoCounter % CLOUD_FOG_COUNT_STEP) == 0)
            {
                auto vReq = nlohmann::json::to_cbor(nlohmann::json({{"actid", 0}, {"actarg", scheduler->remote->devId}, {"cmd", "GET-CF-INFO"}, {"opt", "DEVICE"}}).dump());
                mqtt_publish(scheduler->remote->mq, const_cast<char *>(scheduler->remote->requestUp.c_str()), nvoid_new(vReq.data(), vReq.size()));
                scheduler->remote->cloudFogInfoCounter = scheduler->remote->cloudFogInfoCounter + 1;
            }
            if (scheduler->remote->pongCounter == 0)
            {
                auto vReq = nlohmann::json::to_cbor(nlohmann::json({{"actid", 0}, {"actarg", scheduler->remote->devId}, {"cmd", "PONG"}, {"opt", "DEVICE"}}).dump());
                mqtt_publish(scheduler->remote->mq, const_cast<char *>(scheduler->remote->requestUp.c_str()), nvoid_new(vReq.data(), vReq.size()));
                scheduler->remote->pongCounter = rand() % PONG_COUNTER_MAX;
            }
            else
            {
                scheduler->remote->pongCounter = scheduler->remote->pongCounter - 1;
            }
            printf("Ping.. received... \n");
        });
        RegisterTopic(scheduler->remote->announceDown, "KILL", {
            scheduler->ShutDown();
        });
        RegisterTopic(scheduler->remote->replyDown, "REGISTER-ACK", {
            scheduler->remote->isRegistered = true;
        });
        RegisterTopic(scheduler->remote->announceDown, "PUT-CF-INFO", {
            if (rMsg.contains("opt") && rMsg["opt"].get<std::string>() == "ADD" &&
                rMsg.contains("actarg") && rMsg["actarg"].get<std::string>() == "fog") 
            {
                scheduler->remote->cloudFogInfo = std::make_unique<CloudFogInfo>(rMsg["number"].get<std::uint32_t>(), rMsg["isFixed"].get<bool>(), 
                rMsg["name"].get<std::string>(), rMsg["name"].get<std::string>());
            }
            else if (rMsg.contains("opt") && rMsg["opt"].get<std::string>() == "DEL")
            {
                scheduler->remote->cloudFogInfo = nullptr;
            }
        });
        RegisterTopic(scheduler->remote->replyDown, "REXEC-ACK", {
            if (rMsg.contains("actid")) 
            {
                auto actId = rMsg["actid"].get<uint32_t>();
                if (scheduler->remote->ackLookup.find(actId) != scheduler->remote->ackLookup.end()) 
                {
                    scheduler->remote->ackLookup[actId]->SetValue();
                    scheduler->remote->ackLookup.erase(actId);
                }
            }
        });
        RegisterTopic(scheduler->remote->requestDown, "REXEC-ASY", {
            printf("REXEC-ASY recevied \n");
            if (rMsg.contains("actid")) 
            {
                auto actId = rMsg["actid"].get<uint32_t>();
                if (scheduler->remote->cache.contains(actId) || scheduler->toContinue && scheduler->CreateRPBatchCall(std::move(rMsg))) {
                    auto vReq = nlohmann::json::to_cbor(nlohmann::json({{"actid", actId}, {"cmd", "REXEC-ACK"}}).dump());
                    mqtt_publish(scheduler->remote->mq, const_cast<char *>(scheduler->remote->replyUp.c_str()), nvoid_new(vReq.data(), vReq.size()));
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
        printf("Error... %s\n", e.what());
    }
END_REMOTE:
    mqtt_free_topic_msg(topicname, &msg);
    return 1;
}

bool JAMScript::RExecDetails::ArgumentGC()
{
    return false;
}