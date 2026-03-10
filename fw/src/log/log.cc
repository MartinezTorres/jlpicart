#include "log.h"
#include <cstring>
#include <cstdio>

#ifndef JLPICART_HOST_TEST
// On firmware, use pico_stdio for UART output if enabled.
#include "pico/stdlib.h"
#endif

// ---------------------------------------------------------------------------
// Ring buffer state
// ---------------------------------------------------------------------------

static LogEntry s_ring[LOG_RING_ENTRIES];
static size_t   s_head  = 0;   // index of next slot to write
static size_t   s_count = 0;   // number of valid entries
static uint32_t s_seq   = 0;   // monotonic counter
static uint32_t s_flush_cursor = 0; // next seq to flush (compared against e.seq)

void log_init() {
    s_head         = 0;
    s_count        = 0;
    s_seq          = 0;
    s_flush_cursor = 0;
}

void log_write(LogLevel level, const char* msg) {
    LogEntry& e = s_ring[s_head];
    e.level = level;
    e.seq   = s_seq++;
    size_t len = 0;
    if (msg) {
        len = strlen(msg);
        if (len > LOG_MSG_MAX) len = LOG_MSG_MAX;
        memcpy(e.msg, msg, len);
    }
    e.msg[len] = '\0';

    s_head = (s_head + 1) % LOG_RING_ENTRIES;
    if (s_count < LOG_RING_ENTRIES) s_count++;
}

void log_debug(const char* msg) { log_write(LogLevel::DEBUG, msg); }
void log_info (const char* msg) { log_write(LogLevel::INFO,  msg); }
void log_warn (const char* msg) { log_write(LogLevel::WARN,  msg); }
void log_error(const char* msg) { log_write(LogLevel::ERROR, msg); }

static const char* level_str(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG: return "DBG";
        case LogLevel::INFO:  return "INF";
        case LogLevel::WARN:  return "WRN";
        case LogLevel::ERROR: return "ERR";
    }
    return "???";
}

void log_flush_uart() {
    // Walk from s_flush_cursor to current head, print each entry.
    // The ring may have wrapped; we print at most LOG_RING_ENTRIES entries.
    size_t start = (s_count == LOG_RING_ENTRIES)
                 ? s_head   // ring is full, oldest entry is at s_head
                 : 0;

    for (size_t i = 0; i < s_count; i++) {
        const LogEntry& e = s_ring[(start + i) % LOG_RING_ENTRIES];
        if (e.seq < s_flush_cursor) continue;
        printf("[%s] %s\n", level_str(e.level), e.msg);
    }
    s_flush_cursor = s_seq;
}

size_t log_iterate(void (*cb)(const LogEntry&, void*), void* ctx) {
    size_t start = (s_count == LOG_RING_ENTRIES) ? s_head : 0;
    for (size_t i = 0; i < s_count; i++) {
        cb(s_ring[(start + i) % LOG_RING_ENTRIES], ctx);
    }
    return s_count;
}

size_t log_count() { return s_count; }
