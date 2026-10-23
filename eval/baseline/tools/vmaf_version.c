// Prints the version string the linked libvmaf reports about itself.
#include <libvmaf/libvmaf.h>
#include <stdio.h>
int main(void) { printf("vmaf_version()=%s\n", vmaf_version()); return 0; }
