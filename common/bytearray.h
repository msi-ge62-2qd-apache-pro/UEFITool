/* bytearray.h

Copyright (c) 2016, Nikolaj Schlej. All rights reserved.
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
*/

#ifndef __BYTEARRAY_H__
#define __BYTEARRAY_H__

#include <stdint.h>
#include <string>

class ByteArray
{
public:
    ByteArray() : d() {}
    ByteArray(const ByteArray & ba) : d(ba.d) {}
    ByteArray(const std::basic_string<char> & bs) : d(bs) {}
    ByteArray(const char* bytes, int32_t size) : d(bytes, size) {}
    ~ByteArray() {}

    bool isEmpty() const { return d.length() == 0; }
       
    char* data() { return &(d.front()); /* Feels dirty, but works*/ }
    const char* data() const { return d.c_str(); }
    const char* constData() const { return d.c_str(); }
    void clear() { d.clear(); }

    int32_t size() const { return d.size();  }
    int32_t count(char ch) const { return std::count(d.cbegin(), d.cend(), ch); }
    char at(uint32_t i) const { return d.at(i); }
    char operator[](uint32_t i) const { return d[i]; }
    char& operator[](uint32_t i) { return d[i]; }

    bool startsWith(const ByteArray & ba) const { return 0 == d.find(ba.d, 0); }
    int indexOf(const ByteArray & ba, int from = 0) const { return d.find(ba.d, from); }
    int lastIndexOf(const ByteArray & ba, int from = 0) const { return d.rfind(ba.d, from); }

    ByteArray left(int32_t len) const { return d.substr(0, len); }
    ByteArray right(int32_t len) const { return d.substr(d.size() - 1 - len, len); };
    ByteArray mid(int32_t pos, int32_t len = -1) const { return d.substr(pos, len); };

    ByteArray &operator=(const ByteArray & ba) { d = ba.d; return *this; }
    bool operator== (const ByteArray & ba) const { return d == ba.d; }
    bool operator!= (const ByteArray & ba) const { return d != ba.d; }
    inline void swap(ByteArray &other) { std::swap(d, other.d); }

private:
    std::basic_string<char> d;
};


#endif