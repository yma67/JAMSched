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
      mq(mqtt_createserver(const_cast<char *>(hostAddr.c_str()), 1, 
                           const_cast<char *>(appName.c_str()), 
                           const_cast<char *>(devName.c_str()), connected))
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
    nlohmann::json rMsg = nlohmann::json::from_cbor(cbor_);
    if (!strcmp(topicname, "/mach/func/request") && rMsg.contains("cmd") && rMsg["cmd"].is_string())
    {
        std::string cmd = rMsg["cmd"].get<std::string>();
        if (cmd == "REXEC-SYN" || cmd == "REXEC-ASY")
        {
            scheduler->CreateRPBatchCall(rMsg);
        }
    }
    mqtt_free_topic_msg(topicname, &msg);
    return 1;
}

bool JAMScript::RExecDetails::ArgumentGC()
{
    return false;
}