#pragma once
// storage_service.h — Storage service (0x01) handler for the JLPiCart API.
//
// Handles: LIST_BLOBS, READ_BLOB, WRITE_BLOB_BEGIN/CHUNK/COMMIT, DELETE_BLOB.

#include "msx/api/api_types.h"

class ApiWindow;
class SaveStore;

void storage_service_handle(const MsgHeader& req,
                             const uint8_t*   payload,
                             uint16_t         payload_len,
                             ApiWindow&       win,
                             SaveStore&       save_store,
                             uint16_t         active_profile_id);
