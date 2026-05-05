#pragma once
#include <cstddef>
#include <cstdint>

// log.h — bounded, non-blocking ring-buffer logger.
//
// Safe to call from any non-bus-loop context (Core 1 and host tests).
// Never blocks; if the ring is full, oldest entries are overwritten.

enum class LogLevel : uint8_t { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

// Compile-time ring parameters.
static constexpr size_t LOG_RING_ENTRIES = 64;
static constexpr size_t LOG_MSG_MAX      = 120;  // bytes per message, excl. null

struct LogEntry {
    LogLevel level;
    uint32_t seq;               // monotonic sequence number
    char     msg[LOG_MSG_MAX + 1];
};

void log_init();

// Write a message. Truncated silently if longer than LOG_MSG_MAX.
void log_write(LogLevel level, const char* msg);

// Convenience wrappers.
void log_debug(const char* msg);
void log_info (const char* msg);
void log_warn (const char* msg);
void log_error(const char* msg);

// Flush all pending entries to UART (firmware) or stdout (host tests).
// Call only from non-bus-loop contexts.
void log_flush_uart();

// Iterate entries in order (oldest first). Calls cb for each entry.
// Returns number of entries visited.
size_t log_iterate(void (*cb)(const LogEntry&, void* ctx), void* ctx);

// Number of entries currently in the ring.
size_t log_count();
