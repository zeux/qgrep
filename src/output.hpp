// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#pragma once

class Output
{
public:
	virtual ~Output() {}

	virtual void rawprint(const char* data, size_t size) = 0;

	virtual void print(const char* message, ...) = 0;
	virtual void error(const char* message, ...) = 0;

	virtual bool isTTY() { return false; }
};
