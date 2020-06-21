#ifndef JAMSCRIPT_EXCEPTION_HH
#define JAMSCRIPT_EXCEPTION_HH
#include <string>
#include <exception>

namespace JAMScript
{

    class InvalidArgumentException : public std::exception
    {
    private:
        std::string message_;

    public:
        explicit InvalidArgumentException(const std::string &message) : message_(message){};
        virtual const char *what() const throw() { return message_.c_str(); }
    };

} // namespace JAMScript
#endif