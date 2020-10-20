#include "remote/data.hpp"
#include "remote/remote.hpp"
#include "scheduler/scheduler.hpp"
#include <hiredis/hiredis.h>

#define ConvertToRedisKey(apId_, nameSpace_, varName_) \
    (std::string("aps[") + apId_ + "].ns[" + nameSpace_ + "].bcasts[" + varName_.c_str() + "]").c_str()

static void ConnectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Connected...\n");
}

static void DisConnectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        return;
    }
    printf("Disconnected...\n");
}

jamc::LogManager::LogManager(Remote *remote, RedisState redisState) 
 : remote(remote), redisState(redisState), loggerEventLoop(event_base_new()), 
   rc(redisAsyncConnect(redisState.redisServer.c_str(), redisState.redisPort))
{
    redisLibeventAttach(rc, loggerEventLoop);
    redisAsyncSetConnectCallback(rc, ConnectCallback);
    redisAsyncSetDisconnectCallback(rc, DisConnectCallback);
    event_base_loop(loggerEventLoop, EVLOOP_NONBLOCK);
}

jamc::LogManager::~LogManager()
{
    redisAsyncFree(rc);
    event_base_free(loggerEventLoop);
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
    auto* lobj = new LogStreamObject(
        ConvertToRedisKey(appId, nameSpace, varName), 
        tv.tv_sec * 1000LL + tv.tv_usec / 1000,
        nlohmann::json::to_cbor(streamObjectRaw.dump())
    );
    std::lock_guard lkAsyncBuffer(mAsyncBuffer);
    asyncBufferEncoded.push_back(lobj);
    if (asyncBufferEncoded.size() == 1) cvAsyncBuffer.notify_one();
}

void jamc::LogManager::RunLoggerMainLoop()
{
#ifdef JAMSCRIPT_SCHED_AI_EXP
    int nacc = 0;
#endif
    std::deque<LogStreamObject *> localBuffer;
    std::unique_lock l1(mAsyncBuffer);
    while (remote->scheduler->toContinue)
    {
        l1.unlock();
        {
            std::unique_lock lock(mAsyncBuffer);
            while (asyncBufferEncoded.size() < 200 && remote->scheduler->toContinue)
            {
                cvAsyncBuffer.wait(lock);
            }
            localBuffer = std::move(asyncBufferEncoded);
            asyncBufferEncoded = std::deque<LogStreamObject *>();
        }
#ifdef JAMSCRIPT_SCHED_AI_EXP
        long long dx = 0;
        auto est = std::chrono::high_resolution_clock::now();
#endif
        while (!localBuffer.empty())
        {
            auto ptrBufferEncoded = localBuffer.front();
            localBuffer.pop_front();
#ifdef JAMSCRIPT_SCHED_AI_EXP
            nacc++;
            dx++;
#endif
            redisAsyncCommand(rc, 
                [](redisAsyncContext *c, void *r, void *priv) {
                    delete reinterpret_cast<LogStreamObject *>(priv);
                }, 
                ptrBufferEncoded,
                "ZADD %s %llu %b", 
                ptrBufferEncoded->logKey.c_str(),
                ptrBufferEncoded->timeStamp, 
                ptrBufferEncoded->encodedObject.data(), 
                ptrBufferEncoded->encodedObject.size());
        }
        event_base_loop(loggerEventLoop, EVLOOP_ONCE);
#ifdef JAMSCRIPT_SCHED_AI_EXP
        auto dt = std::chrono::duration_cast<std::chrono::microseconds>((std::chrono::high_resolution_clock::now() - est)).count();
        printf("reqs = %lld, elapsed = %lld, req per sec = %lf\n", dx, dt, double(dx) * 1000000.0f / double(dt));
#endif
        l1.lock();
    }
#ifdef JAMSCRIPT_SCHED_AI_EXP
    printf("%d\n", nacc);
#endif
}
void jamc::LogManager::StopLoggerMainLoop()
{
    event_base_loopbreak(loggerEventLoop);
    cvAsyncBuffer.notify_one();
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
   rc(redisAsyncConnect(redisState.redisServer.c_str(), redisState.redisPort))
{
    if (rc->err)
    {
        std::cerr << "Bad Redis Init for BCast" << std::endl;
        std::terminate();
    }
    redisAsyncSetConnectCallback(rc, [](const redisAsyncContext *c, int status) {
        if (status != REDIS_OK)
        {
            printf("JData Broadcaster Connection Error: %s\n", c->errstr);
            return;
        }
        printf("Connected... status: %d\n", status);
    });
    redisAsyncSetDisconnectCallback(rc, [](const redisAsyncContext *c, int status) {
        if (status != REDIS_OK)
        {
            printf("JData Broadcaster Disconnection Error: %s\n", c->errstr);
            return;
        }
        printf("Disconnected...\n");
    });
    redisLibeventAttach(rc, bCastEventLoop);
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
                rc, [](redisAsyncContext *c, void *r, void *privdata)
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
    redisAsyncDisconnect(rc);
}

void jamc::BroadcastManager::RunBroadcastMainLoop()
{
    while (remote->scheduler->toContinue)
    {
        event_base_loop(bCastEventLoop, EVLOOP_ONCE | EVLOOP_NONBLOCK);
    }   
}

void jamc::BroadcastManager::StopBroadcastMainLoop()
{
    event_base_loopexit(bCastEventLoop, NULL);
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