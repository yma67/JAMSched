#ifndef JAMSCRIPT_NODE_HH
#define JAMSCRIPT_NODE_HH
#include <boost/program_options.hpp>
#include <boost/uuid/uuid.hpp>            

namespace JAMScript
{
    class Node
    {
    public:
        Node(int argc, char *arg[]);
        ~Node();
        std::string getAppId();
        int getSeqNum();
        std::string getDevId();
        std::string getHostAddr();
        std::string getPort();
    private:
        boost::program_options::options_description desc;
        boost::program_options::variables_map vm;
        std::string devId;
        void dieIfNoDirectory(std::string path, int duration, std::string emsg);
        void dieIfNoFile(std::string fname, int duration, std::string emsg);
        std::string generateDevId(std::string path);
        void storeProcessId(std::string path);
    };
} // namespace JAMScript
#endif