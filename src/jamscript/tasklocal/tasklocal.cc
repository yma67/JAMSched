#include "jamscript/tasklocal/tasklocal.hh"
#include "jamscript/tasktype/tasktype.hh"

std::unordered_map<JAMScript::JTLSLocation, std::any>* JAMScript::GetGlobalJTLSMap() {
    static std::unordered_map<JTLSLocation, std::any> tlm;
    return &tlm;
}