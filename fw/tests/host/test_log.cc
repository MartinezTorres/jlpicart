// test_log.cc — unit tests for the ring-buffer logger.
#include "test_helpers.h"
#include "diag/log.h"
#include <cstring>

static void test_log_init_clears() {
    log_init();
    CHECK(log_count() == 0);
}

static void test_log_single_entry() {
    log_init();
    log_info("hello");
    CHECK(log_count() == 1);
}

static void test_log_levels() {
    log_init();
    log_debug("d");
    log_info("i");
    log_warn("w");
    log_error("e");
    CHECK(log_count() == 4);
}

static void test_log_message_content() {
    log_init();
    log_info("test_message");

    const char* got = nullptr;
    LogLevel    got_level = LogLevel::DEBUG;
    log_iterate([](const LogEntry& e, void* ctx) {
        auto* p = reinterpret_cast<const char**>(ctx);
        *p = e.msg;
    }, &got);
    // got points into the ring buffer; content should match
    CHECK(got != nullptr);
    CHECK(strcmp(got, "test_message") == 0);

    log_iterate([](const LogEntry& e, void* ctx) {
        *reinterpret_cast<LogLevel*>(ctx) = e.level;
    }, &got_level);
    CHECK(got_level == LogLevel::INFO);
}

static void test_log_sequence_numbers() {
    log_init();
    log_info("a");
    log_info("b");
    log_info("c");

    // Sequence numbers are stored in arr[0..2], arr[3] is a counter.
    uint32_t seqs[4] = {0, 0, 0, 0};
    log_iterate([](const LogEntry& e, void* ctx) {
        auto* arr = reinterpret_cast<uint32_t*>(ctx);
        arr[arr[3]++] = e.seq;
    }, seqs);
    CHECK(log_count() == 3);
    CHECK(seqs[3] == 3);
    // Sequence numbers are monotonically increasing.
    CHECK(seqs[0] < seqs[1]);
    CHECK(seqs[1] < seqs[2]);
}

static void test_log_iterate_count() {
    log_init();
    for (int i = 0; i < 10; i++) log_info("x");
    size_t visited = 0;
    log_iterate([](const LogEntry&, void* ctx) {
        (*reinterpret_cast<size_t*>(ctx))++;
    }, &visited);
    CHECK(visited == 10);
}

static void test_log_ring_wraps() {
    log_init();
    // Fill ring past capacity; count must stay at LOG_RING_ENTRIES.
    for (size_t i = 0; i < LOG_RING_ENTRIES + 5; i++) {
        log_info("x");
    }
    CHECK(log_count() == LOG_RING_ENTRIES);
}

static void test_log_truncation() {
    log_init();
    // Write a message longer than LOG_MSG_MAX; must not overflow.
    char long_msg[LOG_MSG_MAX + 64];
    memset(long_msg, 'A', sizeof(long_msg) - 1);
    long_msg[sizeof(long_msg) - 1] = '\0';
    log_info(long_msg);
    CHECK(log_count() == 1);

    size_t msg_len = 0;
    log_iterate([](const LogEntry& e, void* ctx) {
        *reinterpret_cast<size_t*>(ctx) = strlen(e.msg);
    }, &msg_len);
    CHECK(msg_len <= LOG_MSG_MAX);
}

int main() {
    test_log_init_clears();
    test_log_single_entry();
    test_log_levels();
    test_log_message_content();
    test_log_sequence_numbers();
    test_log_iterate_count();
    test_log_ring_wraps();
    test_log_truncation();
    return test_summary();
}
