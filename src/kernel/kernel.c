/* ============================================================================
 *  ObsidianOS - minimal 32-bit kernel
 *
 *  - VGA text mode console (0xB8000)
 *  - PS/2 keyboard polling (no IDT/IRQ needed)
 *  - Interactive command shell
 *  - PCI bus enumeration (pci.c)
 *  - RTL8139 Ethernet + ARP + ICMP echo (rtl8139.c)
 *
 *  Freestanding: no libc, no standard headers.  Everything the kernel needs
 *  (integer types, port I/O, memcpy/memset/strcmp) is defined right here.
 * ==========================================================================*/

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef signed int     int32_t;
typedef unsigned int   size_t;

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEMORY  ((volatile uint16_t *)0xB8000)

#define COLOR_DEFAULT 0x07      /* light grey on black */
#define COLOR_PROMPT  0x0A      /* light green         */
#define COLOR_HEAD    0x0B      /* light cyan          */
#define COLOR_OK      0x0A      /* light green         */
#define COLOR_ERROR   0x0C      /* light red           */
#define COLOR_DIM     0x08      /* dark grey           */

/* --------------------------------------------------------------------------
 *  Port I/O
 * ------------------------------------------------------------------------*/
static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* --------------------------------------------------------------------------
 *  Freestanding memory / string helpers
 * ------------------------------------------------------------------------*/
void *memcpy(void *destination, const void *source, size_t count)
{
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;

    while (count--)
        *dst++ = *src++;

    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    uint8_t *dst = (uint8_t *)destination;

    while (count--)
        *dst++ = (uint8_t)value;

    return destination;
}

size_t strlen(const char *text)
{
    size_t length = 0;

    while (text[length] != '\0')
        length++;

    return length;
}

int strcmp(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }

    return (int)(uint8_t)*left - (int)(uint8_t)*right;
}

/* --------------------------------------------------------------------------
 *  VGA text console
 * ------------------------------------------------------------------------*/
static int     vga_row;
static int     vga_col;
static uint8_t vga_color;

static void vga_update_cursor(void)
{
    uint16_t position = (uint16_t)(vga_row * VGA_WIDTH + vga_col);

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(position & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((position >> 8) & 0xFF));
}

