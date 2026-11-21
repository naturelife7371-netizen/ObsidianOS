/* ============================================================================
 *  ObsidianOS - Realtek RTL8139 Ethernet driver (polled, no interrupts)
 *
 *  - PCI detection and I/O BAR setup
 *  - MAC address readout
 *  - Ring based packet reception (8K ring + wrap space)
 *  - Packet transmission through TX descriptor 0
 *  - ARP resolution and ICMP echo ("ping") helpers
 *
 *  The driver targets QEMU's "-device rtl8139" but follows the datasheet
 *  closely enough to work on real RTL8139 adapters.
 * ==========================================================================*/

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long  size_t;

#define RX_RING_SIZE   8192u
#define RX_RING_MASK   (RX_RING_SIZE - 1u)
#define RX_RING_TOTAL  (RX_RING_SIZE + 2048u)   /* wrap + DMA overhang */
#define TX_BUFFER_SIZE 2048u

/* Must match the definition in src/kernel/kernel.c */
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

extern int      pci_scan(pci_dev_t *devices, int max_devices);
extern uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func,
                                  uint8_t offset);
extern void     pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func,
                                   uint8_t offset, uint16_t value);

/* Provided by kernel.c */
extern void *memcpy(void *destination, const void *source, size_t count);
extern void *memset(void *destination, int value, size_t count);

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

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* --------------------------------------------------------------------------
 *  Driver state
 * ------------------------------------------------------------------------*/
static uint16_t io_base;
static uint8_t  mac_address[6];
static int      driver_ready;

static uint32_t rx_next;
static uint8_t  rx_ring[RX_RING_TOTAL] __attribute__((aligned(16)));
static uint8_t  tx_buffer[TX_BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t  rx_packet[2048] __attribute__((aligned(4)));

static uint16_t ping_sequence = 1;

/* Rotating TX descriptor index. QEMU's RTL8139 emulation advances an
 * internal "current TX descriptor" pointer after every transmission, so a
 * driver that always reuses descriptor 0 would stall on the second send.
 * Rotating through descriptors 0..3 keeps us in sync with that pointer. */
static uint8_t tx_desc_index = 0;

/* --------------------------------------------------------------------------
 *  Internal helpers
 * ------------------------------------------------------------------------*/
static void update_capr(void)
{
    /* CAPR must lag 16 bytes behind the real read pointer. */
    outw(io_base + 0x38, (uint16_t)((rx_next - 16u) & RX_RING_MASK));
}

static int find_rtl8139(pci_dev_t *result)
{
    pci_dev_t devices[48];
    int count;
    int i;

    count = pci_scan(devices, 48);

    for (i = 0; i < count; i++) {
        if (devices[i].vendor == 0x10EC && devices[i].device == 0x8139) {
            *result = devices[i];
            return 1;
        }
    }

    return 0;
}

static uint16_t internet_checksum(const uint8_t *data, int length)
{
    uint32_t sum = 0;

    while (length > 1) {
        sum += ((uint32_t)data[0] << 8) | data[1];
        data += 2;
        length -= 2;
    }

    if (length > 0)
        sum += (uint32_t)data[0] << 8;

    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);

    return (uint16_t)(~sum);
}

/* --------------------------------------------------------------------------
 *  Initialisation
 * ------------------------------------------------------------------------*/
int rtl8139_init(void)
{
    pci_dev_t device;
    uint16_t  command;
    uint32_t  spin;
    int       i;

    if (driver_ready)
        return 1;

    if (!find_rtl8139(&device))
        return 0;

    if ((device.bar0 & 0x01u) == 0)
        return 0;                       /* BAR0 is not an I/O BAR */

    io_base = (uint16_t)(device.bar0 & 0xFFFCu);
    if (io_base == 0)
        return 0;

    /* Enable I/O space decoding and bus mastering. */
    command = (uint16_t)(pci_config_read32(device.bus, device.slot,
                                           device.func, 0x04) & 0xFFFFu);
    pci_config_write16(device.bus, device.slot, device.func, 0x04,
                       (uint16_t)(command | 0x0005u));

    /* Wake up and reset the controller. */
    outb(io_base + 0x52, 0x00);         /* CONFIG1: power on */
    outb(io_base + 0x37, 0x10);         /* CR: software reset */

    for (spin = 0; spin < 1000000u; spin++) {
        if ((inb(io_base + 0x37) & 0x10) == 0)
            break;
    }

    /* MAC address lives in IDR0..IDR5. */
    for (i = 0; i < 6; i++)
        mac_address[i] = inb(io_base + i);

    memset(rx_ring, 0, sizeof(rx_ring));
    memset(rx_packet, 0, sizeof(rx_packet));
    rx_next = 0;

    /* Receive ring: 8K, 4-byte aligned. */
    outl(io_base + 0x30, (uint32_t)(unsigned long)rx_ring);   /* RBSTART */
    outl(io_base + 0x44, 0x0000008Fu);         /* RCR: WRAP + accept all */

    outw(io_base + 0x3C, 0x0000u);             /* IMR: no interrupts */
    outw(io_base + 0x3E, 0xFFFFu);             /* clear pending ISR bits */

    outb(io_base + 0x37, 0x0Cu);               /* CR: RX + TX enable */

    /* Writing CAPR makes the chip set its read pointer to CAPR + 16. */
    outw(io_base + 0x38, (uint16_t)((RX_RING_SIZE - 16u) & RX_RING_MASK));

    driver_ready = 1;
    return 1;
}

