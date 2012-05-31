#pragma once

class Output
{
public:
	virtual ~Output() {}

	virtual void print(const char* message, ...) = 0;
	virtual void error(const char* message, ...) = 0;
};
