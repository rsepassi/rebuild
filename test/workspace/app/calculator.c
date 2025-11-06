#include <stdio.h>
#include <string.h>
#include "common.h"
#include "math.h"
#include "string_utils.h"

int main(int argc, char** argv) {
    // Print banner
    char banner[256];
    concat("=== ", APP_NAME, banner, sizeof(banner));
    char temp[256];
    concat(banner, " v", temp, sizeof(temp));
    concat(temp, VERSION, banner, sizeof(banner));
    concat(banner, " ===", temp, sizeof(temp));
    printf("%s\n", temp);

    // Test math operations
    printf("\nMath Operations:\n");
    int a = 15;
    int b = 7;

    int sum = add(a, b);
    printf("  %d + %d = %d\n", a, b, sum);

    int product = multiply(a, b);
    printf("  %d * %d = %d\n", a, b, product);

    // Test string operations
    printf("\nString Operations:\n");

    // Test concat
    char str1[] = "Hello";
    char str2[] = " World";
    char result[256];
    concat(str1, str2, result, sizeof(result));
    printf("  concat(\"%s\", \"%s\") = \"%s\"\n", str1, str2, result);

    // Test trim
    char str3[] = "  trimmed  ";
    printf("  Before trim: \"%s\"\n", str3);
    trim(str3);
    printf("  After trim:  \"%s\"\n", str3);

    printf("\nAll operations completed successfully!\n");
    return 0;
}
