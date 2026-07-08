#include <gtest/gtest.h>
#include "dispatcher.hpp"
#include "store.hpp"
#include <string_view>
#include <vector>

class DispatcherTest : public ::testing::Test {
protected:
    Store store;

    std::string run(const std::vector<std::string_view>& args) {
        return dispatch(args, store);
    }
};

TEST_F(DispatcherTest, PingCommand) {
    EXPECT_EQ(run({"PING"}), "+PONG\r\n");
    EXPECT_EQ(run({"ping"}), "+PONG\r\n");
    EXPECT_EQ(run({"PiNg"}), "+PONG\r\n");
}

TEST_F(DispatcherTest, SetAndGet) {
    EXPECT_EQ(run({"GET", "foo"}), "$-1\r\n"); // Missing key
    EXPECT_EQ(run({"SET", "foo", "bar"}), "+OK\r\n");
    EXPECT_EQ(run({"GET", "foo"}), "$3\r\nbar\r\n"); // Present key
}

TEST_F(DispatcherTest, DelCommand) {
    EXPECT_EQ(run({"SET", "k1", "v1"}), "+OK\r\n");
    EXPECT_EQ(run({"DEL", "k1"}), ":1\r\n"); // Returns 1
    EXPECT_EQ(run({"DEL", "k1"}), ":0\r\n"); // Returns 0 (already deleted)
}

TEST_F(DispatcherTest, ExistsCommand) {
    EXPECT_EQ(run({"EXISTS", "k2"}), ":0\r\n");
    EXPECT_EQ(run({"SET", "k2", "v2"}), "+OK\r\n");
    EXPECT_EQ(run({"EXISTS", "k2"}), ":1\r\n");
}

TEST_F(DispatcherTest, WrongArity) {
    EXPECT_TRUE(run({"PING", "extra"}).starts_with("-ERR"));
    EXPECT_TRUE(run({"SET", "k1"}).starts_with("-ERR"));
    EXPECT_TRUE(run({"SET", "k1", "v1", "v2"}).starts_with("-ERR"));
    EXPECT_TRUE(run({"GET"}).starts_with("-ERR"));
    EXPECT_TRUE(run({"DEL"}).starts_with("-ERR"));
    EXPECT_TRUE(run({"EXISTS"}).starts_with("-ERR"));
}

TEST_F(DispatcherTest, UnknownCommand) {
    EXPECT_TRUE(run({"FOOBAR", "baz"}).starts_with("-ERR"));
}

TEST_F(DispatcherTest, CaseInsensitive) {
    EXPECT_EQ(run({"sEt", "KEY", "value"}), "+OK\r\n");
    EXPECT_EQ(run({"geT", "KEY"}), "$5\r\nvalue\r\n");
    EXPECT_EQ(run({"DeL", "KEY"}), ":1\r\n");
    EXPECT_EQ(run({"eXIsTs", "KEY"}), ":0\r\n");
}
