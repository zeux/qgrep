#pragma once

class Output
{
public:
	virtual ~Output() {}

	virtual void rawprint(const char* data, size_t size) = 0;

	virtual void print(const char* message, ...) = 0;
	virtual void error(const char* message, ...) = 0;

	virtual bool supportsEscapeCodes() { return false; }
};
