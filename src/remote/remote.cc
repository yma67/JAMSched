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

const std::string JAMScript::RExecDetails::HeartbeatFailureException::message_ = std::string("Cancelled due to bad remote connection");
std::unordered_set<JAMScript::CloudFogInfo *> JAMScript::Remote::isValidConnection;
JAMScript::SpinMutex JAMScript::Remote::mCallback;
ThreadPool JAMScript::Remote::callbackThreadPool(1);

static void connected(void *a)
{
    // a is a pointer to a mqtt_adapter_t structure.
    printf("Connected to ... mqtt... \n");
}

JAMScript::CloudFogInfo::CloudFogInfo(Remote *remote, std::string devId, std::string appId, std::string hostAddr)
    :   devId(std::move(devId)), appId(std::move(appId)), hostAddr(std::move(hostAddr)), 
        isRegistered(false), remote(remote),
        requestUp(std::string("/") + this->appId + "/requests/up"), 
        requestDown(std::string("/") + this->appId + "/requests/down/c"),
        replyUp(std::string("/") + this->appId + "/replies/up"), 
        replyDown(std::string("/") + this->appId + "/replies/down"), isExpired(false),
        announceDown(std::string("/") + this->appId + "/announce/down"), 
        pongCounter(0U), cloudFogInfoCounter(0U), prevHearbeat(Clock::now()),
        mqttAdapter(mqtt_createserver(const_cast<char *>(this->hostAddr.c_str()), 1, 
                    const_cast<char *>(this->devId.c_str()), connected))
{
    MQTTAsync_setMessageArrivedCallback(mqttAdapter->mqttserv, this, JAMScript::Remote::RemoteArrivedCallback);
    mqtt_set_subscription(mqttAdapter, const_cast<char *>(requestUp.c_str()));
    mqtt_set_subscription(mqttAdapter, const_cast<char *>(requestDown.c_str()));
    mqtt_set_subscription(mqttAdapter, const_cast<char *>(replyUp.c_str()));
    mqtt_set_subscription(mqttAdapter, const_cast<char *>(replyDown.c_str()));
    mqtt_set_subscription(mqttAdapter, const_cast<char *>(announceDown.c_str()));
    mqtt_connect(mqttAdapter);
}

bool JAMScript::CloudFogInfo::SendBuffer(const std::vector<uint8_t> &buffer)
{
    return mqtt_publish(mqttAdapter, const_cast<char *>("/replies/up"), 
                        nvoid_new(const_cast<uint8_t *>(buffer.data()), buffer.size()));
}

bool JAMScript::CloudFogInfo::SendBuffer(const std::vector<char> &buffer)
{
    return mqtt_publish(mqttAdapter, const_cast<char *>("/replies/up"), 
                        nvoid_new(const_cast<char *>(buffer.data()), buffer.size()));
}

void JAMScript::CloudFogInfo::Clear() 
{
    isRegistered = false;
    for (auto id: rExecPending)
    {
        if (remote->ackLookup.find(id) != remote->ackLookup.end())
        {
            remote->ackLookup[id]->SetValue(false);
        }
    }
    for (auto id: rExecPending)
    {
        if (remote->rLookup.find(id) != remote->rLookup.end())
        {
            remote->rLookup[id]->SetValue(std::make_pair(false, nlohmann::json({})));
        }
    }
    mqtt_deleteserver(mqttAdapter);
}

JAMScript::Remote::Remote(RIBScheduler *scheduler, std::string hostAddr, std::string appId, std::string devId)
    : scheduler(scheduler), cache(1024), mainFogInfo(nullptr),
      devId(std::move(devId)), appId(std::move(appId)), hostAddr(std::move(hostAddr))
{
    std::lock_guard lockCtor(mCallback);
    mainFogInfo = std::make_unique<CloudFogInfo>(this, this->devId, this->appId, this->hostAddr);
    Remote::isValidConnection.insert(mainFogInfo.get());
}

JAMScript::Remote::~Remote() 
{ 
    CancelAllRExecRequests();
}

