#ifndef JAMSCRIPT_NODE_COMMANDLINE_HH
#define JAMSCRIPT_NODE_COMMANDLINE_HH
#include <boost/program_options.hpp>

namespace JAMScript 
{
    struct CommandLine
    {
        int nnumber;
        int port;
        std::string appName;
        std::string tag;

        // TODO: This needs to be completed.
        // We can use BOOST program_options to read the command line input
        // Usage: program -a app_id [-t tag] [-n num] [-p port] 

        // The port value needs to be defaulted to 1883. THis way we can skip specifying it.
    }
}
#endif

