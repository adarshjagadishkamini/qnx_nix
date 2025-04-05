#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h> // For isspace

#define MAX_LINE_LEN 1024
#define MAX_PATH_LEN 1024 // Use PATH_MAX from <limits.h> on POSIX if preferred

int main(int argc, char *argv[]) {
    FILE *infile;
    char line[MAX_LINE_LEN];
    int line_num = 0;

    // --- Argument Check ---
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_ldd_output_file>\n", argv[0]);
        return 1;
    }

    // --- Open Input File ---
    infile = fopen(argv[1], "r");
    if (infile == NULL) {
        perror("Error opening input file");
        return 1;
    }

    printf("Parsing file: %s\n", argv[1]);

    // --- Read and Parse Line by Line ---
    while (fgets(line, sizeof(line), infile)) {
        line_num++;
        // Remove trailing newline character if present
        line[strcspn(line, "\n")] = 0;

        // Look for the "=>" separator indicating a linked library
        char* arrow = strstr(line, "=>");
        if (arrow) {
            char* lib_path_start = arrow + 2; // Point to characters after "=>"
            char extracted_path[MAX_PATH_LEN];

            // Skip leading whitespace after "=>"
            while (*lib_path_start != '\0' && isspace((unsigned char)*lib_path_start)) {
                lib_path_start++;
            }

            // Find the end of the path (stop at whitespace or '(' or end of string)
            char* lib_path_end = lib_path_start;
            while (*lib_path_end != '\0' &&
                   !isspace((unsigned char)*lib_path_end) &&
                   *lib_path_end != '(') {
                lib_path_end++;
            }

            // Calculate path length
            size_t path_len = lib_path_end - lib_path_start;

            if (path_len > 0 && path_len < MAX_PATH_LEN) {
                // Copy the extracted path string
                strncpy(extracted_path, lib_path_start, path_len);
                extracted_path[path_len] = '\0';

                // Basic validation: Check if the path looks like an absolute path
                if (extracted_path[0] == '/') {
                    printf("  Found path: %s (from line %d)\n", extracted_path, line_num);
                } else {
                    // Optional: Print skipped relative paths if desired
                    // printf("  Skipped relative path: %s (from line %d)\n", extracted_path, line_num);
                }
            } else if (path_len == 0) {
                 // Optional: Indicate if nothing was found after =>
                 // printf("  Found '=>' but no path followed on line %d\n", line_num);
            } else {
                fprintf(stderr, "Warning: Extracted path too long on line %d\n", line_num);
            }
        }
        // Optional: Print lines that don't contain "=>" if needed for debugging
        // else {
        //     printf("  Line %d does not contain '=>'\n", line_num);
        // }
    }

    // --- Cleanup ---
    if (ferror(infile)) {
        perror("Error reading input file");
    }
    fclose(infile);

    printf("Parsing complete.\n");
    return 0;
}