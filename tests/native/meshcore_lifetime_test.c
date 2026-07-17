#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "mesh/meshcore_lifetime.h"

static void test_contact_reachability_boundary_and_wrap(void)
{
    const uint32_t heard = 1000U;
    assert(!d1l_meshcore_lifetime_contact_reachable(0U, heard));
    assert(d1l_meshcore_lifetime_contact_reachable(
        heard, heard + D1L_MESHCORE_CONTACT_REACHABLE_MAX_AGE_MS));
    assert(!d1l_meshcore_lifetime_contact_reachable(
        heard, heard + D1L_MESHCORE_CONTACT_REACHABLE_MAX_AGE_MS + 1U));

    const uint32_t wrapped_heard = UINT32_MAX - 10U;
    assert(d1l_meshcore_lifetime_age_current_u32(
        wrapped_heard, 9U, 20U));
    assert(!d1l_meshcore_lifetime_age_current_u32(
        wrapped_heard, 10U, 20U));
}

static void test_route_age_boundary_and_overflow(void)
{
    const uint32_t max_age = 30U * 60U * 1000U;
    assert(d1l_meshcore_lifetime_age_current_u32(500U, 500U + max_age,
                                                 max_age));
    assert(!d1l_meshcore_lifetime_age_current_u32(
        500U, 500U + max_age + 1U, max_age));
    assert(d1l_meshcore_lifetime_age_current_u32(
        UINT32_MAX - 5U, 4U, 10U));
    assert(!d1l_meshcore_lifetime_age_current_u32(
        UINT32_MAX - 5U, 5U, 10U));
}

static void test_packet_fifo_exact_capacity(void)
{
    uint16_t next = UINT16_MAX;
    assert(d1l_meshcore_lifetime_packet_fifo_next(0U, 160U, &next));
    assert(next == 1U);
    assert(d1l_meshcore_lifetime_packet_fifo_next(159U, 160U, &next));
    assert(next == 0U);
    assert(!d1l_meshcore_lifetime_packet_fifo_next(160U, 160U, &next));
    assert(!d1l_meshcore_lifetime_packet_fifo_next(0U, 0U, &next));
}

static void test_advert_timestamp_never_wraps(void)
{
    assert(d1l_meshcore_lifetime_advert_is_strictly_newer(false, 0U, 0U));
    assert(d1l_meshcore_lifetime_advert_is_strictly_newer(true, 0U, 1U));
    assert(!d1l_meshcore_lifetime_advert_is_strictly_newer(true, 1U, 1U));
    assert(!d1l_meshcore_lifetime_advert_is_strictly_newer(true, 1U, 0U));
    assert(d1l_meshcore_lifetime_advert_is_strictly_newer(
        true, UINT32_MAX - 1U, UINT32_MAX));
    assert(!d1l_meshcore_lifetime_advert_is_strictly_newer(
        true, UINT32_MAX, 0U));
}

int main(void)
{
    test_contact_reachability_boundary_and_wrap();
    test_route_age_boundary_and_overflow();
    test_packet_fifo_exact_capacity();
    test_advert_timestamp_never_wraps();
    puts("native MeshCore lifetime boundaries: ok");
    return 0;
}
