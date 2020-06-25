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
    mqtt_set_subscription(mq, const_cast<char *>("/mach/func/request"));
}

JAMScript::Remote::~Remote() { mqtt_deleteserver(mq); }

int JAMScript::Remote::RemoteArrivedCallback(void *ctx, char *topicname, int topiclen, MQTTAsync_message *msg)
{
    auto *scheduler = static_cast<RIBScheduler *>(ctx);
    std::vector<char> cbor_((char *)msg->payload, (char *)msg->payload + msg->payloadlen);
    nlohmann::json rMsg = nlohmann::json::parse(nlohmann::json::from_cbor(cbor_).get<std::string>());
    if (!strcmp(topicname, "/app-1/mach/func/request") && rMsg.contains("cmd") && rMsg["cmd"].is_string())
    {
        std::string cmd = rMsg["cmd"].get<std::string>();
        if (cmd == "REXEC-SYN" || cmd == "REXEC-ASY")
        {
            scheduler->CreateRPBatchCall(rMsg);
        }
        if (cmd == "KILL")
        {
            scheduler->ShutDown();
        }
    }
    mqtt_free_topic_msg(topicname, &msg);
    return 1;
}

bool JAMScript::RExecDetails::ArgumentGC()
{
    return false;
}