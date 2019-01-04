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


#if 0
TEST(TacopieClient, ValidConnectionDefinedHost) {
    tacopie::tcp_client client;
    EXPECT_FALSE(client.is_connected());
    EXPECT_NO_THROW(client.connect("127.0.0.1", 3001));
    EXPECT_TRUE(client.is_connected());
}

TEST(TacopieCilent, InvalidConnection) {
    tacopie::tcp_client client;
    EXPECT_FALSE(client.is_connected());
    EXPECT_THROW(client.connect("invalid url", 1234), tacopie::tacopie_error);
    EXPECT_FALSE(client.is_connected());
}

TEST(TacopieClient, AlreadyConnected) {
    tacopie::tcp_client client;

    EXPECT_FALSE(client.is_connected());
    EXPECT_NO_THROW(client.connect("127.0.0.1", 3001));
    EXPECT_TRUE(client.is_connected());
    EXPECT_THROW(client.connect("127.0.0.1", 3001), tacopie::tacopie_error);
    EXPECT_TRUE(client.is_connected());
}

TEST(TacopieClient, Disconnection) {
    tacopie::tcp_client client;
    client.connect("127.0.0.1", 3001);
    EXPECT_TRUE(client.is_connected());
    client.disconnect();
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
}

TEST(TacopieClient, Operator) {
    tacopie::tcp_client client1;
    tacopie::tcp_client client2;
    client1.connect("127.0.0.1", 3001);
    client2.connect("127.0.0.1", 3001);

    EXPECT_FALSE(client1 == client2);
    EXPECT_TRUE(client1 != client2);
}

#endif

void
on_new_message(tacopie::tcp_client& client, const tacopie::tcp_client::read_result& res) {
    if (res.success) {
        std::cout << "Client recv data" << std::endl;
        EXPECT_NO_THROW(client.async_write({ res.buffer, nullptr }));
        EXPECT_NO_THROW(client.async_read({
            1024, std::bind(&on_new_message, std::ref(client), std::placeholders::_1) }));
    }
    else {
        std::cout << "Client disconnected" << std::endl;
        client.disconnect();
    }
}

TEST(TacopieClient, AsyncRead) {
    tacopie::tcp_client client;
    client.connect("127.0.0.1", 3001);
    EXPECT_NO_THROW(client.async_read({ 1024, std::bind(&on_new_message, std::ref(client), std::placeholders::_1) }));
}

TEST(TacopieClient, AsyncWrite) {
    tacopie::tcp_client client;
    client.connect("127.0.0.1", 3001);
    tacopie::tcp_client::write_request wr_q{ { '1','2','3' } ,
        [](tacopie::tcp_client::write_result& wr) {
        EXPECT_TRUE(wr.success);
        EXPECT_FALSE(wr.success);
    } };

    client.async_write(wr_q);
    client.async_write(wr_q);
    client.async_write(wr_q);

#if 0
    EXPECT_NO_THROW(client.async_write({ { '1', '2' }, [](tacopie::tcp_client::write_result& wr) {
        EXPECT_TRUE(wr.success); } }));
#endif
}