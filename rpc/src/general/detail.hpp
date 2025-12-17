#pragma once
#include<cstdio>
#include<time.h>

#include <iostream>
#include <sstream>
#include <string>
#include <memory>
#include <jsoncpp/json/json.h>
#include <chrono>
#include <random>
#include <atomic>
#include <iomanip>
#include "publicconfig.hpp"

#define LDBG 0
#define LINF 1
#define LWARN 2
#define LERR 3

#define LDEFAULT LINF          // 默认开启所有等级的日志
#define LOG(level,format,...){\
    if(level>=LERR)/*只打印错误日志*/{\
        time_t t=time(NULL);\
        struct tm *lt=localtime(&t);\
        char time_tmp[32]={0};\
        strftime(time_tmp,31,"%m-%d %T",lt);\
        fprintf(stdout,"[%s][%s:%d] " format"\n",time_tmp,__FILE__,__LINE__,##__VA_ARGS__);\
    }\
}
#define DLOG(format,...)LOG(LDBG,format,##__VA_ARGS__);
#define ILOG(format,...)LOG(LINF,format,##__VA_ARGS__);
#define WLOG(format,...)LOG(LWARN,format,##__VA_ARGS__);
#define ELOG(format,...)LOG(LERR,format,##__VA_ARGS__);

// Jsoncpp 的薄封装，便于序列化/反序列化
class JSON{
    public:
    //json对象->字符串 data-要序列化的jason对象 output序列化后的字符串
    static bool serialize(const Json::Value &data,std::string &output)
    {
        Json::StreamWriterBuilder swb;
        std::unique_ptr<Json::StreamWriter> sw(swb.newStreamWriter());
        std::stringstream ss;
        int ret=sw->write(data,&ss);
        if (ret != 0) 
        {
            ELOG("Serialize failed!");
            return false;
        }
        output=ss.str();
        return true;

    }
    //字符串->json对象 data-反序列化后的jason对象 input需要反序列化的字符串
    static bool deserialize(const std::string &input, Json::Value &data)
    {
        Json::CharReaderBuilder crb;
        std::string errs;
        std::unique_ptr<Json::CharReader> cr(crb.newCharReader());
        bool ret=cr->parse(input.c_str(),input.c_str()+input.size(),&data,&errs);
        if (!ret) 
        {
            ELOG("DeSerialize failed!,%s",errs.c_str());
            return false;
        }
        return true;

    }
};
// 简单的 uuid 生成工具：前半随机数，后半自增序号
std::string uuid() {
    std::stringstream ss;
    //1. 构造⼀个机器随机数对象
    std::random_device rd;
    //2. 以机器随机数为种⼦构造伪随机数对象
    std::mt19937 generator (rd());
    //3. 构造限定数据范围的对象
    std::uniform_int_distribution<int> distribution(0, 255);
    //4. ⽣成8个随机数，按照特定格式组织成为16进制数字字符的字符串
    for (int i = 0; i < 8; i++) {
        if (i == 4 || i == 6) ss << "-";
        ss << std::setw(2) << std::setfill('0') <<std::hex <<
        distribution(generator);
    }
    ss << "-";
    //5. 定义⼀个8字节序号，逐字节组织成为16进制数字字符的字符串
    static std::atomic<size_t> seq(1); // 00 00 00 00 00 00 00 01
    size_t cur = seq.fetch_add(1);
    for (int i = 7; i >= 0; i--) {
        if (i == 5) ss << "-";
        ss << std::setw(2) << std::setfill('0') << std::hex << ((cur >> (i*8))&0xFF);
    }
    return ss.str();
}
