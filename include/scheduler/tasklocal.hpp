#ifndef JAMSCRIPT_JAMSCRIPT_LOCALSTORE_H
#define JAMSCRIPT_JAMSCRIPT_LOCALSTORE_H
#include <any>
#include <unordered_map>

#include "core/task/task.hpp"

namespace JAMScript
{

    using JTLSLocation = void **;

    extern std::unordered_map<JTLSLocation, std::any> *GetGlobalJTLSMap();

    template <typename T, typename... Args>
    T &GetByJTLSLocation(JTLSLocation location, Args &&... args)
    {
        std::unordered_map<JTLSLocation, std::any> *taskLocalPool = nullptr;
        if (ThisTask::Active())
        {
            taskLocalPool = ThisTask::Active()->GetTaskLocalStoragePool();
        }
        else
        {
            taskLocalPool = GetGlobalJTLSMap();
        }
        if (taskLocalPool->find(location) == taskLocalPool->end())
            taskLocalPool->insert({location, std::any()});
        std::any &valueLocation = taskLocalPool->at(location);
        if (!valueLocation.has_value())
        {
            return valueLocation.emplace<T>(std::forward<Args>(args)...);
        }
        return std::any_cast<T &>(valueLocation);
    }

    template <typename T>
    class JTLSRef
    {
    public:

        template <typename... Args>
        JTLSRef(JTLSLocation location, Args &&... args) : addressLocation_(location)
        {
            (void)GetByJTLSLocation<T>(addressLocation_, std::forward<Args>(args)...);
        }

        operator T const &() const { return GetByJTLSLocation<T>(addressLocation_); }
        operator T &() { return GetByJTLSLocation<T>(addressLocation_); }

    private:

        JTLSLocation addressLocation_;
        
    };

    template <typename TJTLS>
    using TaskLS = JTLSRef<TJTLS>;

    template <typename TJTLS, typename... Args>
    JTLSRef<TJTLS> __CreateTaskLS(JTLSLocation location, Args &&... args)
    {
        return JTLSRef<TJTLS>(location, std::forward<Args>(args)...);
    }

#define GetJTLSLocation()             \
    [] {                              \
        static void *dummy = nullptr; \
        return &dummy;                \
    }()

#define CreateTaskLS(tname, ...) JAMScript::__CreateTaskLS<tname>(GetJTLSLocation(), ##__VA_ARGS__)

} // namespace JAMScript
#endif