void JAMScript::Remote::CheckExpire()
{
    while (scheduler->toContinue)
    {
        {
            std::unique_lock lkSleep { mLoopSleep };
            if (cvLoopSleep.wait_for(lkSleep, std::chrono::seconds(10), 
                                     [this] { return !scheduler->toContinue; }))
            {
                return;
            }
        }
        std::lock_guard expLock(mCallback);
        if (mainFogInfo != nullptr && mainFogInfo->isExpired)
        {
            mainFogInfo->Clear();
            Remote::isValidConnection.erase(mainFogInfo.get());
            mainFogInfo = nullptr;
        }
        else if (mainFogInfo != nullptr)
        {
            mainFogInfo->isExpired = true;
        }
        else
        {
            mainFogInfo = std::make_unique<CloudFogInfo>(this, devId, appId, hostAddr);
            Remote::isValidConnection.insert(mainFogInfo.get());
        }
        std::vector<std::string> expiredHosts;
        for (auto& [hostName, cfINFO]: cloudFogInfo)
        {
            if (cfINFO->isExpired)
            {
                cfINFO->Clear();
                expiredHosts.push_back(hostName);
            }
            else
            {
                cfINFO->isExpired = true;
            }
        }
        for (auto& host: expiredHosts)
        {
            Remote::isValidConnection.erase(cloudFogInfo[host].get());
            cloudFogInfo.erase(host);
        }
    }
}

void JAMScript::Remote::CancelAllRExecRequests()
{
    std::lock_guard lockClear(mCallback);
    if (mainFogInfo != nullptr)
    {
        mainFogInfo->Clear();
        mainFogInfo = nullptr;
    }
    for (auto& [hostName, cfINFO]: cloudFogInfo)
    {
        cfINFO->Clear();
    }
    cloudFogInfo.clear();
    Remote::isValidConnection.clear();
}

bool JAMScript::Remote::CreateRetryTaskSync(std::string hostName, Duration timeOut, nlohmann::json rexRequest, 
                                            std::shared_ptr<Promise<std::pair<bool, nlohmann::json>>> prCommon, 
                                            std::size_t countCommon, std::shared_ptr<std::atomic_size_t> failureCountCommon)
{
    scheduler->CreateBatchTask({true, 0, true}, Duration::max(), [
        this, hostName { std::move(hostName) }, rexRequest { std::move(rexRequest) }, prCommon { std::move(prCommon) }, 
        countCommon { std::move(countCommon) }, failureCountCommon { std::move(failureCountCommon) }, 
        timeOut { std::move(timeOut) }] () mutable {
        std::unique_lock lk(Remote::mCallback);
        if (cloudFogInfo.find(hostName) == cloudFogInfo.end() || !cloudFogInfo[hostName]->isRegistered)
        {
            lk.unlock();
            if (failureCountCommon->fetch_add(1U) == countCommon)
            {
                prCommon->SetValue(std::make_pair(false, nlohmann::json({"exception", std::string("heartbeat failed")})));
            }
            return;
        }
        rexRequest.push_back({"opt", cloudFogInfo[hostName]->devId});
        rexRequest.push_back({"actid", eIdFactory});
        auto vReq = nlohmann::json::to_cbor(rexRequest.dump());
        auto tempEID = eIdFactory;
        eIdFactory++;
        auto& prAck = ackLookup[tempEID] = std::make_unique<Promise<bool>>();
        auto futureAck = prAck->GetFuture();
        auto& pr = rLookup[tempEID] = std::make_unique<Promise<std::pair<bool, nlohmann::json>>>();
        auto fuExec = pr->GetFuture();
        mainFogInfo->rExecPending.insert(tempEID);
        lk.unlock();
        int retryNum = 0;
        while (retryNum < 3)
        {
            lk.lock();
            if (cloudFogInfo.find(hostName) == cloudFogInfo.end() || !cloudFogInfo[hostName]->isRegistered)
            {
                lk.unlock();
                if (failureCountCommon->fetch_add(1U) == countCommon)
                {
                    prCommon->SetValue(std::make_pair(false, nlohmann::json({"exception", std::string("heartbeat failed")})));
                }
                return;
            }
            auto& refCFINFO = cloudFogInfo[hostName];
            auto* ptrMQTTAdapter = refCFINFO->mqttAdapter;
            mqtt_publish(ptrMQTTAdapter, const_cast<char *>(refCFINFO->requestUp.c_str()), 
                            nvoid_new(vReq.data(), vReq.size()));
            lk.unlock();
            if (!futureAck.WaitFor(std::chrono::milliseconds(100)))
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
                if (failureCountCommon->fetch_add(1U) == countCommon)
                {
                    prCommon->SetValue(std::make_pair(false, nlohmann::json({"exception", std::string("retry failed")})));
                }
                return;
            }
            if (!futureAck.Get() && failureCountCommon->fetch_add(1U) == countCommon)
            {
                prCommon->SetValue(std::make_pair(false, nlohmann::json({"exception", std::string("heartbeat failed")})));
            }
            break;
        }
        try 
        {
            if (!fuExec.WaitFor(timeOut))
            {
                if (failureCountCommon->fetch_add(1U) == countCommon)
                {
                    prCommon->SetValue(std::make_pair(false, nlohmann::json({"exception", std::string("execution timeout")})));
                }
                return;
            }
            auto valueResult = fuExec.Get();
            if (!valueResult.first)
            {
                if (failureCountCommon->fetch_add(1U) == countCommon)
                {
                    prCommon->SetValue(std::make_pair(false, nlohmann::json({"exception", std::string("heartbeat failed")})));
                }
                return;
            }
            prCommon->SetValue(std::move(valueResult));
        }
        catch (const std::exception &e)
        {
            if (failureCountCommon->fetch_add(1U) == countCommon)
            {
                prCommon->SetValue(std::make_pair(false, nlohmann::json({"exception", std::string(e.what())})));
            }
            return;
        }
    }).Detach();
    return true;
}

