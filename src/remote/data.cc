#include "remote/data.hpp"
#include "remote/remote.hpp"
#include "scheduler/scheduler.hpp"
#include <hiredis/hiredis.h>

#define ConvertToRedisKey(apId_, nameSpace_, varName_) \
    (std::string("aps[") + apId_ + "].ns[" + nameSpace_ + "].bcasts[" + varName_.c_str() + "]").c_str()

jamc::LogManager::LogManager(Remote *remote, RedisState redisState) 
 : remote(remote), redisState(redisState), loggerEventLoop(event_base_new()),
   redisContext(redisAsyncConnect(redisState.redisServer.c_str(), redisState.redisPort))
{
    redisAsyncSetConnectCallback(redisContext, [](const redisAsyncContext *c, int status) {
        if (status != REDIS_OK)
        {
            printf("JData Logger Connection Error: %s\n", c->errstr);
            return;
        }
        printf("Connected... status: %d\n", status);
    });
    redisAsyncSetDisconnectCallback(redisContext, [](const redisAsyncContext *c, int status) {
        if (status != REDIS_OK)
        {
            printf("JData Logger Disconnection Error: %s\n", c->errstr);
            return;
        }
        printf("Disconnected...\n");
    });
    redisLibeventAttach(redisContext, loggerEventLoop);
}

jamc::LogManager::~LogManager()
{
    event_base_free(loggerEventLoop);
    loggerEventLoop = nullptr;
    redisAsyncDisconnect(redisContext);
}

void jamc::LogManager::LogRaw(const std::string &nameSpace, const std::string &varName, const nlohmann::json &streamObjectRaw)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    std::shared_lock lk(Remote::mCallback);
    if (remote->mainFogInfo == nullptr)
    {
        throw InvalidArgumentException("Fog not connected");
    }
    auto appId = remote->mainFogInfo->appId;
    lk.unlock();
    std::lock_guard lkAsyncBuffer(mAsyncBuffer);
    asyncBufferEncoded.push_back(
        new LogStreamObject(
            ConvertToRedisKey(appId, nameSpace, varName), 
            tv.tv_sec * 1000LL + tv.tv_usec / 1000,
            nlohmann::json::to_cbor(streamObjectRaw.dump())
        )
    );
    cvAsyncBuffer.notify_one();
}

void jamc::LogManager::RunLoggerMainLoop()
{
    std::thread tLoggerLibEvent([this] {
        while (remote->scheduler->toContinue)
        {
            event_base_dispatch(loggerEventLoop);
        }
    });
    while (remote->scheduler->toContinue)
    {
        std::unique_lock lock(mAsyncBuffer);
        while (asyncBufferEncoded.empty())
        {
            cvAsyncBuffer.wait(lock);
        }
        while (!asyncBufferEncoded.empty())
        {
            auto* ptrBufferEncoded = asyncBufferEncoded.front();
            asyncBufferEncoded.pop_front();
            redisAsyncCommand(
                redisContext, 
                [](redisAsyncContext *c, void *reply, void *privdata) {
                    delete reinterpret_cast<LogStreamObject *>(privdata);
                }, 
                ptrBufferEncoded, "XADD %s %llu %b", 
                ptrBufferEncoded->logKey.c_str(),
                ptrBufferEncoded->timeStamp, 
                ptrBufferEncoded->encodedObject.data(), 
                ptrBufferEncoded->encodedObject.size()
            );
        }
    }
    event_base_loopbreak(loggerEventLoop);
    tLoggerLibEvent.join();
}

void jamc::BroadcastVariable::Append(char* data)
{
    std::lock_guard lk(mVarStream);
    auto* ptrDataStart = reinterpret_cast<std::uint8_t *>(data);
    varStream.push_back({ ptrDataStart, ptrDataStart + std::strlen(data) });
    cvVarStream.notify_one();
}

nlohmann::json jamc::BroadcastVariable::Get()
{
    std::unique_lock lk(mVarStream);
    while (varStream.empty() && !isCancelled) cvVarStream.wait(lk);
    if (isCancelled) return nlohmann::json({{"Exception", "Get Value after Scheduler Shutdown"}});
    auto streamObjectRaw = std::move(varStream.front());
    varStream.pop_front();
    lk.unlock();
    return nlohmann::json::parse(nlohmann::json::from_cbor(streamObjectRaw).get<std::string>());
}