int rtl8139_initialized(void)
{
    return driver_ready;
}

const uint8_t *rtl8139_mac(void)
{
    return mac_address;
}

uint16_t rtl8139_io_base(void)
{
    return io_base;
}

/* --------------------------------------------------------------------------
 *  Transmission
 * ------------------------------------------------------------------------*/
int rtl8139_send(const void *data, int length)
{
    uint32_t spin;
    int      total;

    if (!driver_ready || length <= 0 || (uint32_t)length > TX_BUFFER_SIZE)
        return -1;

    memcpy(tx_buffer, data, (size_t)length);

    total = length;
    if (total < 60) {                       /* Ethernet minimum frame */
        memset(tx_buffer + total, 0, (size_t)(60 - total));
        total = 60;
    }

    {
        uint16_t tsd_reg = (uint16_t)(0x10u + 4u * tx_desc_index);
        uint16_t tsad_reg = (uint16_t)(0x20u + 4u * tx_desc_index);

        /* Wait until the chosen descriptor has been released by the host. */
        for (spin = 0; spin < 1000000u; spin++) {
            if (inl(io_base + tsd_reg) & 0x2000u)  /* OWN: host owns descriptor */
                break;
        }

        outl(io_base + tsad_reg, (uint32_t)(unsigned long)tx_buffer);  /* TSADn */
        outl(io_base + tsd_reg, (uint32_t)total);        /* TSDn: length, OWN = 0 */

        /* Wait for TOK (transmit OK). */
        for (spin = 0; spin < 2000000u; spin++) {
            if (inl(io_base + tsd_reg) & 0x8000u) {
                tx_desc_index = (uint8_t)((tx_desc_index + 1u) & 3u);
                return 0;
            }
        }

        return -1;
    }
}

/* --------------------------------------------------------------------------
 *  Reception
 * ------------------------------------------------------------------------*/
int rtl8139_receive(void *buffer, int max_length)
{
    uint8_t *packet;
    uint16_t status;
    uint16_t length;
    uint32_t advance;
    int      data_length;

    if (!driver_ready)
        return 0;

    /* CR bit 0 is the read-only RxBufEmpty flag. */
    if (inb(io_base + 0x37) & 0x01u)
        return 0;

    packet = &rx_ring[rx_next];
    status = (uint16_t)packet[0] | ((uint16_t)packet[1] << 8);
    length = (uint16_t)packet[2] | ((uint16_t)packet[3] << 8);

    if (length < 4 || length > RX_RING_SIZE) {
        rx_next = (rx_next + 4u) & RX_RING_MASK;
        update_capr();
        return 0;
    }

    advance = 4u + (((uint32_t)length + 3u) & ~3u);

    if ((status & 0x0001u) == 0) {          /* RxStatusOK not set */
        rx_next = (rx_next + advance) & RX_RING_MASK;
        update_capr();
        return 0;
    }

    data_length = (int)length - 4;          /* strip the 4-byte CRC */
    if (data_length > max_length)
        data_length = max_length;

    memcpy(buffer, packet + 4, (size_t)data_length);

    rx_next = (rx_next + advance) & RX_RING_MASK;
    update_capr();

    return data_length;
}

/* --------------------------------------------------------------------------
 *  ARP: resolve an IPv4 address to a MAC address
 * ------------------------------------------------------------------------*/
