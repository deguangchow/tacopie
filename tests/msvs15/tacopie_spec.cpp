///    Copyright (C) 2018 DG.C, DGCHOW, deguangchow
///        deguangchow@qq.com
///
///    \brief    tacopie unit test
///
///    \author   deguangchow
///    \version  1.0
///    \2019/01/04

#include <tacopie/network/tcp_client.hpp>
#include <tacopie/utils/error.hpp>
#include <gtest/gtest.h>
#include <thread>

void
on_new_message(tacopie::tcp_client& client, const tacopie::tcp_client::read_result& res) {
    if (res.success) {
        std::string buf(res.buffer.begin(), res.buffer.end());
        std::cout << "Client recv data: " << buf << std::endl;
    } else {
        std::cout << "Client disconnected" << std::endl;
        client.disconnect();
    }
}

TEST(TacopieClient, ValidConnectionDefinedHost) {
    tacopie::tcp_client client;
    EXPECT_FALSE(client.is_connected());
    EXPECT_NO_THROW(client.connect("127.0.0.1", 3001));
    EXPECT_TRUE(client.is_connected());
    EXPECT_NO_THROW(client.disconnect());
}

TEST(TacopieCilent, InvalidConnection) {
    tacopie::tcp_client client;
    EXPECT_FALSE(client.is_connected());
    EXPECT_THROW(client.connect("invalid url", 1234), tacopie::tacopie_error);
    EXPECT_FALSE(client.is_connected());
    EXPECT_NO_THROW(client.disconnect());
}

TEST(TacopieClient, AlreadyConnected) {
    tacopie::tcp_client client;

    EXPECT_FALSE(client.is_connected());
    EXPECT_NO_THROW(client.connect("127.0.0.1", 3001));
    EXPECT_TRUE(client.is_connected());
    EXPECT_THROW(client.connect("127.0.0.1", 3001), tacopie::tacopie_error);
    EXPECT_TRUE(client.is_connected());
    EXPECT_NO_THROW(client.disconnect());
}

TEST(TacopieClient, Disconnection) {
    tacopie::tcp_client client;
    client.connect("127.0.0.1", 3001);
    EXPECT_TRUE(client.is_connected());
    EXPECT_NO_THROW(client.disconnect());
    EXPECT_FALSE(client.is_connected());
}

TEST(TacopieClient, DisconnectionNotConnected) {
    tacopie::tcp_client client;

    EXPECT_FALSE(client.is_connected());
    EXPECT_NO_THROW(client.disconnect());
    EXPECT_FALSE(client.is_connected());
}

TEST(TacopieClient, GetHostPort) {
    tacopie::tcp_client client;
    client.connect("127.0.0.1", 3001);

    EXPECT_EQ("127.0.0.1", client.get_host());
    EXPECT_EQ(3001, client.get_port());
    EXPECT_NO_THROW(client.disconnect());
}

TEST(TacopieClient, Operator) {
    tacopie::tcp_client client1;
    tacopie::tcp_client client2;
    client1.connect("127.0.0.1", 3001);
    client2.connect("127.0.0.1", 3001);

    EXPECT_FALSE(client1 == client2);
    EXPECT_TRUE(client1 != client2);
    EXPECT_NO_THROW(client1.disconnect());
    EXPECT_NO_THROW(client2.disconnect());
}

TEST(TacopieClient, AsyncWriteRead) {
    tacopie::tcp_client client;
    client.connect("127.0.0.1", 3001);

    const std::string &buf = "123456abc";
    tacopie::tcp_client::write_request wr_req = { { std::vector<char>{ buf.begin(), buf.end()} },
        [](tacopie::tcp_client::write_result& wr) {
        std::cout << "async_write_callback(), ret=" << wr.success << ", size=" << wr.size << ", ";
        EXPECT_TRUE(wr.success);
    } };

    EXPECT_NO_THROW(client.async_write(wr_req));
    EXPECT_NO_THROW(client.async_read({ 1024, std::bind(&on_new_message, std::ref(client), std::placeholders::_1) }));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_NO_THROW(client.disconnect());
}

TEST(TacopieClient, GetSocket) {
    tacopie::tcp_client client;
    client.connect("127.0.0.1", 3001);

    EXPECT_NO_THROW(client.get_socket());
    EXPECT_NO_THROW(EXPECT_FALSE(nullptr == client.get_io_service()));
    EXPECT_NO_THROW(client.disconnect());
}


