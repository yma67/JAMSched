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
      requestDown(std::string("/") + appName + "/requests/down/c"), 
      replyUp(std::string("/") + appName + "/replies/up"), cache(1024),
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
    scheduler->CreateBatchTask({true, 0, true}, Duration::max(), [this, prAck { std::move(prAck) }, vReq { std::move(vReq) }, futureAck { std::move(futureAck) }, tempEID]() mutable {
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

nlohmann::json JAMScript::Remote::CreateAckMessage(uint32_t actId) { 
    nlohmann::json jck = 
                    {  
                        {"actid", actId}, 
                        {"cmd", "REXEC-ACK"} 
                    }; 
    return std::move(jck);
}

int JAMScript::Remote::RemoteArrivedCallback(void *ctx, char *topicname, int topiclen, MQTTAsync_message *msg)
{
    auto *scheduler = static_cast<RIBScheduler *>(ctx);
    try {
        std::vector<char> cbor_((char *)msg->payload, (char *)msg->payload + msg->payloadlen);
        nlohmann::json rMsg = nlohmann::json::parse(nlohmann::json::from_cbor(cbor_).get<std::string>());
        RegisterTopic(scheduler->remote->announceDown, "PING", {
            printf("Ping.. received... \n");
        });
        RegisterTopic(scheduler->remote->announceDown, "KILL", {
            scheduler->ShutDown();
        });
        RegisterTopic(scheduler->remote->replyDown, "REGISTER-ACK", {

        });
        RegisterTopic(scheduler->remote->announceDown, "PUT-CF-INFO", {

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
                if (scheduler->remote->cache.contains(actId))
                {
                    auto jAck = scheduler->remote->CreateAckMessage(actId);
                    auto vReq = nlohmann::json::to_cbor(jAck.dump());
                    mqtt_publish(scheduler->remote->mq, const_cast<char *>(scheduler->remote->replyUp.c_str()), nvoid_new(vReq.data(), vReq.size()));
                }
                if (scheduler->toContinue && scheduler->CreateRPBatchCall(std::move(rMsg))) {
                    auto jAck = scheduler->remote->CreateAckMessage(actId);
                    auto vReq = nlohmann::json::to_cbor(jAck.dump());
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