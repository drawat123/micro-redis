#include "dispatcher.hpp"
#include "store.hpp"
#include <gtest/gtest.h>
#include <string_view>
#include <vector>

class DispatcherTest : public ::testing::Test {
protected:
  Store store{0};
  TimePoint now_{}; // fixed fake "now" (steady_clock epoch)

  std::string run(const std::vector<std::string_view> &args) {
    return dispatch(args, store, now_);
  }
  std::string run_at(const std::vector<std::string_view> &args, TimePoint t) {
    return dispatch(args, store, t); // for expiry tests: any time you want
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

TEST_F(DispatcherTest, DbSizeCommand) {
  EXPECT_EQ(run({"DBSIZE"}), ":0\r\n");
  run({"SET", "k1", "v1"});
  run({"SET", "k2", "v2"});
  EXPECT_EQ(run({"DBSIZE"}), ":2\r\n");
  run({"DEL", "k1"});
  EXPECT_EQ(run({"DBSIZE"}), ":1\r\n");
}

TEST_F(DispatcherTest, ExpiryAndTTL) {
  // SET ... EX n -> +OK
  EXPECT_EQ(run({"SET", "k", "v", "EX", "100"}), "+OK\r\n");

  // TTL/EXPIRE integer replies
  EXPECT_EQ(run({"TTL", "k"}), ":100\r\n");
  EXPECT_EQ(run({"EXPIRE", "k", "50"}), ":1\r\n");
  EXPECT_EQ(run({"TTL", "k"}), ":50\r\n");

  // Invalid TTL -> -ERR
  EXPECT_EQ(run({"SET", "k2", "v", "EX", "0"}), "-ERR invalid expire time\r\n");
  EXPECT_EQ(run({"SET", "k2", "v", "EX", "-5"}),
            "-ERR invalid expire time\r\n");
  EXPECT_EQ(run({"EXPIRE", "k", "0"}), "-ERR invalid expire time\r\n");

  // Deterministic expiry via run_at
  EXPECT_EQ(run_at({"GET", "k"}, now_ + std::chrono::seconds{49}),
            "$1\r\nv\r\n"); // still alive
  EXPECT_EQ(run_at({"GET", "k"}, now_ + std::chrono::seconds{50}),
            "$-1\r\n"); // expired
}

TEST_F(DispatcherTest, StoreSpecifics) {
  // set with TTL then get before/after expiry
  store.set("k1", "v1", std::chrono::seconds{10}, now_);
  EXPECT_NE(store.get("k1", now_ + std::chrono::seconds{9}), nullptr);
  EXPECT_EQ(store.get("k1", now_ + std::chrono::seconds{10}), nullptr);

  // ttl returns -2/-1/remaining
  EXPECT_EQ(store.ttl("missing", now_), -2);
  store.set("k2", "v2", std::nullopt, now_);
  EXPECT_EQ(store.ttl("k2", now_), -1);

  // expire on existing vs missing key (returns true/false)
  EXPECT_TRUE(store.expire("k2", std::chrono::seconds{20}, now_));
  EXPECT_EQ(store.ttl("k2", now_), 20);
  EXPECT_FALSE(store.expire("missing", std::chrono::seconds{10}, now_));

  // sweep removes expired but keeps live and no-expiry keys
  store.set("live_ttl", "v", std::chrono::seconds{100}, now_);
  store.set("dead_ttl", "v", std::chrono::seconds{10}, now_);
  store.set("no_ttl", "v", std::nullopt, now_);

  TimePoint sweep_time = now_ + std::chrono::seconds{50};
  store.sweep(sweep_time);

  EXPECT_NE(store.get("live_ttl", sweep_time), nullptr);
  EXPECT_EQ(store.get("dead_ttl", sweep_time), nullptr);
  EXPECT_NE(store.get("no_ttl", sweep_time), nullptr);

  // a plain SET clears an existing TTL
  store.set("cleared_ttl", "v", std::chrono::seconds{10}, now_);
  store.set("cleared_ttl", "v2", std::nullopt, now_);
  EXPECT_EQ(store.ttl("cleared_ttl", now_), -1);

  // exists on an expired-unswept key returns false
  store.set("unswept", "v", std::chrono::seconds{5}, now_);
  EXPECT_FALSE(store.exists("unswept", now_ + std::chrono::seconds{10}));

  // del on an expired-unswept key returns false
  EXPECT_FALSE(store.del("unswept", now_ + std::chrono::seconds{10}));
}

TEST(StoreTest, LruBehavior) {
  TimePoint now{};
  Store lru_store{3}; // cap = 3

  // 1. insert past cap -> oldest evicted, size stays <= cap
  lru_store.set("k1", "v1", std::nullopt, now);
  lru_store.set("k2", "v2", std::nullopt, now);
  lru_store.set("k3", "v3", std::nullopt, now);
  lru_store.set("k4", "v4", std::nullopt, now); // Evicts k1

  EXPECT_FALSE(lru_store.exists("k1", now));
  EXPECT_TRUE(lru_store.exists("k2", now));
  EXPECT_TRUE(lru_store.exists("k3", now));
  EXPECT_TRUE(lru_store.exists("k4", now));

  // 2. GET on a key makes it survive the next eviction (recency update)
  // Current LRU order (oldest -> newest): k2, k3, k4
  lru_store.get("k2", now); // k2 is now newest. Order: k3, k4, k2
  lru_store.set("k5", "v5", std::nullopt, now); // Evicts k3

  EXPECT_FALSE(lru_store.exists("k3", now));
  EXPECT_TRUE(lru_store.exists("k2", now)); // Survived!
  EXPECT_TRUE(lru_store.exists("k4", now));
  EXPECT_TRUE(lru_store.exists("k5", now));

  // 3. re-SETing an existing key updates recency but doesn't grow size
  // Current order: k4, k2, k5
  lru_store.set("k4", "v4-new", std::nullopt,
                now); // k4 is now newest. Order: k2, k5, k4
  lru_store.set("k6", "v6", std::nullopt, now); // Evicts k2

  EXPECT_FALSE(lru_store.exists("k2", now));
  EXPECT_TRUE(lru_store.exists("k5", now));
  EXPECT_TRUE(lru_store.exists("k4", now));
  EXPECT_TRUE(lru_store.exists("k6", now));

  // 4. expiry still works alongside the cap
  // Current order: k5, k4, k6
  lru_store.set("k7", "v7", std::chrono::seconds{5}, now); // Evicts k5
  EXPECT_FALSE(lru_store.exists("k5", now));
  EXPECT_TRUE(lru_store.exists("k7", now));

  // Move time forward: k7 expires
  TimePoint future = now + std::chrono::seconds{10};
  EXPECT_FALSE(lru_store.exists("k7", future)); // Lazy expiration

  // Now capacity is effectively 2. We can add 1 without evicting k4 or k6
  lru_store.set("k8", "v8", std::nullopt, future);
  EXPECT_TRUE(lru_store.exists("k4", future)); // Did not get evicted
  EXPECT_TRUE(lru_store.exists("k6", future));
  EXPECT_TRUE(lru_store.exists("k8", future));
}
