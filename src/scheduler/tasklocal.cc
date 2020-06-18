#include "scheduler/tasklocal.hpp"
#include "core/task/task.hpp"

std::unordered_map<JAMScript::JTLSLocation, std::any>* JAMScript::GetGlobalJTLSMap() {
    static thread_local std::unordered_map<JTLSLocation, std::any> tlm;
    return &tlm;
}