static void vga_clear(void)
{
    int i;

    for (i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_MEMORY[i] = (uint16_t)(' ' | ((uint16_t)vga_color << 8));

    vga_row = 0;
    vga_col = 0;
    vga_update_cursor();
}

static void vga_scroll(void)
{
    int i;

    for (i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
        VGA_MEMORY[i] = VGA_MEMORY[i + VGA_WIDTH];

    for (i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
        VGA_MEMORY[i] = (uint16_t)(' ' | ((uint16_t)vga_color << 8));

    vga_row = VGA_HEIGHT - 1;
}

static void __attribute__((noinline)) vga_putc(char character)
{
    if (character == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (character == '\r') {
        vga_col = 0;
    } else if (character == '\b') {
        if (vga_col > 0) {
            vga_col--;
        } else if (vga_row > 0) {
            vga_row--;
            vga_col = VGA_WIDTH - 1;
        }
        VGA_MEMORY[vga_row * VGA_WIDTH + vga_col] =
            (uint16_t)(' ' | ((uint16_t)vga_color << 8));
    } else if (character == '\t') {
        int spaces = 4 - (vga_col & 3);

        while (spaces-- > 0) {
            VGA_MEMORY[vga_row * VGA_WIDTH + vga_col] =
                (uint16_t)(' ' | ((uint16_t)vga_color << 8));
            vga_col++;
            if (vga_col >= VGA_WIDTH) {
                vga_col = 0;
                vga_row++;
            }
        }
    } else {
        VGA_MEMORY[vga_row * VGA_WIDTH + vga_col] =
            (uint16_t)((uint8_t)character | ((uint16_t)vga_color << 8));

        vga_col++;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
    }

    while (vga_row >= VGA_HEIGHT)
        vga_scroll();

    vga_update_cursor();
}

static void __attribute__((noinline)) vga_puts(const char *text)
{
    while (*text != '\0')
        vga_putc(*text++);
}

static void vga_puts_color(const char *text, uint8_t color)
{
    uint8_t saved = vga_color;

    vga_color = color;
    vga_puts(text);
    vga_color = saved;
}

static void print_udec(uint32_t value)
{
    char digits[11];
    int  count = 0;

    if (value == 0) {
        vga_putc('0');
        return;
    }

    while (value > 0) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (count > 0)
        vga_putc(digits[--count]);
}

static void print_hex(uint32_t value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    int shift;

    for (shift = (digits - 1) * 4; shift >= 0; shift -= 4)
        vga_putc(hex[(value >> shift) & 0x0F]);
}

static void print_hex8(uint8_t value)
{
    print_hex(value, 2);
}

static void print_hex16(uint16_t value)
{
    print_hex(value, 4);
}

static void print_mac(const uint8_t *address)
{
    int i;

    for (i = 0; i < 6; i++) {
        if (i != 0)
            vga_putc(':');
        print_hex8(address[i]);
    }
}

static void print_ip(const uint8_t *address)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (i != 0)
            vga_putc('.');
        print_udec(address[i]);
    }
}

/* --------------------------------------------------------------------------
 *  PS/2 keyboard (scancode set 1, polling)
 * ------------------------------------------------------------------------*/
static const char keyboard_map[128] = {
    [0x01] = 27,   [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5',  [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9',
    [0x0B] = '0',  [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q',  [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y',  [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[',  [0x1B] = ']', [0x1C] = '\n',
    [0x1E] = 'a',  [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h',  [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';',
    [0x28] = '\'', [0x29] = '`', [0x2B] = '\\',
    [0x2C] = 'z',  [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n',  [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x37] = '*',  [0x39] = ' ',
    [0x47] = '7',  [0x48] = '8', [0x49] = '9', [0x4A] = '-',
    [0x4B] = '4',  [0x4C] = '5', [0x4D] = '6', [0x4E] = '+',
    [0x4F] = '1',  [0x50] = '2', [0x51] = '3', [0x52] = '0', [0x53] = '.'
};

static const char keyboard_shift_map[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+',
    [0x1A] = '{', [0x1B] = '}', [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|', [0x33] = '<', [0x34] = '>', [0x35] = '?'
};

static int shift_down;
static int caps_lock;
static int extended_key;

static int keyboard_poll(void)
{
    uint8_t status;
    uint8_t scancode;
    char    character;

    status = inb(0x64);
    if ((status & 0x01) == 0)
        return -1;

    scancode = inb(0x60);

    if (status & 0x20)          /* byte came from the PS/2 mouse */
        return -1;

    if (scancode == 0xE0) {
        extended_key = 1;
        return -1;
    }

    if (scancode == 0xE1) {
        extended_key = 2;
        return -1;
    }

    if (extended_key) {
        extended_key = 0;
        return -1;              /* ignore arrows, home, ... */
    }

    if (scancode & 0x80) {      /* key release */
        uint8_t code = (uint8_t)(scancode & 0x7F);

        if (code == 0x2A || code == 0x36)
            shift_down = 0;

        return -1;
    }

    if (scancode == 0x2A || scancode == 0x36) {
        shift_down = 1;
        return -1;
    }

    if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        return -1;
    }

    if (scancode == 0x1C)
        return '\n';

    if (scancode == 0x0E)
        return '\b';

    character = keyboard_map[scancode];

    if (character >= 'a' && character <= 'z') {
        if (shift_down ^ caps_lock)
            character = (char)(character - 'a' + 'A');
    } else if (shift_down && keyboard_shift_map[scancode] != 0) {
        character = keyboard_shift_map[scancode];
    }

    return (int)(uint8_t)character;
}

static char keyboard_getchar(void)
{
    int key;

    for (;;) {
        key = keyboard_poll();
        if (key >= 0)
            return (char)key;

        __asm__ volatile ("pause");
    }
}

/* --------------------------------------------------------------------------
 *  Shared PCI device description (must match src/net/pci.c)
 * ------------------------------------------------------------------------*/
typedef struct {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint8_t  base_class;
    uint8_t  sub_class;
    uint8_t  prog_if;
    uint16_t vendor;
    uint16_t device;
    uint32_t bar0;
    uint32_t bar1;
} pci_dev_t;

extern int pci_scan(pci_dev_t *devices, int max_devices);

/* --------------------------------------------------------------------------
 *  Network driver interface (src/net/rtl8139.c)
 * ------------------------------------------------------------------------*/
extern int            rtl8139_init(void);
extern int            rtl8139_initialized(void);
extern const uint8_t *rtl8139_mac(void);
extern uint16_t       rtl8139_io_base(void);
extern int            rtl8139_arp_resolve(const uint8_t ip[4], uint8_t mac[6]);
extern int            rtl8139_ping(const uint8_t ip[4], const uint8_t mac[6]);

/* --------------------------------------------------------------------------
 *  Shell
 * ------------------------------------------------------------------------*/
#define LINE_MAX 128

static char shell_line[LINE_MAX];
static int  shell_length;

static const uint8_t default_gateway[4] = { 10, 0, 2, 2 };

static void shell_prompt(void)
{
    vga_puts_color("obsidian> ", COLOR_PROMPT);
}

static void shell_readline(void)
{
    char key;

    shell_length = 0;

    for (;;) {
        key = keyboard_getchar();

        if (key == '\n') {
            vga_putc('\n');
            shell_line[shell_length] = '\0';
            return;
        }

        if (key == '\b') {
            if (shell_length > 0) {
                shell_length--;
                vga_putc('\b');
            }
            continue;
        }

        if (key >= 32 && key < 127 && shell_length < LINE_MAX - 1) {
            shell_line[shell_length++] = key;
            vga_putc(key);
        }
    }
}

static int parse_ip(const char *text, uint8_t address[4])
{
    int part;
    int value;
    int digits;

    for (part = 0; part < 4; part++) {
        value = 0;
        digits = 0;

        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (*text - '0');
            if (value > 255)
                return 0;
            text++;
            digits++;
        }

        if (digits == 0)
            return 0;

        address[part] = (uint8_t)value;

        if (part < 3) {
            if (*text != '.')
                return 0;
            text++;
        }
    }

    return (*text == '\0');
}

static const char *pci_class_name(uint8_t base_class, uint8_t sub_class)
{
    (void)sub_class;

    switch (base_class) {
    case 0x00: return "legacy";
    case 0x01: return "storage";
    case 0x02: return "network";
    case 0x03: return "display";
    case 0x04: return "multimedia";
    case 0x05: return "memory";
    case 0x06: return "bridge";
    case 0x07: return "comm";
    case 0x08: return "system";
    case 0x09: return "input";
    case 0x0A: return "docking";
    case 0x0B: return "processor";
    case 0x0C: return "serial-bus";
    case 0x0D: return "wireless";
    case 0x0E: return "intelligent-io";
    case 0x0F: return "satellite";
    case 0x10: return "crypto";
    default:   return "other";
    }
}

static const char *nic_name(uint16_t vendor, uint16_t device)
{
    if (vendor == 0x10EC && device == 0x8139)
        return "Realtek RTL8139";
    if (vendor == 0x8086 && device == 0x100E)
        return "Intel 82540EM (e1000)";
    if (vendor == 0x8086 && device == 0x100F)
        return "Intel 82545EM (e1000)";
    if (vendor == 0x8086 && device == 0x10D3)
        return "Intel 82574L (e1000e)";
    if (vendor == 0x1AF4 && device == 0x1000)
        return "Virtio network";
    if (vendor == 0x1022 && device == 0x2000)
        return "AMD PCnet";
    return 0;
}

static void command_help(void)
{
    vga_puts_color("Available commands:\n", COLOR_HEAD);
    vga_puts("  help          show this help\n");
    vga_puts("  cls, clear    clear the screen\n");
    vga_puts("  pci           list PCI devices\n");
    vga_puts("  net           show RTL8139 status, I/O base and MAC address\n");
    vga_puts("  arp [ip]      resolve the MAC address of an IP (default 10.0.2.2)\n");
    vga_puts("  ping [ip]     ARP + ICMP echo request (default 10.0.2.2)\n");
  vga_puts("  tcp           TCP connectivity test (prototype stub)\n");
    vga_puts("  info          system information\n");
    vga_puts("  echo <text>   print text\n");
    vga_puts("  reboot        reboot the machine\n");
    vga_puts("  halt          halt the CPU\n");
}

static void command_pci(void)
{
    pci_dev_t devices[32];
    int count;
    int i;

    vga_puts_color("PCI devices\n", COLOR_HEAD);
    count = pci_scan(devices, 32);

    if (count == 0) {
        vga_puts_color("  no PCI devices found\n", COLOR_ERROR);
        return;
    }

    for (i = 0; i < count; i++) {
        const char *name = nic_name(devices[i].vendor, devices[i].device);

        vga_puts("  ");
        print_hex8(devices[i].bus);
        vga_putc(':');
        print_hex8(devices[i].slot);
        vga_putc('.');
        print_udec(devices[i].func);
        vga_puts("  ");
        print_hex16(devices[i].vendor);
        vga_putc(':');
        print_hex16(devices[i].device);
        vga_puts("  [");
        vga_puts(pci_class_name(devices[i].base_class, devices[i].sub_class));
        vga_putc(']');

        if (name != 0) {
            vga_putc(' ');
            vga_puts(name);
        }

        vga_putc('\n');
    }
}

static void command_net(void)
{
    vga_puts_color("Network status\n", COLOR_HEAD);

    if (!rtl8139_initialized()) {
        pci_dev_t devices[32];
        int count = pci_scan(devices, 32);
        int i;

        vga_puts_color("  RTL8139 driver : not initialized\n", COLOR_ERROR);

        for (i = 0; i < count; i++) {
            if (devices[i].base_class == 0x02) {
                vga_puts("  other NIC      : ");
                print_hex16(devices[i].vendor);
                vga_putc(':');
                print_hex16(devices[i].device);
                vga_puts(" (only the RTL8139 is supported)\n");
                break;
            }
        }

        if (i == count)
            vga_puts_color("  no network controller found\n", COLOR_ERROR);
        return;
    }

    vga_puts("  Driver     : Realtek RTL8139 (I/O mapped)\n");
    vga_puts("  I/O base   : 0x");
    print_hex16(rtl8139_io_base());
    vga_putc('\n');
    vga_puts("  MAC address: ");
    print_mac(rtl8139_mac());
    vga_putc('\n');
    vga_puts("  IPv4       : 10.0.2.15/24 (static, QEMU user-mode network)\n");
    vga_puts("  Gateway    : 10.0.2.2\n");
    vga_puts_color("  State      : ready\n", COLOR_OK);
}

static void command_arp(const char *argument)
{
    uint8_t ip[4];
    uint8_t mac[6];

    if (!rtl8139_initialized()) {
        vga_puts_color("arp: RTL8139 is not initialized\n", COLOR_ERROR);
        return;
    }

    if (argument != 0 && *argument != '\0') {
        if (!parse_ip(argument, ip)) {
            vga_puts_color("arp: invalid IPv4 address\n", COLOR_ERROR);
            return;
        }
    } else {
        memcpy(ip, default_gateway, 4);
    }

    vga_puts("ARP: who has ");
    print_ip(ip);
    vga_puts(" ? ");

    if (rtl8139_arp_resolve(ip, mac)) {
        vga_puts_color("reply ", COLOR_OK);
        print_mac(mac);
        vga_putc('\n');
    } else {
        vga_puts_color("no reply (timeout)\n", COLOR_ERROR);
    }
}

static void command_ping(const char *argument)
{
    uint8_t ip[4];
    uint8_t mac[6];

    if (!rtl8139_initialized()) {
        vga_puts_color("ping: RTL8139 is not initialized\n", COLOR_ERROR);
        return;
    }

    if (argument != 0 && *argument != '\0') {
        if (!parse_ip(argument, ip)) {
            vga_puts_color("ping: invalid IPv4 address\n", COLOR_ERROR);
            return;
        }
    } else {
        memcpy(ip, default_gateway, 4);
    }

    vga_puts("ARP: resolving ");
    print_ip(ip);
    vga_puts(" ... ");

    if (!rtl8139_arp_resolve(ip, mac)) {
        vga_puts_color("failed (timeout)\n", COLOR_ERROR);
        return;
    }

    print_mac(mac);
    vga_putc('\n');

    vga_puts("PING ");
    print_ip(ip);
    vga_puts(": 32 data bytes\n");

    {
        int ping_result = rtl8139_ping(ip, mac);
        if (ping_result == 1) {
            vga_puts_color("Reply from ", COLOR_OK);
            print_ip(ip);
            vga_puts(": bytes=32 ttl=64\n");
        } else if (ping_result == -1) {
            vga_puts_color("Send failed\n", COLOR_ERROR);
        } else {
            vga_puts_color("Request timed out\n", COLOR_ERROR);
        }
    }
}

static void command_tcp(void)
{
    vga_puts_color("TCP: not implemented in this prototype\n", COLOR_ERROR);
    vga_puts("  (ARP/ICMP path is verified; a full TCP stack is out of scope.)\n");
}

static void command_info(void)
{
    vga_puts_color("ObsidianOS\n", COLOR_HEAD);
    vga_puts("  Version : 0.1 (prototype)\n");
    vga_puts("  CPU     : 32-bit x86, protected mode, flat memory model\n");
    vga_puts("  Video   : VGA text mode 80x25 at 0xB8000\n");
    vga_puts("  Input   : PS/2 keyboard (polled)\n");
    vga_puts("  Kernel  : linked at 0x00010000, loaded from LBA 1\n");
    vga_puts("  Network : RTL8139 driver, static 10.0.2.15/24\n");
    vga_puts("  Build   : NASM + freestanding C, no libc\n");
}

static void command_reboot(void)
{
    vga_puts("Rebooting...\n");

    /* Give the VGA controller a moment to flush the text. */
    {
        volatile uint32_t spin;
        for (spin = 0; spin < 2000000; spin++)
            __asm__ volatile ("pause");
    }

    /* Pulse the keyboard controller reset line (works on QEMU and most PCs). */
    outb(0x64, 0xFE);

    /* Fallback: load an empty IDT and trigger an exception (triple fault). */
    {
        struct {
            uint16_t limit;
            uint32_t base;
        } __attribute__((packed)) idtr = { 0, 0 };

        __asm__ volatile ("lidt %0" : : "m"(idtr));
        __asm__ volatile ("int3");
    }

    for (;;)
        __asm__ volatile ("cli; hlt");
}

static void command_halt(void)
{
    vga_puts_color("System halted. You can close the QEMU window.\n", COLOR_ERROR);

    for (;;)
        __asm__ volatile ("cli; hlt");
}

static void shell_execute(char *line)
{
    char *cursor = line;
    char *command;
    char *argument;
    char *end;

    while (*cursor == ' ')
        cursor++;

    command = cursor;

    while (*cursor != '\0' && *cursor != ' ')
        cursor++;

    if (*cursor != '\0') {
        *cursor = '\0';
        cursor++;
    }

    while (*cursor == ' ')
        cursor++;

    argument = cursor;
    end = argument;

    while (*end != '\0')
        end++;

    while (end > argument && end[-1] == ' ') {
        end--;
        *end = '\0';
    }

    if (*command == '\0')
        return;

    if (strcmp(command, "help") == 0) {
        command_help();
    } else if (strcmp(command, "cls") == 0 || strcmp(command, "clear") == 0) {
        vga_color = COLOR_DEFAULT;
        vga_clear();
    } else if (strcmp(command, "pci") == 0) {
        command_pci();
    } else if (strcmp(command, "net") == 0) {
        command_net();
    } else if (strcmp(command, "arp") == 0) {
        command_arp(argument);
    } else if (strcmp(command, "ping") == 0) {
        command_ping(argument);
    } else if (strcmp(command, "tcp") == 0) {
        command_tcp();
    } else if (strcmp(command, "info") == 0) {
        command_info();
    } else if (strcmp(command, "echo") == 0) {
        vga_puts(argument);
        vga_putc('\n');
    } else if (strcmp(command, "reboot") == 0) {
        command_reboot();
    } else if (strcmp(command, "halt") == 0) {
        command_halt();
    } else {
        vga_puts_color("Unknown command: ", COLOR_ERROR);
        vga_puts(command);
        vga_puts("\nType 'help' for the command list.\n");
    }
}

/* --------------------------------------------------------------------------
 *  Kernel entry point (called from entry.asm)
 * ------------------------------------------------------------------------*/
void kernel_main(void)
{
    vga_color = COLOR_DEFAULT;
    vga_clear();

    /* We poll every device instead of using interrupts, so mask the PIC. */
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    vga_puts_color("ObsidianOS 0.1", COLOR_HEAD);
    vga_puts(" - 32-bit protected mode kernel\n");
    vga_puts_color("-----------------------------------------------------\n",
                   COLOR_DIM);

    if (rtl8139_init()) {
        vga_puts("RTL8139 Ethernet: MAC ");
        print_mac(rtl8139_mac());
        vga_putc('\n');
    } else {
        vga_puts_color("RTL8139 Ethernet: not found on the PCI bus\n",
                       COLOR_ERROR);
    }

    vga_puts("Type 'help' for the command list.\n\n");

    memset(shell_line, 0, sizeof(shell_line));
    shell_length = 0;

    for (;;) {
        shell_prompt();
        shell_readline();
        shell_execute(shell_line);
    }
}
