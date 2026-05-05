#pragma once
// transport_esp_at.h — ESP32 AT command transport driver.
//
// Owns UART0 (GPIO64_UART_TX=44, GPIO64_UART_RX=45) and exposes WiFi status
// and a blocking HTTP client to the firmware.
//
// Hardware note: EN and BOOT pins are NOT connected on the JLPiCart reference
// board. The AT firmware must be pre-flashed externally; software reset via
// AT+RST is the only control available.
//
// Security note: the ESP32 is an UNTRUSTED transport. All sensitive traffic
// MUST be end-to-end protected by the RP2350 above this layer.
//
// In host tests (JLPICART_HOST_TEST), all methods return stub/offline results.

#include "diag/diag.h"
#include "msx/api/api_types.h"
#include <cstddef>
#include <cstdint>

class TransportEspAt {
public:
    struct NetStatus {
        bool     connected;  // true if WiFi is associated and has an IP
        int8_t   rssi;       // signal strength in dBm; 0 if not connected
        uint32_t ipv4;       // station IP, big-endian network byte order; 0 if not connected
    };

    // Initialise UART0 at 115200 baud and drain startup noise.
    // Safe to call more than once (reinitialises).
    void init();
    bool initialized() const { return initialized_; }

    // Issue AT+CWJAP to connect to a WiFi network using stored credentials.
    // Blocks until the ESP32 reports association or failure (up to 15 s).
    // If ssid is empty or the transport is not initialised, this is a no-op.
    // The ESP32 stores credentials in its own flash after a successful join,
    // so subsequent power cycles auto-connect without needing this call again.
    void connect(const char* ssid, const char* pass);

    // Query WiFi connection state, RSSI, and station IP.
    // Always succeeds; NetStatus.connected is false if the ESP32 is not
    // associated or does not respond.
    DiagStatus get_status(NetStatus& out) const;

    // Synchronous HTTP request (blocking until response or timeout).
    //
    //   verb:             0=GET, 1=POST, 2=PUT, 3=DELETE
    //   https:            use HTTPS (TLS terminates on ESP32 — see security note above)
    //   url:              null-terminated URL (http[s]://host[:port]/path)
    //   body/body_len:    request body for POST/PUT; may be nullptr when body_len==0
    //   resp_buf:         output buffer for response body
    //   resp_max:         capacity of resp_buf in bytes
    //   timeout_ms:       per-request timeout (0 = 15 000 ms default)
    //   http_status_out:  receives HTTP status code; 0 if not determinable
    //   resp_len_out:     receives bytes written to resp_buf
    //
    // Returns NET_UNAVAILABLE if not initialised or WiFi not connected.
    // Returns NET_TIMEOUT if the AT command does not complete within timeout_ms.
    // Returns NET_PARSE_ERROR if the URL is malformed.
    DiagStatus http_request(uint8_t        verb,
                             bool           https,
                             const char*    url,
                             const uint8_t* body,
                             uint16_t       body_len,
                             uint8_t*       resp_buf,
                             uint16_t       resp_max,
                             uint16_t       timeout_ms,
                             uint16_t*      http_status_out,
                             uint16_t*      resp_len_out);

private:
    bool initialized_ = false;

#ifndef JLPICART_HOST_TEST
    void     uart_write_str(const char* s);
    void     uart_write_bytes(const uint8_t* buf, uint16_t len);
    uint16_t read_line(char* buf, size_t buf_max, uint32_t timeout_ms);
    bool     wait_line_prefix(const char* prefix, uint32_t timeout_ms,
                               char* line_out = nullptr, size_t line_max = 0);
    bool     wait_char(char c, uint32_t timeout_ms);
    uint16_t collect_httpclient(uint8_t* resp_buf, uint16_t resp_max,
                                 uint32_t timeout_ms);
    static bool     parse_url(const char* url,
                               char* host, size_t host_max,
                               uint16_t* port,
                               char* path, size_t path_max,
                               bool* is_https);
    static uint32_t parse_ipv4(const char* s);
#endif
};

// ---------------------------------------------------------------------------
// Network service (0x02) handler — declared here, implemented in network_service.cc
// ---------------------------------------------------------------------------

class ApiWindow;

void network_service_handle(const MsgHeader& req,
                             const uint8_t*   payload,
                             uint16_t         payload_len,
                             ApiWindow&       win,
                             TransportEspAt&  transport);
