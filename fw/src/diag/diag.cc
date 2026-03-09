#include "diag.h"

const char* diag_code_to_string(DiagCode code) {
    switch (code) {
        case DiagCode::OK:                          return "OK";
        case DiagCode::INTERNAL_ASSERT:             return "INTERNAL_ASSERT";
        case DiagCode::OTP_UNREADABLE:              return "OTP_UNREADABLE";
        case DiagCode::POLICY_MISSING:              return "POLICY_MISSING";
        case DiagCode::POLICY_BAD_SIGNATURE:        return "POLICY_BAD_SIGNATURE";
        case DiagCode::POLICY_BAD_CANONICALIZATION: return "POLICY_BAD_CANONICALIZATION";
        case DiagCode::POLICY_VERSION_UNSUPPORTED:  return "POLICY_VERSION_UNSUPPORTED";
        case DiagCode::POLICY_FLASH_READ_ERROR:     return "POLICY_FLASH_READ_ERROR";
        case DiagCode::STORAGE_CORRUPT:             return "STORAGE_CORRUPT";
        case DiagCode::STORAGE_FULL:                return "STORAGE_FULL";
        case DiagCode::COLLECTION_HASH_MISMATCH:    return "COLLECTION_HASH_MISMATCH";
        case DiagCode::COLLECTION_BAD_MANIFEST:     return "COLLECTION_BAD_MANIFEST";
        case DiagCode::COLLECTION_UNSATISFIED_REQ:  return "COLLECTION_UNSATISFIED_REQ";
    }
    return "UNKNOWN";
}
