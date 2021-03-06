#include <functional>
#include <time.h>
#include <stdio.h>
#include <thread>
#include <iostream>
#include <assert.h>
#include <chrono>
#include <vector>

#include "packet.h"
#include "systemlib.h"
#include "socketlibfunction.h"

#include "eventloop.h"
#include "datasocket.h"
#include "TCPServer.h"
#include "msgqueue.h"

enum NetMsgType
{
    NMT_ENTER,      /*链接进入*/
    NMT_CLOSE,      /*链接断开*/
    NMT_RECV_DATA,  /*收到消息*/
};

struct NetMsg
{
    NetMsg(NetMsgType t, int64_t id) : mType(t), mID(id)
    {
    }

    void        setData(const char* data, int len)
    {
        mData = std::string(data, len);
    }

    NetMsgType  mType;
    int64_t     mID;
    std::string      mData;
};

std::mutex                      gTatisticsCppMutex;

void    lockStatistics()
{
    gTatisticsCppMutex.lock();
}

void    unLockStatistics()
{
    gTatisticsCppMutex.unlock();
}

int main()
{
    double total_recv_len = 0;
    double  packet_num = 0;

    int thread_num;

    std::cout << "enter tcp server eventloop thread num:";
    std::cin >> thread_num;

    int port_num;
    std::cout << "enter port:";
    std::cin >> port_num;

    ox_socket_init();
    
    int total_client_num = 0;

    /*  用于网络IO线程发送消息给逻辑线程的消息队列,当网络线程的回调函数push消息后，需要wakeup主线程 */
    /*  当然，TcpServer的各个回调函数中可以自己处理消息，而不必发送到msgList队列    */
    MsgQueue<NetMsg*>  msgList;
    EventLoop       mainLoop;

    TcpServer t;
    t.startListen(port_num, nullptr, nullptr);
    t.startWorkerThread(thread_num, [&](EventLoop& l){
        /*每帧回调函数里强制同步rwlist*/
        if (true)
        {
            lockStatistics();
            msgList.ForceSyncWrite();
            unLockStatistics();

            if (msgList.SharedListSize() > 0)
            {
                mainLoop.wakeup();
            }
        }
    });

    t.setEnterHandle([&](int64_t id, std::string ip){
        if (true)
        {
            NetMsg* msg = new NetMsg(NMT_ENTER, id);
            lockStatistics();
            msgList.Push(msg);
            unLockStatistics();

            mainLoop.wakeup();
        }
        else
        {
            lockStatistics();
            total_client_num++;
            unLockStatistics();
        }
    });

    t.setDisconnectHandle([&](int64_t id){
        if (true)
        {
            NetMsg* msg = new NetMsg(NMT_CLOSE, id);
            lockStatistics();
            msgList.Push(msg);
            unLockStatistics();

            mainLoop.wakeup();
        }
        else
        {
            lockStatistics();
            total_client_num--;
            unLockStatistics();
        }
    });

    t.setMsgHandle([&](int64_t id, const char* buffer, int len){
        const char* parse_str = buffer;
        int total_proc_len = 0;
        int left_len = len;

        while (true)
        {
            bool flag = false;
            if (left_len >= sizeof(sizeof(uint16_t) + sizeof(uint16_t)))
            {
                ReadPacket rp(parse_str, left_len);
                uint16_t packet_len = rp.readINT16();
                if (left_len >= packet_len && packet_len >= (sizeof(uint16_t) + sizeof(uint16_t)))
                {
                    if (true)
                    {
                        NetMsg* msg = new NetMsg(NMT_RECV_DATA, id);
                        msg->setData(parse_str, packet_len);
                        lockStatistics();
                        msgList.Push(msg);
                        unLockStatistics();

                        mainLoop.wakeup();
                    }
                    else
                    {
                        t.send(id, DataSocket::makePacket(parse_str, packet_len));
                        lockStatistics();
                        total_recv_len += packet_len;
                        packet_num++;
                        unLockStatistics();
                    }

                    total_proc_len += packet_len;
                    parse_str += packet_len;
                    left_len -= packet_len;
                    flag = true;
                }
            }

            if (!flag)
            {
                break;
            }
        }

        return total_proc_len;
    });

    /*  主线程处理msgList消息队列    */
    int64_t lasttime = ox_getnowtime();
    int total_count = 0;

    std::vector<int64_t> sessions;
    mainLoop.restoreThreadID();
    while (true)
    {
        mainLoop.loop(10);

        msgList.SyncRead(0);
        NetMsg* msg = nullptr;
        while (msgList.ReadListSize() > 0)
        {
            bool ret = msgList.PopFront(&msg);
            if (ret)
            {
                if (msg->mType == NMT_ENTER)
                {
                    printf("client %lld enter \n", msg->mID);
                    total_client_num++;
                    sessions.push_back(msg->mID);
                }
                else if (msg->mType == NMT_CLOSE)
                {
                    printf("client %lld close \n", msg->mID);
                    total_client_num--;
                }
                else if (msg->mType == NMT_RECV_DATA)
                {
                    if (true)
                    {
                        DataSocket::PACKET_PTR packet = DataSocket::makePacket(msg->mData.c_str(), msg->mData.size());
                        for (int i = 0; i < sessions.size(); ++i)
                        {
                            t.send(sessions[i], packet);
                            total_recv_len += msg->mData.size();
                            packet_num++;
                        }
                    }
                    else
                    {
                        DataSocket::PACKET_PTR packet = DataSocket::makePacket(msg->mData.c_str(), msg->mData.size());
                        t.send(msg->mID, packet);
                        total_recv_len += msg->mData.size();
                        packet_num++;
                    }
                }
                else
                {
                    assert(false);
                }

                delete msg;
                msg = nullptr;
            }
            else
            {
                break;
            }
        }
        int64_t now = ox_getnowtime();
        if ((now - lasttime) >= 1000)
        {
            std::cout << "recv by clientnum:" << total_client_num << " of :" << (total_recv_len / 1024) / 1024 << " M / s, " << "packet num : " << packet_num << std::endl;
            lasttime = now;
            total_recv_len = 0;
            packet_num = 0;
        }
    }

    t.closeService();
}