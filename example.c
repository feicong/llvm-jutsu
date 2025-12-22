#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define LICENSE_PREFIX "LICENSE-"
#define MAGIC 0xDAF51234

static int check_license(const char *license) {
    // Check prefix
    if (strncmp(license, LICENSE_PREFIX, strlen(LICENSE_PREFIX)) != 0) {
        return 0;
    }

    // Parse hex value after prefix
    const char *hex_part = license + strlen(LICENSE_PREFIX);
    char *endptr;
    uint32_t value = (uint32_t)strtoul(hex_part, &endptr, 16);

    // Must have consumed exactly 8 hex chars and nothing more
    if (endptr != hex_part + 8 || *endptr != '\0') {
        return 0;
    }

    // The magic check - this will be obfuscated!
    if (value != MAGIC) {
        return 0;
    }

    return 1;
}

int main(int argc, char *argv[]) {
    char license[64];

    if (argc > 1) {
        strncpy(license, argv[1], sizeof(license) - 1);
        license[sizeof(license) - 1] = '\0';
    } else {
        printf("Enter license key: ");
        if (fgets(license, sizeof(license), stdin) == NULL) {
            printf("Error reading input\n");
            return 1;
        }
        license[strcspn(license, "\n")] = '\0';
    }

    if (check_license(license)) {
        printf("Valid!\n");
        return 0;
    } else {
        printf("Invalid.\n");
        return 1;
    }
}
