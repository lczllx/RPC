#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include "log_system/lcz_log.h"

// dlmuduo 网络库头文件（原 muduo，现已迁到自有库）
#include "TcpServer.hpp"//tcp服务器
#include "TcpClient.hpp"//tcp客户端
#include "Connection.hpp"//tcp连接
#include "EventLoop.hpp"//事件循环
#include "LoopThread.hpp"//事件循环线程(EventLoopThread 别名)
#include "InetAddress.hpp"//网络地址
#include <arpa/inet.h> // inet_pton
#include "Buffer.hpp"//缓冲区
#include "CallbackTypes.hpp"//回调函数
#include "CountDownLatch.hpp"//倒计时器
#include <thread>
#include <chrono>
#include "serializer.hpp"

#include "abstract.hpp"
#include "message.hpp"
#include "publicconfig.hpp"

namespace lcz_rpc
{
    // 缓冲区适配器类：将 dlmuduo Buffer 封装成 BaseBuffer 接口
    class MuduoBuffer:public BaseBuffer
    {
        public:
        using ptr = std::shared_ptr<MuduoBuffer>;
        MuduoBuffer(Buffer* buffer)
        {
            _buffer = buffer;
        }
         // 获取可读数据大小
         virtual size_t readableSize() override
         {
            return _buffer->ReadableBytes();
         }
         // 查看int32数据（不移动读指针）
         virtual int32_t peekInt32() override
         {
            //dlmuduo是网络库，从缓冲区中获取int32数据，会进行网络字节序到主机字节序的转换
            return _buffer->peekInt32();
         }
         // 跳过int32数据
         virtual void retrieveInt32() override
         {
            _buffer->retrieveInt32();
         }
         // 读取int32数据
         virtual int32_t readInt32() override
         {
            return _buffer->readInt32();
         }
         // 读取指定长度的字符串
         virtual std::string retrieveAsString(size_t len) override
         {
            return _buffer->retrieveAsString(len);
         }
        private:
        Buffer* _buffer;

    };
    // Buffer 工厂类：创建 MuduoBuffer 实例
    class BufferFactory
    {
      public:
      template<typename... ARGS>
      static BaseBuffer::ptr create(ARGS&& ...args)
      {
         return std::make_shared<MuduoBuffer>(std::forward<ARGS>(args)...);
      }
    };
    // LV 协议类：基于「长度+类型+id+body」的简单网络协议
    // 线缆格式（大端网络字节序）：
    // ┌────────────┬──────────────┬────────────┬──────────┬──────────┐
    // │ total_len  │   msgtype    │  id_len    │    id    │   data   │
    // │  (4B)      │   (4B)       │  (4B)      │ (变长)    │ (变长)    │
    // └────────────┴──────────────┴────────────┴──────────┴──────────┘
    class LVProtocol :public BaseProtocol
    {
      public:
          using ptr = std::shared_ptr<LVProtocol>;
          // 判断缓冲区中是否已有完整消息可解析
          virtual bool canProcessed(const BaseBuffer::ptr &buf) override
          {
            // 检查是否有足够的数据读取长度字段
            if(buf->readableSize() < _totalfield_len)
            {
               return false;
            }
            // 检查完整消息是否已到达（peekInt32 返回的是消息体长度）
            int32_t body_len = buf->peekInt32();
            // 长度字段来自网络，负数非法；负数隐式转 size_t 会与可读字节比较失真，这里显式拒绝
            if(body_len < 0)
            {
               return false;
            }
            if(body_len > buf->readableSize() - _totalfield_len)
            {
               return false;  // 数据不完整，继续等待
            }
            return true;
          }
          // 从缓冲区解析一条完整消息，填充 msg
          virtual bool onMessage(const BaseBuffer::ptr &buf, BaseMessage::ptr &msg) override
          {
            if(!canProcessed(buf)){return false;}
            int32_t total_len=buf->readInt32();
            MsgType msgtype=static_cast<MsgType>(buf->readInt32());
            int32_t id_len=buf->readInt32();
            // 长度字段来自网络，必须非负；用 int64 计算 data_len 防溢出，
            // 否则负数 id_len/data_len 直传 retrieveAsString 会隐式转巨大 size_t 导致越界读。
            if(total_len < 0 || id_len < 0){return false;}
            int64_t data_len=static_cast<int64_t>(total_len)
                              -static_cast<int64_t>(_msgidfield_len)
                              -static_cast<int64_t>(_msgtypefield_len)
                              -static_cast<int64_t>(id_len);
            if(data_len < 0){return false;}

            std::string id=buf->retrieveAsString(static_cast<size_t>(id_len));
            std::string data=buf->retrieveAsString(static_cast<size_t>(data_len));
            msg=MessageFactory::create(msgtype);
            if(msg.get()==nullptr){LCZ_ERROR("创建消息失败");return false;}
            bool ret=_serializer->decode(data, msg);//通过序列化器反序列化数据
            if(!ret){LCZ_ERROR("反序列化数据失败");return false;}
            msg->setId(id);
            msg->setMsgType(msgtype);
            return true;
          }
          // 将消息序列化为「长度+类型+id+body」格式的字节串
          virtual std::string serialize(const BaseMessage::ptr &msg) override
          {
            //len msgtype idlen id data
            std::string data=_serializer->encode(msg);//通过序列化器序列化数据
            if(data.empty()){LCZ_ERROR("序列化数据失败");return "";}
            std::string id=msg->rid();

            //获取消息类型和ID长度和数据长度
            int32_t msgtype = static_cast<int32_t>(msg->msgType());
            int32_t id_len = static_cast<int32_t>(id.size());
            int32_t data_len = static_cast<int32_t>(data.size());

            //计算总长度
            int32_t total_len=_msgtypefield_len+_msgidfield_len+id_len+data_len;

            //转换为网络字节序，保证跨平台移植性
            auto total_len_net = htonl(total_len);
            auto msgtype_net = htonl(msgtype);
            auto id_len_net = htonl(id_len);

            std::string output;
            output.reserve(total_len);
            output.append((char*)&total_len_net,_totalfield_len);
            output.append((char*)&msgtype_net,_msgtypefield_len);
            output.append((char*)&id_len_net,_msgidfield_len);
            output.append(id);
            output.append(data);
            return output;
          }
          public:
          // 注入序列化器，不设置则默认 ProtobufSerializer
          void setSerializer(std::shared_ptr<ISerializer> s) override { _serializer = std::move(s); }
      private:
          const size_t _totalfield_len=4;
          const size_t _msgtypefield_len=4;
          const size_t _msgidfield_len=4;
          ISerializer::ptr _serializer = std::make_shared<ProtobufSerializer>(); // 默认 protobuf
      };
      // 协议工厂类：创建 LVProtocol 实例
      class ProtocolFactory
      {
        public:
        template<typename... ARGS>
        static BaseProtocol::ptr create(ARGS&& ...args)
        {
          return std::make_shared<LVProtocol>(std::forward<ARGS>(args)...);
        }

      };
      // 连接类：BaseConnection 的 dlmuduo 实现，负责序列化和底层 send/shutdown
      class MuduoConnection :public BaseConnection
      {
         public:
         using ptr = std::shared_ptr<MuduoConnection>;
         MuduoConnection(const PtrConnection& connection,const BaseProtocol::ptr& protocol)
         {
            _connection = connection;
            _protocol = protocol;
         }
         // 将消息序列化后通过底层 Connection 发送
          virtual void send(const BaseMessage::ptr &msg)override
          {
            if (!_connection->Connected()) return;
            std::string data=_protocol->serialize(msg);
            _connection->Send(data.data(), data.size());
          }
          // 关闭底层 TCP 连接
          virtual void shutdown()override
          {
            _connection->Shutdown();
          }
          // 检查底层连接是否仍然有效
          virtual bool connected()override
          {
            return _connection->Connected();
          }
          // 检查底层连接是否仍然有效
          virtual std::string peerAddress()const override
          {
            return _connection->peerAddress().toIpPort();  // InetAddress::toIpPort() → "10.0.0.1:8889"
          }
          // 获取底层连接的 EventLoop，供 Requestor 等设置超时定时器
          EventLoop* getLoop() const
          {
            return _connection->GetLoop();
          }
         private:
         PtrConnection _connection;
         BaseProtocol::ptr _protocol;

      };
      // 连接工厂类：创建 MuduoConnection 实例
      class ConnectionFactory
      {
         public:
         template<typename... ARGS>
         static BaseConnection::ptr create(ARGS&& ...args)
         {
            return std::make_shared<MuduoConnection>(std::forward<ARGS>(args)...);
         }
      };
      // 服务器类：基于 TcpServer 的 BaseServer 实现
      class MuduoServer :public BaseServer
      {
         public:
             using ptr = std::shared_ptr<MuduoServer>;
            MuduoServer(const int port, int thread_cnt = 1)
            :_server(port)
            ,_protocol(ProtocolFactory::create()){
                if (thread_cnt > 1) _server.SetThreadCnt(thread_cnt);
            }
             // 启动服务器并进入事件循环（阻塞）
            virtual void start() override
            {
               _server.SetConnectedCallBack(std::bind(&MuduoServer::onConnected,this,std::placeholders::_1));
               _server.SetClosedCallBack(std::bind(&MuduoServer::onClosed,this,std::placeholders::_1));
               _server.SetMessageCallBack(std::bind(&MuduoServer::onMessage,this,std::placeholders::_1,std::placeholders::_2));
               _server.Start();//开始监听并进入事件循环
            }
            // 注入序列化器到协议层
            void setSerializer(std::shared_ptr<ISerializer> s) override { _protocol->setSerializer(s); }
            // 优雅退出：Stop() 唤醒事件循环使其从 Start() 返回，线程安全
            virtual void stop() override
            {
                _server.Stop();
            }
            private:
            // 连接建立时维护 _connections 映射并触发回调
            void onConnected(const PtrConnection& conn)
            {
               LCZ_DEBUG("新连接建立");
               auto muduo_conn=ConnectionFactory::create(conn,_protocol);
               {
                  std::unique_lock<std::mutex> lock(_mutex);
                  _connections[conn]=muduo_conn;
               }
               if(_cb_connection)_cb_connection(muduo_conn);
            }
            // 连接断开时从 _connections 移除并触发回调
            void onClosed(const PtrConnection& conn)
            {
                 LCZ_DEBUG("连接断开");
                 BaseConnection::ptr muduo_conn;
                 {
                     std::unique_lock<std::mutex> lock(_mutex);
                     auto it=_connections.find(conn);
                     if(it==_connections.end())
                     {
                       return;
                     }
                     muduo_conn=it->second;
                     _connections.erase(it);
                     if(_cb_close)_cb_close(muduo_conn);
                  }
            }
           // 从 Buffer 解析完整消息并调用 _cb_message 派发
           void onMessage(const PtrConnection& conn,Buffer* buf)
           {
              auto base_buf=BufferFactory::create(buf);
              while(true)
              {
                 if(_protocol->canProcessed(base_buf)==false)
                 {
                    LCZ_DEBUG("数据不完整，继续等待");
                    if(base_buf->readableSize()>_maxdatalen)
                    {
                     conn->Shutdown();
                       LCZ_ERROR("数据长度超过最大值");
                       return;
                    }
                    break;
                 }

                 BaseMessage::ptr msg;
               bool ret = _protocol->onMessage(base_buf, msg);
               if (ret == false) {
               conn->Shutdown();
               LCZ_ERROR("缓冲区中数据错误！");
               return ;
               }
               //LCZ_DEBUG("消息反序列化成功！")
               BaseConnection::ptr base_conn;
               {
               std::unique_lock<std::mutex> lock(_mutex);
               auto it = _connections.find(conn);
               if (it == _connections.end()) {
               conn->Shutdown();
               return;
               }
               base_conn = it->second;
               }
               //LCZ_DEBUG("调⽤回调函数进⾏消息处理！");
               if (_cb_message) _cb_message(base_conn, msg);
              }
           }
         private:
            const size_t _maxdatalen=1024*1024*10;   // 10MB，单消息最大长度，防止恶意或异常大包撑爆内存
            TcpServer _server;
            BaseProtocol::ptr _protocol;
            std::unordered_map<PtrConnection/**<-- 网络连接指针 */,BaseConnection::ptr/**<-- 抽象连接指针 */> _connections;
            std::mutex _mutex;
      };
      // 服务器工厂类：创建 MuduoServer 实例
      class ServerFactory
      {
         public:
         template<typename... ARGS>
         static BaseServer::ptr create(ARGS&& ...args)
         {
            return std::make_shared<MuduoServer>(std::forward<ARGS>(args)...);
         }
      };
      // 客户端类：基于 TcpClient 的 BaseClient 实现，支持同步 connect/shutdown
      class MuduoClient :public BaseClient
      {
         public:
            using ptr = std::shared_ptr<MuduoClient>;