jamc::BroadcastManager::BroadcastManager(Remote *remote, RedisState redisState, std::vector<JAMDataKeyType> variableInfo)
 : remote(remote), redisState(redisState), bCastEventLoop(event_base_new()), 
   redisContext(redisAsyncConnect(redisState.redisServer.c_str(), redisState.redisPort))
{
    if (redisContext->err)
    {
        std::cerr << "Bad Redis Init for BCast" << std::endl;
        std::terminate();
    }
    redisAsyncSetConnectCallback(redisContext, [](const redisAsyncContext *c, int status) {
        if (status != REDIS_OK)
        {
            printf("JData Broadcaster Connection Error: %s\n", c->errstr);
            return;
        }
        printf("Connected... status: %d\n", status);
    });
    redisAsyncSetDisconnectCallback(redisContext, [](const redisAsyncContext *c, int status) {
        if (status != REDIS_OK)
        {
            printf("JData Broadcaster Disconnection Error: %s\n", c->errstr);
            return;
        }
        printf("Disconnected...\n");
    });
    redisLibeventAttach(redisContext, bCastEventLoop);
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
            std::shared_lock lk(Remote::mCallback);
            if (remote->mainFogInfo == nullptr)
            {
                throw InvalidArgumentException("Fog not connected");
            }
            auto pBCVar = std::make_unique<BroadcastVariable>(ConvertToRedisKey(remote->mainFogInfo->appId, vInfo.first, vInfo.second));
            lk.unlock();
            auto& refBCastVar = *pBCVar;
            nameSpaceRef.emplace(vInfo.second, std::move(pBCVar));
            redisAsyncCommand(
                redisContext, [](redisAsyncContext *c, void *r, void *privdata)
                {
                    auto* bCastManger = reinterpret_cast<jamc::BroadcastManager *>(privdata);
                    auto *reply = reinterpret_cast<redisReply *>(r);
                    if (reply == nullptr)
                    {
                        printf("ERROR! Null reply from Redis...\n");
                        return;
                    }
                    if (reply->type == REDIS_REPLY_ARRAY)
                    {
                        bCastManger->Append(std::string(reply->element[1]->str), reply->element[2]->str);
                    }
                }, this, "SUBSCRIBE %s", 
                refBCastVar.GetBroadcastKey().c_str()
            );
            varNameMaxLen = std::max(vInfo.second.size(), varNameMaxLen);
        }
    }
}

jamc::BroadcastManager::~BroadcastManager()
{
    event_base_free(bCastEventLoop);
    bCastEventLoop = nullptr;
    redisAsyncDisconnect(redisContext);
}

void jamc::BroadcastManager::RunBroadcastMainLoop()
{
    while (remote->scheduler->toContinue)
    {
        event_base_dispatch(bCastEventLoop);
    }
}

void jamc::BroadcastManager::StopBroadcastMainLoop()
{
    event_base_loopbreak(bCastEventLoop);
}

nlohmann::json jamc::BroadcastManager::Get(const std::string &nameSpace, const std::string &variableName)
{
    return bCastVarStores[nameSpace][variableName]->Get();
}

void jamc::BroadcastManager::Append(std::string key, char* data)
{
    std::shared_lock lk(Remote::mCallback);
    if (remote->mainFogInfo == nullptr)
    {
        return;
    }
    std::string appId(remote->mainFogInfo->appId.size(), 0), nameSpace(nameSpaceMaxLen, 0), varName(varNameMaxLen, 0);
    lk.unlock();
    std::sscanf(key.c_str(), "aps[%s].ns[%s].bcasts[%s]", appId.data(), nameSpace.data(), varName.data());
    if (bCastVarStores.find(nameSpace) != bCastVarStores.end())
    {
        auto& nameSpaceRef = bCastVarStores[nameSpace];
        if (nameSpaceRef.find(varName) != nameSpaceRef.end())
        {
            auto& bCastVarRef = nameSpaceRef[varName];
            bCastVarRef->Append(data);
        }
    }
}