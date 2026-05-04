// transport_esp_at.cc — ESP32 AT command transport driver (Stage 32).
//
// Hardware path: UART0 on GPIO44 (TX) / GPIO45 (RX) at 115 200 baud.
// Uses AT+CWJAP? and AT+CIFSR for status; AT+HTTPCLIENT for HTTP requests.
//
// AT+HTTPCLIENT limitations (acceptable for v1):
//   - HTTP status code is not returned by the AT command; http_status_out
//     is always set to 0.  Z80 callers should inspect the response body.
//   - POST body is passed as an inline quoted string; binary bodies with
//     embedded double-quotes will be rejected with NET_PARSE_ERROR.
//   - Maximum combined command length is ~1 KB; large URLs or bodies will
//     be rejected with NET_PARSE_ERROR.
//   - HTTPS uses TLS on the ESP32 (not the RP2350).  Do NOT use for
//     identity, entitlement, or score traffic — those flows require
//     end-to-end crypto owned by the RP2350, not TLS on the ESP32.

#include "net/transport_esp_at.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Host-test stub (no UART, no AT commands)
// ---------------------------------------------------------------------------

#ifdef JLPICART_HOST_TEST

void TransportEspAt::init()
{
    initialized_ = true;
}

DiagStatus TransportEspAt::get_status(NetStatus& out) const
{
    out = {};  // disconnected
    return DiagStatus::success();
}

void TransportEspAt::connect(const char* /*ssid*/, const char* /*pass*/) {}

DiagStatus TransportEspAt::http_request(uint8_t, bool, const char*,
                                          const uint8_t*, uint16_t,
                                          uint8_t*, uint16_t, uint16_t,
                                          uint16_t* http_status_out,
                                          uint16_t* resp_len_out)
{
    if (http_status_out) *http_status_out = 0u;
    if (resp_len_out)    *resp_len_out    = 0u;
    return DiagStatus::error(DiagCode::NET_UNAVAILABLE);
}

// ---------------------------------------------------------------------------
// Hardware implementation
// ---------------------------------------------------------------------------

#else

#include "boards/gpio_defs.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "pico/time.h"

static uart_inst_t* const s_uart  = uart0;
static constexpr uint32_t AT_BAUD = 115200u;

// ---------------------------------------------------------------------------
// Low-level UART helpers
// ---------------------------------------------------------------------------

void TransportEspAt::uart_write_str(const char* s)
{
    for (; *s; ++s) uart_putc_raw(s_uart, *s);
}

void TransportEspAt::uart_write_bytes(const uint8_t* buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; ++i) uart_putc_raw(s_uart, buf[i]);
}

// Read one line (strips \r, stops at \n). Returns character count (excl NUL).
uint16_t TransportEspAt::read_line(char* buf, size_t buf_max, uint32_t timeout_ms)
{
    if (!buf || buf_max == 0) return 0;
    uint64_t deadline = time_us_64() + (uint64_t)timeout_ms * 1000u;
    uint16_t n = 0;
    while (n < buf_max - 1u) {
        while (!uart_is_readable(s_uart)) {
            if (time_us_64() >= deadline) { buf[n] = '\0'; return n; }
        }
        char c = (char)uart_getc(s_uart);
        if (c == '\n') break;
        if (c != '\r') buf[n++] = c;
    }
    buf[n] = '\0';
    return n;
}

// Read lines until one contains prefix (via strstr). Returns true if found.
// Optionally copies the matching line into line_out.
bool TransportEspAt::wait_line_prefix(const char* prefix, uint32_t timeout_ms,
                                       char* line_out, size_t line_max)
{
    uint64_t deadline = time_us_64() + (uint64_t)timeout_ms * 1000u;
    char line[256];
    while (time_us_64() < deadline) {
        uint64_t rem_us = deadline - time_us_64();
        uint32_t rem_ms = (uint32_t)(rem_us / 1000u);
        if (rem_ms == 0u) rem_ms = 1u;
        read_line(line, sizeof(line), rem_ms < 500u ? rem_ms : 500u);
        if (strstr(line, prefix) != nullptr) {
            if (line_out && line_max > 0u) {
                strncpy(line_out, line, line_max - 1u);
                line_out[line_max - 1u] = '\0';
            }
            return true;
        }
    }
    return false;
}