bool JAMScript::Remote::CreateRetryTaskSync(Duration timeOut, nlohmann::json rexRequest, 
                                            std::shared_ptr<Promise<std::pair<bool, nlohmann::json>>> prCommon, 
                                            std::size_t countCommon, std::shared_ptr<std::atomic_size_t> failureCountCommon)
{
    scheduler->CreateBatchTask({true, 0, true}, Duration::max(), [
        this, rexRequest { std::move(rexRequest) }, prCommon { std::move(prCommon) }, 
        countCommon { std::move(countCommon) }, timeOut { std::move(timeOut) },
        failureCountCommon { std::move(failureCountCommon) }] () mutable {
        std::unique_lock lk(Remote::mCallback);
        if (mainFogInfo == nullptr || !mainFogInfo->isRegistered)
        {
            lk.unlock();
            if (failureCountCommon->fetch_add(1U) == countCommon)
            {
                prCommon->SetValue(std::make_pair(false, nlohmann::json({"exception", std::string("heartbeat failed")})));
            }
            return;
        }
        rexRequest.push_back({"opt", mainFogInfo->devId});
        rexRequest.push_back({"actid", eIdFactory});
        auto vReq = nlohmann::json::to_cbor(rexRequest.dump());
        auto tempEID = eIdFactory;
        eIdFactory++;
        auto& prAck = ackLookup[tempEID] = std::make_unique<Promise<bool>>();
        auto futureAck = prAck->GetFuture();
        auto& pr = rLookup[tempEID] = std::make_unique<Promise<std::pair<bool, nlohmann::json>>>();
        auto fuExec = pr->GetFuture();
        mainFogInfo->rExecPending.insert(tempEID);
        lk.unlock();
        int retryNum = 0;
        while (retryNum < 3)
        {
            lk.lock();
            if (mainFogInfo == nullptr || !mainFogInfo->isRegistered)
            {
                lk.unlock();
                if (failureCountCommon->fetch_add(1U) == countCommon)
                {
                    prCommon->SetValue(std::make_pair(false, nlohmann::json({"exception", std::string("heartbeat failed")})));
                }
                return;
            }
            auto* ptrMQTTAdapter = mainFogInfo->mqttAdapter;
            mqtt_publish(ptrMQTTAdapter, const_cast<char *>(mainFogInfo->requestUp.c_str()), 
                            nvoid_new(vReq.data(), vReq.size()));
            lk.unlock();
            if (!futureAck.WaitFor(std::chrono::milliseconds(100)))
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
                if (failureCountCommon->fetch_add(1U) == countCommon)
                {
                    prCommon->SetValue(std::make_pair(false, nlohmann::json({"exception", std::string("retry failed")})));
                }
                return;
            }
            if (!futureAck.Get())
            {
                if (failureCountCommon->fetch_add(1U) == countCommon)
                {
                    prCommon->SetValue(std::make_pair(false, nlohmann::json({"exception", std::string("heartbeat failed")})));
                }
                return;
            }
            break;
        }
        try 
        {
            if (!fuExec.WaitFor(timeOut))
            {
                if (failureCountCommon->fetch_add(1U) == countCommon)
                {
                    prCommon->SetValue(std::make_pair(false, nlohmann::json({"exception", std::string("execution timeout")})));
                }
                return;
            }
            auto valueResult = fuExec.Get();
            if (!valueResult.first)
            {
                if (failureCountCommon->fetch_add(1U) == countCommon)
                {
                    prCommon->SetValue(std::make_pair(false, nlohmann::json({"exception", std::string("heartbeat failed")})));
                }
                return;
            }
            prCommon->SetValue(std::move(valueResult));
        }
        catch (const std::exception &e)
        {
            if (failureCountCommon->fetch_add(1U) == countCommon)
            {
                prCommon->SetValue(std::make_pair(false, nlohmann::json({"exception", std::string(e.what())})));
            }
            return;
        }
    }).Detach();
    return true;
}

