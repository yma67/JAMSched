#include "scheduler/tasklocal.hpp"
#include "core/task/task.hpp"

std::unordered_map<jamc::JTLSLocation, std::any> *jamc::GetGlobalJTLSMap()
{
    static std::unordered_map<JTLSLocation, std::any> tlm;
    return &tlm;
}