/* kernel.c */

void kernel_main() {
    // Pointer to the VGA text buffer (base address for color display)
    char *video_memory = (char *)0xB8000;
    const char *str = "Hello from your 32-bit TCC Kernel!";

    // Write characters directly into video memory
    // Each character takes 2 bytes: [ASCII character, Color Attribute]
    while (*str) {
        *video_memory++ = *str++;     // Set character byte
        *video_memory++ = 0x0F;       // Set color byte (0x0F = White text on Black background)
    }
}