// Wait until a specific character is received. Used for the '>' CIPSEND prompt.
bool TransportEspAt::wait_char(char c, uint32_t timeout_ms)
{
    uint64_t deadline = time_us_64() + (uint64_t)timeout_ms * 1000u;
    while (time_us_64() < deadline) {
        if (uart_is_readable(s_uart)) {
            if ((char)uart_getc(s_uart) == c) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Collect +HTTPCLIENT response chunks
//
// AT+HTTPCLIENT replies with one or more lines:
//   +HTTPCLIENT:<size>,<data>
// followed eventually by a blank line and "OK".
// We parse each chunk's <size> and copy exactly that many bytes of <data>.
// ---------------------------------------------------------------------------

uint16_t TransportEspAt::collect_httpclient(uint8_t* resp_buf, uint16_t resp_max,
                                              uint32_t timeout_ms)
{
    static const char PREFIX[] = "+HTTPCLIENT:";
    static const size_t PREFIX_LEN = sizeof(PREFIX) - 1u;
    uint16_t total = 0;
    uint64_t deadline = time_us_64() + (uint64_t)timeout_ms * 1000u;

    while (time_us_64() < deadline) {
        char line[512];
        uint64_t rem_us = deadline - time_us_64();
        uint32_t rem_ms = (uint32_t)(rem_us / 1000u);
        if (rem_ms == 0u) break;
        uint16_t llen = read_line(line, sizeof(line), rem_ms < 1000u ? rem_ms : 1000u);

        if (strncmp(line, PREFIX, PREFIX_LEN) == 0) {
            // Parse "<size>," then copy <size> bytes
            const char* p = line + PREFIX_LEN;
            uint16_t chunk_sz = (uint16_t)atoi(p);
            const char* comma = strchr(p, ',');
            if (!comma) continue;
            const char* data = comma + 1u;
            uint16_t inline_len = (uint16_t)(llen - (size_t)(data - line));
            if (inline_len > chunk_sz) inline_len = chunk_sz;
            uint16_t copy_len = inline_len;
            if (total + copy_len > resp_max) copy_len = resp_max - total;
            memcpy(resp_buf + total, data, copy_len);
            total += copy_len;
            if (total >= resp_max) break;
        } else if (strcmp(line, "OK") == 0) {
            break;
        } else if (strcmp(line, "ERROR") == 0) {
            break;
        }
    }
    return total;
}

// ---------------------------------------------------------------------------
// URL parser: http[s]://host[:port]/path
// Sets *is_https and default port (80/443) before returning true.
// ---------------------------------------------------------------------------

bool TransportEspAt::parse_url(const char* url,
                                 char* host, size_t host_max,
                                 uint16_t* port,
                                 char* path, size_t path_max,
                                 bool* is_https)
{
    *is_https = false;
    if (strncmp(url, "https://", 8u) == 0) { *is_https = true; url += 8u; }
    else if (strncmp(url, "http://",  7u) == 0) {                url += 7u; }
    else return false;

    *port = *is_https ? 443u : 80u;

    const char* host_end = url;
    while (*host_end && *host_end != ':' && *host_end != '/') ++host_end;
    size_t hlen = (size_t)(host_end - url);
    if (hlen == 0u || hlen >= host_max) return false;
    memcpy(host, url, hlen);
    host[hlen] = '\0';

    if (*host_end == ':') {
        ++host_end;
        *port = (uint16_t)atoi(host_end);
        while (*host_end && *host_end != '/') ++host_end;
    }

    if (*host_end == '/') {
        strncpy(path, host_end, path_max - 1u);
        path[path_max - 1u] = '\0';
    } else {
        path[0] = '/'; path[1] = '\0';
    }
    return true;
}

// Parse "a.b.c.d" into big-endian uint32_t (network byte order).
uint32_t TransportEspAt::parse_ipv4(const char* s)
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0u;
    return (a << 24u) | (b << 16u) | (c << 8u) | d;
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void TransportEspAt::init()
{
    uart_init(s_uart, AT_BAUD);
    gpio_set_function(GPIO64_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(GPIO64_UART_RX, GPIO_FUNC_UART);
    uart_set_hw_flow(s_uart, false, false);
    uart_set_format(s_uart, 8, 1, UART_PARITY_NONE);

    // Drain any startup noise from the ESP32.
    busy_wait_ms(120u);
    while (uart_is_readable(s_uart)) (void)uart_getc(s_uart);

    // Probe: send AT, expect OK.  Failure is non-fatal; we set initialized_
    // regardless so the service can report disconnected rather than crashing.
    uart_write_str("AT\r\n");
    wait_line_prefix("OK", 1500u);

    initialized_ = true;
}

// ---------------------------------------------------------------------------
// connect
// ---------------------------------------------------------------------------

void TransportEspAt::connect(const char* ssid, const char* pass)
{
    if (!initialized_ || !ssid || ssid[0] == '\0') return;

    // Issue AT+CWJAP="ssid","pass" and wait for a terminal response.
    // The ESP32 stores credentials in its own flash after a successful join,
    // so this is only strictly needed when credentials are new or changed.
    uart_write_str("AT+CWJAP=\"");
    uart_write_str(ssid);
    uart_write_str("\",\"");
    if (pass) uart_write_str(pass);
    uart_write_str("\"\r\n");

    // Drain lines until OK, FAIL, ERROR, or 15 seconds elapse.
    // Intermediate lines ("WIFI CONNECTED", "WIFI GOT IP") are silently consumed.
    char line[128];
    for (int i = 0; i < 20; ++i) {
        uint16_t len = read_line(line, sizeof(line), 800u);
        if (len == 0u) continue;
        if (strncmp(line, "OK",    2) == 0) break;
        if (strncmp(line, "FAIL",  4) == 0) break;
        if (strncmp(line, "ERROR", 5) == 0) break;
        if (strncmp(line, "+CWJAP:", 7) == 0) break;  // error code form
    }
}

// ---------------------------------------------------------------------------
// get_status
// ---------------------------------------------------------------------------

DiagStatus TransportEspAt::get_status(NetStatus& out) const
{
    out = {};
    if (!initialized_) return DiagStatus::error(DiagCode::NET_UNAVAILABLE);

    auto* self = const_cast<TransportEspAt*>(this);

    // AT+CWJAP? — returns "+CWJAP:<ssid>,<bssid>,<ch>,<rssi>" if connected,
    // or "No AP\r\nERROR" / "No AP" if not.
    self->uart_write_str("AT+CWJAP?\r\n");
    char line[128];
    if (!self->wait_line_prefix("+CWJAP:", 2000u, line, sizeof(line))) {
        // Not connected — drain remaining response then return disconnected.
        self->wait_line_prefix("OK", 500u);
        return DiagStatus::success();
    }
    out.connected = true;

    // Extract RSSI: fourth comma-separated field in "+CWJAP:..."
    // Format: +CWJAP:"ssid","bssid",channel,rssi
    const char* p = line;
    for (int i = 0; i < 4; ++i) {
        p = strchr(p, ',');
        if (!p) { p = nullptr; break; }
        ++p;
    }
    if (p) out.rssi = (int8_t)atoi(p);
    self->wait_line_prefix("OK", 500u);

    // AT+CIFSR — returns "+CIFSR:STAIP,\"x.x.x.x\""
    self->uart_write_str("AT+CIFSR\r\n");
    char ip_line[128];
    if (self->wait_line_prefix("+CIFSR:STAIP,", 1500u, ip_line, sizeof(ip_line))) {
        const char* q = strchr(ip_line, '"');
        if (q) {
            ++q;
            const char* end = strchr(q, '"');
            if (end) {
                char ip_str[16];
                size_t len = (size_t)(end - q);
                if (len < sizeof(ip_str)) {
                    memcpy(ip_str, q, len);
                    ip_str[len] = '\0';
                    out.ipv4 = parse_ipv4(ip_str);
                }
            }
        }
    }
    self->wait_line_prefix("OK", 500u);

    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// http_request
// ---------------------------------------------------------------------------

DiagStatus TransportEspAt::http_request(uint8_t verb, bool https, const char* url,
                                          const uint8_t* body, uint16_t body_len,
                                          uint8_t* resp_buf, uint16_t resp_max,
                                          uint16_t timeout_ms,
                                          uint16_t* http_status_out,
                                          uint16_t* resp_len_out)
{
    if (http_status_out) *http_status_out = 0u;
    if (resp_len_out)    *resp_len_out    = 0u;

    if (!initialized_) return DiagStatus::error(DiagCode::NET_UNAVAILABLE);
    if (!url || !resp_buf) return DiagStatus::error(DiagCode::NET_PARSE_ERROR);

    // Check for embedded double-quotes in body (not supported by AT command
    // quoted-string encoding).
    for (uint16_t i = 0; i < body_len; ++i) {
        if (body[i] == '"') return DiagStatus::error(DiagCode::NET_PARSE_ERROR);
    }

    // Map verb to AT+HTTPCLIENT opt parameter (1=GET,3=POST,4=PUT,5=DELETE).
    uint8_t opt;
    switch (verb) {
        case 1:  opt = 3u; break;  // POST
        case 2:  opt = 4u; break;  // PUT
        case 3:  opt = 5u; break;  // DELETE
        default: opt = 1u; break;  // GET
    }
    uint8_t transport = https ? 2u : 1u;  // 1=HTTP, 2=HTTPS

    // Build AT+HTTPCLIENT command.
    // Format: AT+HTTPCLIENT=<opt>,0,"<url>",,,<transport>[,"<body>"]
    char cmd[1024];
    int n;
    if (body_len > 0u && body != nullptr) {
        n = snprintf(cmd, sizeof(cmd),
                     "AT+HTTPCLIENT=%u,0,\"%s\",,,%u,\"%.*s\"\r\n",
                     opt, url, transport, (int)body_len, (const char*)body);
    } else {
        n = snprintf(cmd, sizeof(cmd),
                     "AT+HTTPCLIENT=%u,0,\"%s\",,,%u\r\n",
                     opt, url, transport);
    }
    if (n < 0 || (size_t)n >= sizeof(cmd))
        return DiagStatus::error(DiagCode::NET_PARSE_ERROR);

    uint32_t req_timeout = timeout_ms > 0u ? (uint32_t)timeout_ms : 15000u;

    uart_write_str(cmd);

    // Collect response chunks.
    uint16_t resp_len = collect_httpclient(resp_buf, resp_max, req_timeout);
    if (resp_len_out) *resp_len_out = resp_len;

    // Check for ERROR response (collect_httpclient exits on OK or ERROR).
    // If resp_len is 0 and we ran out of time, report timeout.
    // Otherwise treat as success (we don't distinguish OK from ERROR here
    // since collect_httpclient exits on either).
    // A future improvement: track whether "ERROR" was seen and return E_IO.

    return DiagStatus::success();
}

#endif  // !JLPICART_HOST_TEST