int rtl8139_arp_resolve(const uint8_t ip[4], uint8_t mac[6])
{
    uint8_t  frame[64];
    uint32_t spin;
    int      i;

    if (!driver_ready)
        return 0;

    for (i = 0; i < 6; i++)
        frame[i] = 0xFF;                    /* broadcast */
    for (i = 0; i < 6; i++)
        frame[6 + i] = mac_address[i];

    frame[12] = 0x08; frame[13] = 0x06;     /* EtherType: ARP */

    frame[14] = 0x00; frame[15] = 0x01;     /* hardware type: Ethernet */
    frame[16] = 0x08; frame[17] = 0x00;     /* protocol type: IPv4 */
    frame[18] = 6;                          /* hardware length */
    frame[19] = 4;                          /* protocol length */
    frame[20] = 0x00; frame[21] = 0x01;     /* operation: request */

    for (i = 0; i < 6; i++)
        frame[22 + i] = mac_address[i];     /* sender MAC */

    frame[28] = 10; frame[29] = 0; frame[30] = 2; frame[31] = 15;

    for (i = 0; i < 6; i++)
        frame[32 + i] = 0;                  /* target MAC unknown */

    frame[38] = ip[0];
    frame[39] = ip[1];
    frame[40] = ip[2];
    frame[41] = ip[3];

    if (rtl8139_send(frame, 42) != 0)
        return 0;

    for (spin = 0; spin < 3000000u; spin++) {
        int received = rtl8139_receive(rx_packet, (int)sizeof(rx_packet));

        if (received < 42)
            continue;

        if (rx_packet[12] != 0x08 || rx_packet[13] != 0x06)
            continue;

        if (rx_packet[20] != 0x00 || rx_packet[21] != 0x02)
            continue;                       /* not an ARP reply */

        if (rx_packet[28] != ip[0] || rx_packet[29] != ip[1] ||
            rx_packet[30] != ip[2] || rx_packet[31] != ip[3])
            continue;

        for (i = 0; i < 6; i++)
            mac[i] = rx_packet[22 + i];

        return 1;
    }

    return 0;
}

/* --------------------------------------------------------------------------
 *  ICMP echo request
 * ------------------------------------------------------------------------*/
int rtl8139_ping(const uint8_t ip[4], const uint8_t mac[6])
{
    static const char payload[] = "ObsidianOS-ping!";
    uint8_t  frame[128];
    uint16_t checksum;
    uint32_t spin;
    int      i;

    if (!driver_ready)
        return 0;

    for (i = 0; i < 6; i++)
        frame[i] = mac[i];
    for (i = 0; i < 6; i++)
        frame[6 + i] = mac_address[i];

    frame[12] = 0x08; frame[13] = 0x00;     /* EtherType: IPv4 */

    /* IPv4 header (20 bytes) */
    frame[14] = 0x45;                       /* version 4, IHL 5 */
    frame[15] = 0x00;                       /* DSCP */
    frame[16] = 0x00; frame[17] = 44;       /* total length: 20 + 8 + 16 */
    frame[18] = 0x4F; frame[19] = 0x53;     /* identification */
    frame[20] = 0x00; frame[21] = 0x00;     /* flags / fragment offset */
    frame[22] = 64;                         /* TTL */
    frame[23] = 1;                          /* protocol: ICMP */
    frame[24] = 0x00; frame[25] = 0x00;     /* checksum (filled below) */

    frame[26] = 10; frame[27] = 0; frame[28] = 2; frame[29] = 15;

    frame[30] = ip[0];
    frame[31] = ip[1];
    frame[32] = ip[2];
    frame[33] = ip[3];

    checksum = internet_checksum(&frame[14], 20);
    frame[24] = (uint8_t)(checksum >> 8);
    frame[25] = (uint8_t)(checksum & 0xFF);

    /* ICMP echo request */
    frame[34] = 8;                          /* type: echo request */
    frame[35] = 0;                          /* code */
    frame[36] = 0x00; frame[37] = 0x00;     /* checksum (filled below) */
    frame[38] = 0x4F; frame[39] = 0x53;     /* identifier */
    frame[40] = (uint8_t)(ping_sequence >> 8);
    frame[41] = (uint8_t)(ping_sequence & 0xFF);

    for (i = 0; i < 16; i++)
        frame[42 + i] = (uint8_t)payload[i];

    checksum = internet_checksum(&frame[34], 8 + 16);
    frame[36] = (uint8_t)(checksum >> 8);
    frame[37] = (uint8_t)(checksum & 0xFF);

    ping_sequence++;

    if (rtl8139_send(frame, 14 + 20 + 8 + 16) != 0)
        return -1;

    for (spin = 0; spin < 3000000u; spin++) {
        int received = rtl8139_receive(rx_packet, (int)sizeof(rx_packet));
        int header_length;
        int icmp_offset;

        if (received < 42)
            continue;

        if (rx_packet[12] != 0x08 || rx_packet[13] != 0x00)
            continue;

        header_length = (rx_packet[14] & 0x0F) * 4;
        if (header_length < 20 || 14 + header_length + 8 > received)
            continue;

        if (rx_packet[14 + 9] != 1)         /* IP protocol: ICMP */
            continue;

        if (rx_packet[26] != ip[0] || rx_packet[27] != ip[1] ||
            rx_packet[28] != ip[2] || rx_packet[29] != ip[3])
            continue;

        icmp_offset = 14 + header_length;

        if (rx_packet[icmp_offset] != 0)    /* type: echo reply */
            continue;

        if (rx_packet[icmp_offset + 4] != 0x4F ||
            rx_packet[icmp_offset + 5] != 0x53)
            continue;

        return 1;
    }

    return 0;
}
