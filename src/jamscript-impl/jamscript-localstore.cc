#include "jamscript-impl/jamscript-localstore.hh"

std::unordered_map<JAMScript::JTLSLocation, std::any>* JAMScript::GetThreadLocalJTLSMap() {
    static thread_local std::unordered_map<JTLSLocation, std::any> tlm;
    return &tlm;
}