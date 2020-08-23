#include "node/node.hpp"
#include <iostream>
#include <thread>
#include <boost/uuid/uuid_generators.hpp> 
#include <boost/uuid/uuid_io.hpp> 
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <sys/types.h>
#include <unistd.h>

JAMScript::Node::Node(int argc, char *argv[])
    :   desc("C-side command line options")
{
    desc.add_options()
        ("help,l", "Display this help message (h used for height for backward compatibility)")
        ("app,a", boost::program_options::value<std::string>(), "appName specification")
        ("num,n", boost::program_options::value<int>(), "sequence number")
        ("height,h", boost::program_options::value<int>(), "machine height")        
        ("port,p", boost::program_options::value<std::string>(), "MQTT port number");
    try {
        boost::program_options::store(boost::program_options::command_line_parser(argc, argv).options(desc).run(), vm);
        boost::program_options::notify(vm);
    } catch (const std::exception &e) {
        std::cout << e.what() << std::endl << desc << std::endl;
        std::exit(1);
    }
    if (vm.count("help")) {
        std::cout << desc;
        std::exit(0);
    }
    dieIfNoDirectory(getPort(), 60, std::string("Exiting. Port directory missing."));
    dieIfNoFile(std::string("./") + getPort() + std::string("/machType"), 60, std::string("Exiting. machine type missing."));
    devId = generateDevId(std::string("./") + getPort() + std::string("/cdevId.") + std::to_string(getSeqNum()));
    storeProcessId(std::string("./") + getPort() + std::string("/cdevProcessId.") + std::to_string(getSeqNum()));
}

std::string JAMScript::Node::getAppId() {
    if (vm.count("app")) 
        return vm["app"].as<std::string>();
    else 
        return std::string("noname-app");
}

int JAMScript::Node::getSeqNum() {
    if (vm.count("num"))
        return vm["num"].as<int>();
    else 
        return 0;
}

std::string JAMScript::Node::getHostAddr() {
    if (vm.count("port"))
        return std::string("tcp://localhost:") + vm["port"].as<std::string>();
    else 
        return std::string("tcp://localhost:1883");
}

std::string JAMScript::Node::getPort() {
    if (vm.count("port"))
        return vm["port"].as<std::string>();
    else 
        return std::string("1883");
}

std::string JAMScript::Node::getDevId() {
    return devId;
}

JAMScript::Node::~Node() {
}
    
void JAMScript::Node::dieIfNoDirectory(std::string path, int duration, std::string emsg)
{
    const boost::filesystem::path de(path);
    for (int i = 0; i < duration; i++) {
        if (boost::filesystem::exists(de) && boost::filesystem::is_directory(de))
            return;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << emsg << std::endl;
    std::exit(1);
}
        
void JAMScript::Node::dieIfNoFile(std::string fname, int duration, std::string emsg)
{
    const boost::filesystem::path de(fname);
    for (int i = 0; i < duration; i++) {
        if (boost::filesystem::exists(de) && boost::filesystem::is_regular_file(de))
            return;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << emsg << std::endl;
    std::exit(1);
}

std::string JAMScript::Node::generateDevId(std::string path)
{
    const boost::filesystem::path p(path);
    if (boost::filesystem::exists(p) && boost::filesystem::is_regular_file(p)) {
        std::string data;
        boost::filesystem::ifstream ifs{p};
        ifs >> data;
        ifs.close();
        return data;
    } else {
        auto newId = boost::uuids::random_generator()();
        auto strId = boost::uuids::to_string(newId);
        boost::filesystem::ofstream ofs{p};
        ofs << strId;
        ofs.close();
        return strId;
    }
}
        
void JAMScript::Node::storeProcessId(std::string path)
{
    const boost::filesystem::path p(path);
    int pid = getpid();
    boost::filesystem::ofstream ofs{p};
    ofs << pid;
    ofs.close();
}