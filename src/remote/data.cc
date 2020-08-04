#include "remote/data.hpp"
#include <hiredis/hiredis.h>

JAMScript::BroadcastManager::BroadcastManager(Remote *remote, RedisState redisState, std::vector<JAMDataKeyType> variableInfo)
 : remote(remote), redisState(redisState), bCastEventLoop(event_base_new()), isBCastEventLoopDispatched(false)
{
    for (auto& vInfo: variableInfo)
    {
        if (bCastVarStores.find(vInfo.first) == bCastVarStores.end())
        {
            bCastVarStores.emplace(vInfo.first, std::unordered_map<std::string, std::unique_ptr<BroadcastVariable>>());
            nameSpaceMaxLen = std::max(vInfo.first.size(), nameSpaceMaxLen);
        }
        auto& nameSpaceRef = bCastVarStores[vInfo.first];
        if (nameSpaceRef.find(vInfo.second) == nameSpaceRef.end())
        {
            nameSpaceRef.emplace(vInfo.second, std::make_unique<BroadcastVariable>());
            varNameMaxLen = std::max(vInfo.second.size(), varNameMaxLen);
        }
    }
}

JAMScript::BroadcastManager::~BroadcastManager()
{
    event_base_free(bCastEventLoop);
    bCastEventLoop = nullptr;
}

static void BCastReceiveCallback(redisAsyncContext *c, void *r, void *privdata)
{
    auto* bCastManger = reinterpret_cast<JAMScript::BroadcastManager *>(privdata);
    auto *reply = reinterpret_cast<redisReply *>(r);
    if (reply == nullptr)
    {
        printf("ERROR! Null reply from Redis...\n");
        return;
    }
    if (reply->type == REDIS_REPLY_ARRAY)
    {
        bCastManger->Insert(std::string(reply->element[1]->str), reply->element[2]->str);
    }
}

void JAMScript::BroadcastManager::operator()(std::promise<void> prNotifier)
{
    auto* redisContext = redisAsyncConnect(redisState.redisServer.c_str(), redisState.redisPort);
    if (redisContext->err)
    {
        std::cerr << "Bad Redis Init for BCast" << std::endl;
        std::terminate();
    }
    redisAsyncSetConnectCallback(redisContext, [](const redisAsyncContext *c, int status) {
        if (status != REDIS_OK)
        {
            printf("JData Connection Error: %s\n", c->errstr);
            return;
        }
        printf("Connected... status: %d\n", status);
    });
    redisAsyncSetDisconnectCallback(redisContext, [](const redisAsyncContext *c, int status) {
        if (status != REDIS_OK)
        {
            printf("JData Disconnection Error: %s\n", c->errstr);
            return;
        }
        printf("Disconnected...\n");
    });
    redisLibeventAttach(redisContext, bCastEventLoop);
    for (auto& [nameSpace, bCastVarStore]: bCastVarStores)
    {
        for (auto& [varName, bCastVar]: bCastVarStore)
        {
            redisAsyncCommand(
                redisContext, BCastReceiveCallback, this, "SUBSCRIBE %s", 
                (std::string("aps[") + remote->appId + "].ns[" + nameSpace.c_str() + "].bcasts[" + varName.c_str() + "]").c_str()
            );
        }
    }
    if (!isBCastEventLoopDispatched)
    {
        isBCastEventLoopDispatched = true;
        prNotifier.set_value();
        event_base_dispatch(bCastEventLoop);
    }
    else
    {
        prNotifier.set_value();
    }
}

void JAMScript::BroadcastManager::Insert(std::string key, char* data)
{
    std::string appId(remote->appId.size(), 0), nameSpace(nameSpaceMaxLen, 0), varName(varNameMaxLen, 0);
    std::sscanf(key.c_str(), "aps[%s].ns[%s].bcasts[%s]", appId.data(), nameSpace.data(), varName.data());
    if (bCastVarStores.find(nameSpace) != bCastVarStores.end())
    {
        auto& nameSpaceRef = bCastVarStores[nameSpace];
        if (nameSpaceRef.find(varName) != nameSpaceRef.end())
        {
            auto& bCastVarRef = nameSpaceRef[varName];
            bCastVarRef->Insert(data);
        }
    }
}