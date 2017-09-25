// serializeJSONTest.cpp - serializeJSON test code.
//
// Copyright (c) 2017 Timothy Zhu.
// Licensed under the MIT License. See LICENSE file for details.
//

#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <json/json.h>
#include "../common/serializeJSON.hpp"
#include "DNC-LibraryTest.hpp"

using namespace std;

class TestObj : public Serializable
{
public:
    double d;
    int i;
    unsigned int u;
    bool b;
    string str;

    TestObj()
    {
    }
    virtual ~TestObj()
    {
    }

    virtual void serialize(Json::Value& json) const
    {
        serializeJSON(json, "d", d);
        serializeJSON(json, "i", i);
        serializeJSON(json, "u", u);
        serializeJSON(json, "b", b);
        serializeJSON(json, "str", str);
    }
    virtual void deserialize(const Json::Value& json)
    {
        deserializeJSON(json, "d", d);
        deserializeJSON(json, "i", i);
        deserializeJSON(json, "u", u);
        deserializeJSON(json, "b", b);
        deserializeJSON(json, "str", str);
    }

    bool operator==(const TestObj& other) const
    {
        return (d == other.d) &&
            (i == other.i) &&
            (u == other.u) &&
            (b == other.b) &&
            (str == other.str);
    }
};

void serializeJSONTest()
{
    vector<TestObj> v1(3);
    vector<TestObj> v2;
    TestObj& obj0 = v1[0];
    obj0.d = 0.5;
    obj0.i = -100;
    obj0.u = 100;
    obj0.b = true;
    obj0.str = "123";

    TestObj& obj1 = v1[1];
    obj1.d = 0.25;
    obj1.i = -200;
    obj1.u = 200;
    obj1.b = false;
    obj1.str = "456";

    TestObj& obj2 = v1[2];
    obj2.d = 0.125;
    obj2.i = -300;
    obj2.u = 300;
    obj2.b = true;
    obj2.str = "789";

    Json::Value json;
    serializeJSON(json, "data", v1);
    deserializeJSON(json, "data", v2);

    assert(v1.size() == v2.size());
    assert(v1[0] == v2[0]);
    assert(v1[1] == v2[1]);
    assert(v1[2] == v2[2]);
    cout << "PASS serializeJSONTest" << endl;
}
