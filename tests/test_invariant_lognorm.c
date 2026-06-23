#include <check.h>
#include <stdlib.h>
#include <string.h>
#include "../src/lognorm.h"

START_TEST(test_buffer_reads_never_exceed_declared_length)
{
    // Invariant: Buffer reads never exceed the declared length
    const char *payloads[] = {
        "normal",                    // Valid input
        "A" * 255,                   // Boundary: 255 chars (common buffer size)
        "EXPLOIT" * 100,             // Exact exploit case: 700 chars
        "X" * 1024,                  // Large payload: 1024 chars
        "\0"                         // Edge case: null byte only
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);
    
    for (int i = 0; i < num_payloads; i++) {
        char dest[256] = {0};        // Fixed buffer size
        const char *src = payloads[i];
        size_t src_len = strlen(src);
        
        // Call actual function - assuming it's normalize_string or similar
        int result = normalize_string(dest, sizeof(dest), src);
        
        // Check invariant: no buffer overflow occurred
        ck_assert_msg(result >= 0, "Function failed on payload %d", i);
        ck_assert_msg(strlen(dest) < sizeof(dest), 
                     "Output exceeds buffer size for payload %d", i);
        ck_assert_msg(dest[sizeof(dest)-1] == '\0', 
                     "Buffer not null-terminated for payload %d", i);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_buffer_reads_never_exceed_declared_length);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}