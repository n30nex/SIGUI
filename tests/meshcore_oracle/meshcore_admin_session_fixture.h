#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace d1l::meshcore::host_fixture {

struct AdminSessionFixtureResult {
    bool passed = false;
    std::size_t positive_checks = 0U;
    std::size_t negative_checks = 0U;
    std::string receipt_json;
    std::vector<std::string> failures;
};

/*
 * Deterministic host-only authenticated-admin transcript.  This composes the
 * pinned oracle packet/crypto/ACL/replay primitives; it is not a production
 * runtime, UI, RF, hardware, or release-closure claim.
 */
AdminSessionFixtureResult run_admin_session_fixture();

}  // namespace d1l::meshcore::host_fixture
