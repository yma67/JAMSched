#ifndef JAMSCRIPT_DATA_HH
#define JAMSCRIPT_DATA_HH
#include <queue>
#include <mutex>
#include <string>
#include <future>
#include <unordered_map>
#include <condition_variable>

#include <nlohmann/json.hpp>
#include <utility>
#include <hiredis/adapters/libevent.h>

#include "concurrency/mutex.hpp"
#include "concurrency/condition_variable.hpp"

namespace jamc
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
        logKey(std::move(logKey)), timeStamp(timeStamp), encodedObject(std::move(encodedObject)) {}
    };

    class LogManager
    {
    public:
        void RunLoggerMainLoop();
        void StopLoggerMainLoop();
        void LogRaw(const std::string &nameSpace, const std::string &varName, const nlohmann::json &streamObjectRaw);
        template <typename... Args>
        void Log(const std::string &nameSpace, const std::string &varName, Args &&... eArgs)
        {
            return LogRaw(nameSpace, varName, nlohmann::json::array({std::forward<Args>(eArgs)...}));
        }
        LogManager(Remote *remote, RedisState redisState);
        ~LogManager();
    private:
        Remote *remote;
        RedisState redisState;
        redisAsyncContext *rc;
        struct event_base *loggerEventLoop;
        SpinMutex mAsyncBuffer;
        std::condition_variable_any cvAsyncBuffer;
        std::deque<LogStreamObject *> asyncBufferEncoded;
    };

    class BroadcastVariable
    {
    public:
    
        void Append(char* data);
        nlohmann::json Get();
        const std::string &GetBroadcastKey() { return bCastKey; }
        BroadcastVariable(std::string bCastKey) : bCastKey(std::move(bCastKey)), isCancelled(false) {}
        ~BroadcastVariable() { isCancelled = true; cvVarStream.notify_all(); }
    private:
        std::string bCastKey;
        bool isCancelled;
        SpinMutex mVarStream;
        ConditionVariable cvVarStream;
        std::deque<std::vector<std::uint8_t>> varStream;
    };

    class BroadcastManager
    {
        using JAMDataKeyType = std::pair<std::string, std::string>;
    public:
        void Append(std::string key, char* data);
        nlohmann::json Get(const std::string &nameSpace, const std::string &variableName);
        void RunBroadcastMainLoop();
        void StopBroadcastMainLoop();
        BroadcastManager(Remote *remote, RedisState redisState, std::vector<JAMDataKeyType> variableInfo);
        ~BroadcastManager();
    private:
        Remote *remote;
        RedisState redisState;
        redisAsyncContext *rc;
        struct event_base *bCastEventLoop;
        std::size_t nameSpaceMaxLen, varNameMaxLen;
        std::unordered_map<std::string, std::unordered_map<std::string, std::unique_ptr<BroadcastVariable>>> bCastVarStores;
    };
}
#endif