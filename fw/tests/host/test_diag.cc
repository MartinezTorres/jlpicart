// test_diag.cc — unit tests for DiagCode, DiagStatus, diag_code_to_string.
#include "test_helpers.h"
#include "diag/diag.h"
#include <cstring>

static void test_diag_status_ok() {
    DiagStatus s = DiagStatus::success();
    CHECK(s.ok());
    CHECK(s.code == DiagCode::OK);
    CHECK(s.detail == 0);
}

static void test_diag_status_error() {
    DiagStatus s = DiagStatus::error(DiagCode::POLICY_MISSING);
    CHECK(!s.ok());
    CHECK(s.code == DiagCode::POLICY_MISSING);
    CHECK(s.detail == 0);
}

static void test_diag_status_error_with_detail() {
    DiagStatus s = DiagStatus::error(DiagCode::POLICY_VERSION_UNSUPPORTED, 42u);
    CHECK(!s.ok());
    CHECK(s.code == DiagCode::POLICY_VERSION_UNSUPPORTED);
    CHECK(s.detail == 42u);
}

static void test_diag_code_to_string_known() {
    // All named codes must return a non-null, non-empty string.
    DiagCode codes[] = {
        DiagCode::OK,
        DiagCode::INTERNAL_ASSERT,
        DiagCode::OTP_UNREADABLE,
        DiagCode::POLICY_MISSING,
        DiagCode::POLICY_BAD_SIGNATURE,
        DiagCode::POLICY_BAD_CANONICALIZATION,
        DiagCode::POLICY_VERSION_UNSUPPORTED,
        DiagCode::POLICY_FLASH_READ_ERROR,
        DiagCode::STORAGE_CORRUPT,
        DiagCode::STORAGE_FULL,
        DiagCode::COLLECTION_HASH_MISMATCH,
        DiagCode::COLLECTION_BAD_MANIFEST,
        DiagCode::COLLECTION_UNSATISFIED_REQ,
    };
    for (DiagCode c : codes) {
        const char* s = diag_code_to_string(c);
        CHECK(s != nullptr);
        CHECK(strlen(s) > 0);
    }
}

static void test_diag_code_to_string_ok_is_ok() {
    CHECK(strcmp(diag_code_to_string(DiagCode::OK), "OK") == 0);
}

int main() {
    test_diag_status_ok();
    test_diag_status_error();
    test_diag_status_error_with_detail();
    test_diag_code_to_string_known();
    test_diag_code_to_string_ok_is_ok();
    return test_summary();
}
