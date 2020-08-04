#ifndef JAMSCRIPT_DATA_HH
#define JAMSCRIPT_DATA_HH
#include <string>
#include <queue>
#include <future>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <hiredis/adapters/libevent.h>

#include "remote/remote.hpp"
#include "concurrency/mutex.hpp"
#include "concurrency/condition_variable.hpp"

namespace JAMScript
{

    struct RedisState
    {
        std::string redisServer;
        int redisPort;
    };

    class LogManager
    {

    };

    class BroadcastVariable
    {
    public:
    
        void Insert(char* data)
        {
            std::lock_guard lk(mVarStream);
            varStream.push_back({ data, data + std::strlen(data) });
            cvVarStream.notify_one();
        }

        template<typename T>
        T Get() 
        {
            std::lock_guard lk(mVarStream);
            while (varStream.size() < 1) cvVarStream.wait(lk);
            auto streamObjectRaw = std::move(varStream.front());
            varStream.pop_front();
            return nlohmann::json::from_cbor(streamObjectRaw).get<T>();
        }

    private:
        Mutex mVarStream;
        ConditionVariable cvVarStream;
        std::deque<std::vector<char>> varStream;
    };

    class BroadcastManager
    {
        using JAMDataKeyType = std::pair<std::string, std::string>;
    public:
        void Insert(std::string key, char* data);
        void operator()(std::promise<void> prNotifier);
        BroadcastManager(Remote *remote, RedisState redisState, std::vector<JAMDataKeyType> variableInfo);
        ~BroadcastManager();
    private:
        Remote *remote;
        RedisState redisState;
        struct event_base *bCastEventLoop;
        bool isBCastEventLoopDispatched;
        std::size_t nameSpaceMaxLen, varNameMaxLen;
        std::unordered_map<std::string, std::unordered_map<std::string, std::unique_ptr<BroadcastVariable>>> bCastVarStores;
    };
}
#endif