bool JAMScript::Remote::CreateRetryTask(Future<bool> &futureAck, std::vector<unsigned char> &vReq, uint32_t tempEID, 
                                        std::function<void()> callback)
{
    scheduler->CreateBatchTask({true, 0, true}, Duration::max(), 
                               [this, callback { std::move(callback) }, vReq { std::move(vReq) }, 
                                futureAck { std::move(futureAck) }, tempEID]() mutable {
        int retryNum = 0;
        while (retryNum < 3)
        {
            {
                std::unique_lock lkPublish(Remote::mCallback);
                if (mainFogInfo == nullptr || !mainFogInfo->isRegistered)
                {
                    lkPublish.unlock();
                    callback();
                    return;
                }
                auto* ptrMQTTAdapter = mainFogInfo->mqttAdapter;
                mqtt_publish(ptrMQTTAdapter, const_cast<char *>(mainFogInfo->requestUp.c_str()), 
                                nvoid_new(vReq.data(), vReq.size()));
                lkPublish.unlock();
            }
            if (!futureAck.WaitFor(std::chrono::milliseconds(100)))
            {
                if (retryNum < 3)
                {
                    retryNum++;
                    continue;
                }
                {
                    std::lock_guard lk(Remote::mCallback);
                    ackLookup.erase(tempEID);
                }
                callback();
                return;
            }
            return;
        }
    }).Detach();
    return true;
}

bool JAMScript::Remote::CreateRetryTask(std::string hostName, Future<bool> &futureAck, 
                                        std::vector<unsigned char> &vReq, 
                                        uint32_t tempEID, std::function<void()> callback)
{
    scheduler->CreateBatchTask({true, 0, true}, Duration::max(), 
                               [this, hostName{ std::move(hostName) }, callback { std::move(callback) }, 
                                vReq { std::move(vReq) }, 
                                futureAck { std::move(futureAck) }, tempEID]() mutable {
        int retryNum = 0;
        while (retryNum < 3)
        {
            {
                std::unique_lock lkPublish(Remote::mCallback);
                if (cloudFogInfo.find(hostName) == cloudFogInfo.end() || !cloudFogInfo[hostName]->isRegistered)
                {
                    lkPublish.unlock();
                    callback();
                    return;
                }
                auto& refCFINFO = cloudFogInfo[hostName];
                auto* ptrMQTTAdapter = refCFINFO->mqttAdapter;
                mqtt_publish(ptrMQTTAdapter, const_cast<char *>(refCFINFO->requestUp.c_str()), 
                                nvoid_new(vReq.data(), vReq.size()));
                lkPublish.unlock();
            }
            if (!futureAck.WaitFor(std::chrono::milliseconds(100)))
            {
                if (retryNum < 3)
                {
                    retryNum++;
                    continue;
                }
                {
                    std::lock_guard lk(Remote::mCallback);
                    ackLookup.erase(tempEID);
                }
                callback();
                return;
            }
            return;
        }
    }).Detach();
    return true;
}

