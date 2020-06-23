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
    : scheduler(scheduler),
      mq(mqtt_createserver(const_cast<char *>(hostAddr.c_str()), 1, const_cast<char *>(appName.c_str()), const_cast<char *>(devName.c_str()), connected))
{
    MQTTAsync_setMessageArrivedCallback(mq->mqttserv, scheduler, RemoteArrivedCallback);
    mqtt_connect(mq);
    mqtt_set_subscription(mq, const_cast<char *>("/schedule"));
    mqtt_set_subscription(mq, const_cast<char *>("/rexec-response"));
}

JAMScript::Remote::~Remote() { mqtt_deleteserver(mq); }

int JAMScript::Remote::RemoteArrivedCallback(void *ctx, char *topicname, int topiclen, MQTTAsync_message *msg)
{
    auto *scheduler = static_cast<RIBScheduler *>(ctx);
    std::vector<std::uint8_t> cbor_;
    cbor_.assign((uint8_t *)msg->payload, (uint8_t *)msg->payload + msg->payloadlen);
    nlohmann::json rResponse = nlohmann::json::from_cbor(cbor_);
    if (strcmp(topicname, "/schedule"))
    {
        std::vector<RealTimeSchedule> greedy, normal;
        for (auto &ent : rResponse["greedy"])
        {
            greedy.push_back({std::chrono::high_resolution_clock::duration(std::chrono::microseconds(ent["start"].get<uint64_t>())),
                              std::chrono::high_resolution_clock::duration(std::chrono::microseconds(ent["end"].get<uint64_t>())),
                              ent["id"].get<uint32_t>()});
        }
        for (auto &ent : rResponse["normal"])
        {
            normal.push_back({std::chrono::high_resolution_clock::duration(std::chrono::microseconds(ent["start"].get<uint64_t>())),
                              std::chrono::high_resolution_clock::duration(std::chrono::microseconds(ent["end"].get<uint64_t>())),
                              ent["id"].get<uint32_t>()});
        }
        scheduler->SetSchedule(normal, greedy);
    }
    if (strcmp(topicname, "/rexec-response"))
    {
        auto resId = rResponse["execId"].get<uint32_t>();
        std::lock_guard lk(scheduler->remote->mRexec);
        auto& pf = scheduler->remote->rLookup[resId];
        if (rResponse.contains("result")) {
            pf->SetValue(rResponse["result"]);
        } else if (rResponse.contains("exception")) {
            pf->SetException(std::make_exception_ptr(InvalidArgumentException(rResponse["exception"])));
        } else {
            pf->SetException(std::make_exception_ptr(InvalidArgumentException("Invalid Format\n")));
        }
        scheduler->remote->rLookup.erase(resId);
    }
    mqtt_free_topic_msg(topicname, &msg);
    return 1;
}