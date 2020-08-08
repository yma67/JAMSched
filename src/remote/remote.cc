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

JAMScript::Remote::Remote(RIBScheduler *scheduler, std::string hostAddr, std::string appName, std::string devName)
    : scheduler(scheduler), devId(std::move(devName)), appId(std::move(appName)), isRegistered(false), 
      requestUp(std::string("/") + appId + "/requests/up"), cloudFogInfoCounter(0U), 
      requestDown(std::string("/") + appId + "/requests/down/c"), cloudFogInfo(nullptr),
      replyUp(std::string("/") + appId + "/replies/up"), cache(1024), pongCounter(0U),
      replyDown(std::string("/") + appId + "/replies/down"), hostAddr(std::move(hostAddr)),
      announceDown(std::string("/") + appId + "/announce/down"),
      mq(mqtt_createserver(const_cast<char *>(hostAddr.c_str()), 1,
                           const_cast<char *>(devName.c_str()), connected))
{
    MQTTAsync_setMessageArrivedCallback(mq->mqttserv, this, RemoteArrivedCallback);
    mqtt_set_subscription(mq, const_cast<char *>(requestUp.c_str()));
    mqtt_set_subscription(mq, const_cast<char *>(requestDown.c_str()));
    mqtt_set_subscription(mq, const_cast<char *>(replyUp.c_str()));
    mqtt_set_subscription(mq, const_cast<char *>(replyDown.c_str()));
    mqtt_set_subscription(mq, const_cast<char *>(announceDown.c_str()));
    mqtt_connect(mq);
}

JAMScript::Remote::~Remote() 
{ 
    CancelAllRExecRequests();
    mqtt_deleteserver(mq); 
}

void JAMScript::Remote::CancelAllRExecRequests()
{
    std::lock_guard lk(mRexec);
    if (!isRegistered) return;
    isRegistered = false;
    if (!rLookup.empty())
    {
        for (auto& [id, fu]: rLookup)
        {
            fu->SetException(std::make_exception_ptr(RExecDetails::HeartbeatFailureException()));
        }
        rLookup.clear();
    }
    if (!ackLookup.empty())
    {
        for (auto& [id, fu]: ackLookup)
        {
            fu->SetException(std::make_exception_ptr(RExecDetails::HeartbeatFailureException()));
        }
        ackLookup.clear();
    }
}

