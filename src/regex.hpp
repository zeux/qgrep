#ifndef REGEX_HPP
#define REGEX_HPP

class Regex
{
public:
    virtual ~Regex() {}

    virtual const char* rangePrepare(const char* data, size_t size) = 0;
    virtual const char* rangeSearch(const char* data, size_t size) = 0;
    virtual void rangeFinalize(const char* data) = 0;

    virtual const char* search(const char* data, size_t size) = 0;
};

Regex* createRegex(const char* pattern, unsigned int options);

#endif
