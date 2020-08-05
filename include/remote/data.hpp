#ifndef JAMSCRIPT_DATA_HH
#define JAMSCRIPT_DATA_HH
#include <queue>
#include <mutex>
#include <string>
#include <future>
#include <unordered_map>
#include <condition_variable>

#include <nlohmann/json.hpp>
#include <hiredis/adapters/libevent.h>

#include "concurrency/mutex.hpp"
#include "concurrency/condition_variable.hpp"

namespace JAMScript
{

    class Remote;

    struct RedisState
    {
        std::string redisServer;
        int redisPort;
    };

    struct LogStreamObject
    {
        std::string logKey;
        unsigned long long timeStamp;
        std::vector<std::uint8_t> encodedObject;
        LogStreamObject(std::string logKey, unsigned long long timeStamp, std::vector<std::uint8_t> encodedObject) : 
        logKey(std::move(logKey)), timeStamp(timeStamp), encodedObject(encodedObject) {}
    };

    class LogManager
    {
    public:
        void RunLoggerMainLoop();
        void Log(std::string nameSpace, std::string varName, nlohmann::json streamObjectRaw);
        template <typename... Args>
        void Log(std::string nameSpace, std::string varName, Args &&... eArgs)
        {
            return Log(std::move(nameSpace), std::move(varName), nlohmann::json::array({std::forward<Args>(eArgs)...}));
        }
        LogManager(Remote *remote, RedisState redisState);
        ~LogManager();
    private:
        Remote *remote;
        RedisState redisState;
        redisAsyncContext *redisContext;
        struct event_base *loggerEventLoop;
        std::mutex mAsyncBuffer;
        std::condition_variable cvAsyncBuffer;
        std::deque<LogStreamObject *> asyncBufferEncoded;
    };

    class BroadcastVariable
    {
    public:
    
        void Append(char* data);
        nlohmann::json Get();
        const std::string &GetBroadcastKey() { return bCastKey; }
        BroadcastVariable(std::string bCastKey) : bCastKey(bCastKey), isCancelled(false) {}
        ~BroadcastVariable() { isCancelled = true; cvVarStream.notify_all(); }
    private:
        std::string bCastKey;
        bool isCancelled;
        Mutex mVarStream;
        ConditionVariable cvVarStream;
        std::deque<std::vector<std::uint8_t>> varStream;
    };

    class BroadcastManager
    {
        using JAMDataKeyType = std::pair<std::string, std::string>;
    public:
        void Append(std::string key, char* data);
        void RunBroadcastMainLoop();
        void StopBroadcastMainLoop();
        BroadcastManager(Remote *remote, RedisState redisState, std::vector<JAMDataKeyType> variableInfo);
        ~BroadcastManager();
    private:
        Remote *remote;
        RedisState redisState;
        redisAsyncContext *redisContext;
        struct event_base *bCastEventLoop;
        std::size_t nameSpaceMaxLen, varNameMaxLen;
        std::unordered_map<std::string, std::unordered_map<std::string, std::unique_ptr<BroadcastVariable>>> bCastVarStores;
    };
}
#endif