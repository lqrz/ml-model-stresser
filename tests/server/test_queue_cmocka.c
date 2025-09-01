// tests/unit/test_queue_cmocka.c

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>   // uintptr_t
#include <stdlib.h>   // free
#include "server/queue.h"

/* =========================== Helpers =========================== */

static request_t *ptr_val(uintptr_t v) {
    // Convert an integer value into a non-null opaque request_t*.
    // We never dereference it; queue only stores/returns the pointer.
    return (request_t*)(uintptr_t)v;
}

/* =========================== Tests ============================= */

static void test_create_queue_and_empty_dequeue(void **state) {
    (void)state;
    queue_t *q = create_queue();
    assert_non_null(q);

    // New queue must be empty
    assert_null(dequeue(q));

    // Caller owns the queue struct; free it when empty
    free(q);
}

static void test_fifo_three_elements(void **state) {
    (void)state;
    queue_t *q = create_queue();
    assert_non_null(q);

    request_t *r1 = ptr_val(0xA1);
    request_t *r2 = ptr_val(0xB2);
    request_t *r3 = ptr_val(0xC3);

    enqueue(q, r1);
    enqueue(q, r2);
    enqueue(q, r3);

    // FIFO order
    assert_ptr_equal(dequeue(q), r1);
    assert_ptr_equal(dequeue(q), r2);
    assert_ptr_equal(dequeue(q), r3);

    // Now empty again
    assert_null(dequeue(q));

    free(q);
}

static void test_interleave_push_pop(void **state) {
    (void)state;
    queue_t *q = create_queue(); assert_non_null(q);

    request_t *a = ptr_val(0x11), *b = ptr_val(0x22), *c = ptr_val(0x33), *d = ptr_val(0x44);
    enqueue(q, a); enqueue(q, b);
    assert_ptr_equal(dequeue(q), a);
    enqueue(q, c);
    assert_ptr_equal(dequeue(q), b);
    enqueue(q, d);
    assert_ptr_equal(dequeue(q), c);
    assert_ptr_equal(dequeue(q), d);
    assert_null(dequeue(q));

    free(q);
}

static void test_tail_resets_after_drain(void **state) {
    (void)state;
    queue_t *q = create_queue(); assert_non_null(q);

    request_t *x = ptr_val(0xDEAD), *y = ptr_val(0xBEEF);
    enqueue(q, x);
    assert_ptr_equal(dequeue(q), x);
    assert_null(dequeue(q));     // fully drained

    // If tail wasn't reset when head became NULL, this enqueue could break
    enqueue(q, y);
    assert_ptr_equal(dequeue(q), y);
    assert_null(dequeue(q));

    free(q);
}

/* ---- MAIN ---- */
int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_create_queue_and_empty_dequeue),
        cmocka_unit_test(test_fifo_three_elements),
        cmocka_unit_test(test_interleave_push_pop),
        cmocka_unit_test(test_tail_resets_after_drain),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