#define RegisterTopic(topicName, commandName, ...) {                                                                   \
    if (topicNameString == topicName && rMsg.contains("cmd") && rMsg["cmd"].is_string())                               \
    {                                                                                                                  \
        std::string cmd = rMsg["cmd"].get<std::string>();                                                              \
        if (cmd == commandName)                                                                                        \
        {                                                                                                              \
            __VA_ARGS__                                                                                                \
            return;                                                                                                    \
        }                                                                                                              \
    }                                                                                                                  \
}

int JAMScript::Remote::RemoteArrivedCallback(void *ctx, char *topicname, int topiclen, MQTTAsync_message *msg)
{
    auto *cfINFO = static_cast<CloudFogInfo *>(ctx);
    std::vector<char> cbor_((char *)msg->payload, (char *)msg->payload + msg->payloadlen);
    nlohmann::json rMsg = nlohmann::json::parse(nlohmann::json::from_cbor(cbor_).get<std::string>());
    std::string topicNameString(topicname);
    Remote::callbackThreadPool.enqueue(
    [cfINFO, rMsg { std::move(rMsg) }, topicNameString { std::move(topicNameString) }] {
        auto *remote = cfINFO->remote;
        std::unique_lock lkValidConn(mCallback);
        if (isValidConnection.find(cfINFO) == isValidConnection.end()) 
        {
            return;
        }
        try {
            RegisterTopic(cfINFO->announceDown, "PING", {
                if (!cfINFO->isRegistered)
                {
                    auto vReq = nlohmann::json::to_cbor(nlohmann::json({
                        {"actid", 0}, {"actarg", cfINFO->devId}, {"cmd", "REGISTER"}, {"opt", "DEVICE"}}).dump());
                    mqtt_publish(cfINFO->mqttAdapter, const_cast<char *>(cfINFO->requestUp.c_str()), 
                                 nvoid_new(vReq.data(), vReq.size()));
                }
                if (cfINFO == remote->mainFogInfo.get() && remote->cloudFogInfo.empty() && 
                    (cfINFO->cloudFogInfoCounter % CLOUD_FOG_COUNT_STEP) == 0)
                {
                    auto vReq = nlohmann::json::to_cbor(nlohmann::json({
                        {"actid", 0}, {"actarg", cfINFO->devId}, {"cmd", "GET-CF-INFO"}, {"opt", "DEVICE"}}).dump());
                    mqtt_publish(cfINFO->mqttAdapter, const_cast<char *>(cfINFO->requestUp.c_str()), 
                                 nvoid_new(vReq.data(), vReq.size()));
                    cfINFO->cloudFogInfoCounter = cfINFO->cloudFogInfoCounter + 1;
                }
                if (cfINFO->pongCounter == 0)
                {
                    auto vReq = nlohmann::json::to_cbor(nlohmann::json({
                        {"actid", 0}, {"actarg", cfINFO->devId}, {"cmd", "PONG"}, {"opt", "DEVICE"}}).dump());
                    mqtt_publish(cfINFO->mqttAdapter, const_cast<char *>(cfINFO->requestUp.c_str()), 
                                 nvoid_new(vReq.data(), vReq.size()));
                    cfINFO->pongCounter = rand() % PONG_COUNTER_MAX;
                }
                else
                {
                    cfINFO->pongCounter = cfINFO->pongCounter - 1;
                }
                cfINFO->isExpired = false;
                printf("Ping.. received... \n");
            });
            RegisterTopic(cfINFO->announceDown, "KILL", {
                lkValidConn.unlock();
                remote->scheduler->ShutDown();
                lkValidConn.lock();
            });
            RegisterTopic(cfINFO->announceDown, "REGISTER-ACK", {
                cfINFO->isRegistered = true;
                cfINFO->isExpired = false;
            });
            RegisterTopic(cfINFO->announceDown, "PUT-CF-INFO", {
                if (rMsg.contains("opt") && rMsg["opt"].is_string() && 
                    rMsg["opt"].get<std::string>() == "ADD" &&
                    rMsg.contains("actarg") && rMsg["actarg"].is_string() && 
                    rMsg["actarg"].get<std::string>() == "fog" && 
                    rMsg.contains("hostAddr") && rMsg["hostAddr"].is_string() &&
                    rMsg.contains("appName") && rMsg["appName"].is_string() &&
                    rMsg.contains("devName") && rMsg["devName"].is_string()) 
                {
                    auto deviceNameStr = rMsg["devName"].get<std::string>();
                    auto appNameStr = rMsg["appName"].get<std::string>();
                    auto hostAddrStr = rMsg["hostAddr"].get<std::string>();
                    remote->cloudFogInfo.emplace(hostAddrStr, 
                        std::make_unique<CloudFogInfo>(remote, deviceNameStr ,appNameStr, hostAddrStr));
                    Remote::isValidConnection.insert(remote->cloudFogInfo[hostAddrStr].get());
                }
                else if (rMsg.contains("opt")  && rMsg["opt"].is_string() && 
                         rMsg["opt"].get<std::string>() == "DEL" &&
                         rMsg.contains("hostAddr") && rMsg["hostAddr"].is_string())
                {
                    auto hostAddrStr = rMsg["hostAddr"].get<std::string>();
                    Remote::isValidConnection.erase(remote->cloudFogInfo[hostAddrStr].get());
                    remote->cloudFogInfo.erase(hostAddrStr);
                }
            });
            RegisterTopic(cfINFO->replyDown, "REXEC-ACK", {
                if (rMsg.contains("actid") && rMsg["actid"].is_number_unsigned()) 
                {
                    auto actId = rMsg["actid"].get<uint32_t>();
                    if (remote->ackLookup.find(actId) != remote->ackLookup.end()) 
                    {
                        auto& refRLookup = remote->ackLookup[actId];
                        refRLookup->SetValue(true);
                        remote->ackLookup.erase(actId);
                    }
                }
            });
            RegisterTopic(cfINFO->requestDown, "REXEC-ASY", {
                printf("REXEC-ASY recevied \n");
                if (rMsg.contains("actid") && rMsg["actid"].is_number_unsigned()) 
                {
                    auto actId = rMsg["actid"].get<uint32_t>();
                    if (remote->cache.contains(actId) || remote->scheduler->toContinue && 
                        remote->scheduler->CreateRPBatchCall(cfINFO, std::move(rMsg))) {
                        auto vReq = nlohmann::json::to_cbor(nlohmann::json({
                            {"actid", actId}, {"cmd", "REXEC-ACK"}}).dump());
                        mqtt_publish(cfINFO->mqttAdapter, const_cast<char *>(cfINFO->replyUp.c_str()), 
                                     nvoid_new(vReq.data(), vReq.size()));
                    }   
                }
            });
            RegisterTopic(cfINFO->requestDown, "REXEC-SYN", {

            });
            RegisterTopic(cfINFO->replyDown, "REXEC-RES", {
                if (rMsg.contains("actid") && rMsg["actid"].is_number_unsigned() && 
                    rMsg.contains("args")) 
                {
                    auto actId = rMsg["actid"].get<uint32_t>();
                    if (remote->rLookup.find(actId) != remote->rLookup.end()) 
                    {
                        auto& refRLookup = remote->rLookup[actId];
                        refRLookup->SetValue(std::make_pair(true, rMsg["args"]));
                        cfINFO->rExecPending.erase(actId);
                        remote->rLookup.erase(actId);
                    }
                }
            });
        } catch (const std::exception& e) {
            e.what();
            printf("Error... %s\n", e.what());
        }
    });
    mqtt_free_topic_msg(topicname, &msg);
    return 1;
}

bool JAMScript::RExecDetails::ArgumentGC()
{
    return false;
}