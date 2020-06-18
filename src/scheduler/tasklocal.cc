#include "scheduler/tasklocal.h"
#include "core/task/task.h"

std::unordered_map<JAMScript::JTLSLocation, std::any>* JAMScript::GetGlobalJTLSMap() {
    static thread_local std::unordered_map<JTLSLocation, std::any> tlm;
    return &tlm;
}