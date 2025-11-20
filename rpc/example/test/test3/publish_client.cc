#include "src/client/rpc_client.hpp"
#include <iostream>

int main(int argc, char *argv[])
{
    std::string mode = (argc > 1) ? argv[1] : "broadcast";

    auto topic_client = std::make_shared<lcz_rpc::client::TopicClient>("127.0.0.1", 7070);
    if (!topic_client->createTopic("order"))
        WLOG("topic order 已存在或创建失败，继续发布");

    lcz_rpc::TopicForwardStrategy strategy = lcz_rpc::TopicForwardStrategy::BROADCAST;
    int fanout = 0;
    std::string shard;
    int priority = 0;
    std::vector<std::string> tags;
    int redundant = 0;
    if (mode == "rr")
        strategy = lcz_rpc::TopicForwardStrategy::ROUND_ROBIN;
    else if (mode == "fanout")
    {
        strategy = lcz_rpc::TopicForwardStrategy::FANOUT;
        fanout = 1;
    }
    else if (mode == "hash")
    {
        strategy = lcz_rpc::TopicForwardStrategy::SOURCE_HASH;
        shard = "user123";
    }
    else if (mode == "priority")
    {
        strategy = lcz_rpc::TopicForwardStrategy::PRIORITY;
        priority = 2;
        tags = {"vip"};
    }
    else if (mode == "redundant")
    {
        strategy = lcz_rpc::TopicForwardStrategy::REDUNDANT;
        redundant = 2;
    }
    for (int i = 0; i < 5; ++i)
    {
        if (!topic_client->publishTopic("order",
                                        "msg-" + std::to_string(i),
                                        strategy,
                                        fanout,
                                        shard,
                                        priority,
                                        tags,
                                        redundant))
        {
            ELOG("第 %d 次发布失败", i);
        }
    }

    topic_client->shutdown();
    return 0;
}