            MuduoClient(const std::string& sip,const int sport)
            :_protocol(ProtocolFactory::create())
            ,_baceloop(_loopthread.startLoop())
            ,_downlatch(new CountDownLatch(1))
            ,_client(_baceloop,resolveHost(sip,sport),"MuduoClient"){}
          private:
            // InetAddress(sip,sport) 不支持 DNS，手工解析 hostname
            static InetAddress resolveHost(const std::string& host, uint16_t port) {
                struct in_addr v4; struct in6_addr v6;
                if (::inet_pton(AF_INET, host.c_str(), &v4) == 1 ||
                    ::inet_pton(AF_INET6, host.c_str(), &v6) == 1) {
                    return InetAddress(host, port);  // 已是 IP
                }
                InetAddress resolved;
                if (InetAddress::resolve(host, &resolved))
                    return InetAddress(resolved.toIp(), port);  // DNS 解析
                return InetAddress(port);  // 回退
            }
          public:
            // 注入序列化器到协议层
            virtual void setSerializer(std::shared_ptr<ISerializer> s) override { _protocol->setSerializer(s); }
            // 连接建立/断开时更新 _connection
            void onConnection(const PtrConnection& conn)
            {
              if(conn->Connected())
              {
                 LCZ_DEBUG("连接建立");
                 _connection = ConnectionFactory::create(conn, _protocol);
                 _downlatch->countDown();
              }
              else{
                LCZ_DEBUG("连接断开");_connection.reset();
              }
           }
           // 从 Buffer 解析消息并调用 _cb_message 派发
           void onMessage(const PtrConnection& conn,Buffer* buf)
           {
             auto bace_buf=BufferFactory::create(buf);

             while(true)
             {
                if(_protocol->canProcessed(bace_buf)==false)
                {
                   LCZ_DEBUG("数据不完整，继续等待");
                   if(bace_buf->readableSize()>_maxdatalen)
                   {
                      LCZ_ERROR("数据长度超过最大值");
                      return;
                   }
                   break;
                }
                BaseMessage::ptr msg;
                bool ret=_protocol->onMessage(bace_buf,msg);
                if(!ret){conn->Shutdown();LCZ_ERROR("处理消息失败");return;}
                if(_cb_message) _cb_message(_connection,msg);
             }
           }
           // 连接服务器并阻塞等待连接建立（支持断线重连）
           virtual void connect() override
           {
             _client.SetConnectionCallback(std::bind(&MuduoClient::onConnection,this,std::placeholders::_1));
             _client.SetMessageCallback(std::bind(&MuduoClient::onMessage,this,std::placeholders::_1,std::placeholders::_2));
              // 每次 connect 重建 latch，支持重复调用（断线重连等场景）
              _downlatch.reset(new CountDownLatch(1));
              _client.connect();
               _downlatch->wait();
               LCZ_DEBUG("连接服务器成功！");
            }
             // 断开与服务器的连接
             virtual void shutdown() override
             {
               _client.disconnect();
               std::this_thread::sleep_for(std::chrono::milliseconds(200)); // 等事件循环处理完 Channel 移除再析构
             }
           // 通过已建立的连接发送消息，失败返回 false
           virtual bool send(const BaseMessage::ptr& msg) override
           {
               if (!_connection) {LCZ_ERROR("连接对象为空"); return false;}
               if (!_connection->connected()) {LCZ_ERROR("底层连接已断开");return false;}
               _connection->send(msg);
               return true;

           }
             // 获取封装后的连接对象
             virtual BaseConnection::ptr connection()  override
             {
               if(_connection.get()==nullptr){LCZ_ERROR("连接不存在");return nullptr;}
               return _connection;
             }
             // 检查是否已连接且连接有效
             virtual bool connected()  override
             {
               return _connection && _connection->connected();
             }
        private:
           const size_t _maxdatalen=1024*1024*10;   // 10MB，单消息最大长度，防止恶意或异常大包撑爆内存
           BaseProtocol::ptr _protocol;
           EventLoopThread _loopthread;
           EventLoop* _baceloop;
           std::unique_ptr<CountDownLatch> _downlatch;
           TcpClient _client;
           BaseConnection::ptr _connection;
      };
      // 客户端工厂类：创建 MuduoClient 实例
      class ClientFactory
      {
         public:
         template<typename... ARGS>
         static BaseClient::ptr create(ARGS&& ...args)
         {
            return std::make_shared<MuduoClient>(std::forward<ARGS>(args)...);
         }
      };
}
