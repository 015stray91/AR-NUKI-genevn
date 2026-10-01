#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Path pointing to your isolated, pruned NetHunter tools directory
#define NH_BINARY_VAULT "/data/local/nhsystem/kali-arm64/usr/bin/"

int main(int argc, char *argv[]) {
    // 1. Read argv[0] to determine WHICH name link was used to open this file
    char *shortcut_name = strrchr(argv[0], '/');
    if (shortcut_name) {
        shortcut_name++; // Advance past the slash
    } else {
        shortcut_name = argv[0];
    }

    // If executed directly as the box itself, print the toolkit manifest
    if (strcmp(shortcut_name, "nethunter_box") == 0) {
        printf("=== NetHunter Hidden Applet Multi-Call Box ===\n");
        printf("Status: Operational. Use symlink hooks to trigger individual applets.\n");
        return 0;
    }

    // 2. Dynamically build the target pointer path to your isolated NetHunter applet
    char real_execution_path[512];
    snprintf(real_execution_path, sizeof(real_execution_path), "%s%s", NH_BINARY_VAULT, shortcut_name);

    // 3. Hot-swap the execution thread directly into the full-vitamin Linux utility
    // This passes all your custom arguments (like -E or monitor mode flags) seamlessly
    execv(real_execution_path, argv);

    // If execv fails (meaning the tool was removed or path changed), throw an alert
    fprintf(stderr, "[!] NetHunter Box Error: Failed to execute hidden applet '%s'\n", shortcut_name);
    return 1;
}
