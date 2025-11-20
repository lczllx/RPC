#include "src/client/rpc_client.hpp"
#include <thread>
#include <iostream>

int main(int argc, char *argv[])
{
    std::string role = (argc > 1) ? argv[1] : "normal";
    int priority = (role == "vip") ? 5 : 1;
    std::vector<std::string> tags =
        (role == "vip") ? std::vector<std::string>{"vip"}
                        : std::vector<std::string>{"normal"};

    auto topic_client = std::make_shared<lcz_rpc::client::TopicClient>("127.0.0.1", 7070);
    if (!topic_client->createTopic("order"))
    {
        WLOG("topic order 已存在或创建失败，尝试直接订阅");
    }
    auto cb = [role](const std::string &topic, const std::string &msg) {
        std::cout << "[" << role << "] recv: " << msg << std::endl;
    };

    topic_client->subscribeTopic("order", cb, priority, tags);
    std::this_thread::sleep_for(std::chrono::seconds(60));
    topic_client->cancelTopic("order");
    topic_client->shutdown();
    return 0;
}