bool JAMScript::Remote::CreateRetryTask(Future<void> &futureAck, std::vector<unsigned char> &vReq, uint32_t tempEID, std::function<void()> callback)
{
    scheduler->CreateBatchTask({true, 0, true}, Duration::max(), 
                               [this, callback { std::move(callback) }, vReq { std::move(vReq) }, 
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
            catch (const RExecDetails::HeartbeatFailureException& he)
            {
                callback();
                ThisTask::Exit();
            }
            catch (const InvalidArgumentException &e)
            {
                if (retryNum < 3)
                {
                    retryNum++;
                    continue;
                }
                {
                    std::lock_guard lk(mRexec);
                    ackLookup.erase(tempEID);
                }
                callback();
            }
        }
    }).Detach();
    return true;
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
    auto *remote = static_cast<Remote *>(ctx);
    printf("RemoteArrivedCallback....\n");
    try {
        std::vector<char> cbor_((char *)msg->payload, (char *)msg->payload + msg->payloadlen);
        nlohmann::json rMsg = nlohmann::json::parse(nlohmann::json::from_cbor(cbor_).get<std::string>());
        RegisterTopic(remote->announceDown, "PING", {
            if (!remote->isRegistered)
            {
                auto vReq = nlohmann::json::to_cbor(nlohmann::json({{"actid", 0}, {"actarg", remote->devId}, {"cmd", "REGISTER"}, {"opt", "DEVICE"}}).dump());
                mqtt_publish(remote->mq, const_cast<char *>(remote->requestUp.c_str()), nvoid_new(vReq.data(), vReq.size()));
            }
            if (remote->cloudFogInfo == nullptr && (remote->cloudFogInfoCounter % CLOUD_FOG_COUNT_STEP) == 0)
            {
                auto vReq = nlohmann::json::to_cbor(nlohmann::json({{"actid", 0}, {"actarg", remote->devId}, {"cmd", "GET-CF-INFO"}, {"opt", "DEVICE"}}).dump());
                mqtt_publish(remote->mq, const_cast<char *>(remote->requestUp.c_str()), nvoid_new(vReq.data(), vReq.size()));
                remote->cloudFogInfoCounter = remote->cloudFogInfoCounter + 1;
            }
            if (remote->pongCounter == 0)
            {
                auto vReq = nlohmann::json::to_cbor(nlohmann::json({{"actid", 0}, {"actarg", remote->devId}, {"cmd", "PONG"}, {"opt", "DEVICE"}}).dump());
                mqtt_publish(remote->mq, const_cast<char *>(remote->requestUp.c_str()), nvoid_new(vReq.data(), vReq.size()));
                remote->pongCounter = rand() % PONG_COUNTER_MAX;
            }
            else
            {
                remote->pongCounter = remote->pongCounter - 1;
            }
            printf("Ping.. received... \n");
        });
        RegisterTopic(remote->announceDown, "KILL", {
            remote->scheduler->ShutDown();
        });
        RegisterTopic(remote->announceDown, "REGISTER-ACK", {
            std::lock_guard lk(remote->mRexec);
            remote->isRegistered = true;
        });
        RegisterTopic(remote->announceDown, "PUT-CF-INFO", {
            if (rMsg.contains("opt") && rMsg["opt"].is_string() && rMsg["opt"].get<std::string>() == "ADD" &&
                rMsg.contains("actarg") && rMsg["actarg"].is_string() && rMsg["actarg"].get<std::string>() == "fog" && 
                rMsg.contains("hostAddr") && rMsg["hostAddr"].is_string() &&
                rMsg.contains("appName") && rMsg["appName"].is_string() &&
                rMsg.contains("devName") && rMsg["devName"].is_string()) 
            {
                auto deviceNameStr = rMsg["devName"].get<std::string>();
                auto appNameStr = rMsg["appName"].get<std::string>();
                auto hostAddrStr = rMsg["hostAddr"].get<std::string>();
                remote->cloudFogInfo = std::make_unique<CloudFogInfo>(deviceNameStr ,appNameStr, hostAddrStr);
                remote->scheduler->CreateConnection(std::move(hostAddrStr), std::move(appNameStr), std::move(deviceNameStr));
            }
            else if (rMsg.contains("opt")  && rMsg["opt"].is_string() && rMsg["opt"].get<std::string>() == "DEL" &&
                     rMsg.contains("hostAddr") && rMsg["hostAddr"].is_string())
            {
                remote->scheduler->DeleteConnectionByHostAddress(rMsg["hostAddr"].get<std::string>());
            }
        });
        RegisterTopic(remote->replyDown, "REXEC-ACK", {
            if (rMsg.contains("actid") && rMsg["actid"].is_number_unsigned()) 
            {
                auto actId = rMsg["actid"].get<uint32_t>();
                if (remote->ackLookup.find(actId) != remote->ackLookup.end()) 
                {
                    remote->ackLookup[actId]->SetValue();
                    remote->ackLookup.erase(actId);
                }
            }
        });
        RegisterTopic(remote->requestDown, "REXEC-ASY", {
            printf("REXEC-ASY recevied \n");
            if (rMsg.contains("actid") && rMsg["actid"].is_number_unsigned()) 
            {
                auto actId = rMsg["actid"].get<uint32_t>();
                if (remote->cache.contains(actId) || remote->scheduler->toContinue && remote->scheduler->CreateRPBatchCall(remote, std::move(rMsg))) {
                    auto vReq = nlohmann::json::to_cbor(nlohmann::json({{"actid", actId}, {"cmd", "REXEC-ACK"}}).dump());
                    mqtt_publish(remote->mq, const_cast<char *>(remote->replyUp.c_str()), nvoid_new(vReq.data(), vReq.size()));
                }
            }
        });
        RegisterTopic(remote->requestDown, "REXEC-SYN", {

        });
        RegisterTopic(remote->replyDown, "REXEC-RES", {
            if (rMsg.contains("actid") && rMsg["actid"].is_number_unsigned() && rMsg.contains("args")) 
            {
                auto actId = rMsg["actid"].get<uint32_t>();
                if (remote->rLookup.find(actId) != remote->rLookup.end()) 
                {
                    remote->rLookup[actId]->SetValue(rMsg["args"]);
                    remote->rLookup.erase(actId);
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