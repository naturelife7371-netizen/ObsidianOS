/* ============================================================================
 *  ObsidianOS - PCI bus enumeration
 *
 *  Uses the legacy PCI configuration mechanism #1 (ports 0xCF8 / 0xCFC).
 *  Only 32-bit aligned configuration reads are performed; 16-bit writes are
 *  emulated with a read-modify-write cycle.
 * ==========================================================================*/

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;

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

/* --------------------------------------------------------------------------
 *  Port I/O
 * ------------------------------------------------------------------------*/
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
 *  Configuration space access
 * ------------------------------------------------------------------------*/
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t address;

    address = 0x80000000u
            | ((uint32_t)bus  << 16)
            | ((uint32_t)slot << 11)
            | ((uint32_t)func << 8)
            | ((uint32_t)(offset & 0xFC));

    outl(0xCF8, address);
    return inl(0xCFC);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset,
                        uint32_t value)
{
    uint32_t address;

    address = 0x80000000u
            | ((uint32_t)bus  << 16)
            | ((uint32_t)slot << 11)
            | ((uint32_t)func << 8)
            | ((uint32_t)(offset & 0xFC));

    outl(0xCF8, address);
    outl(0xCFC, value);
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset,
                        uint16_t value)
{
    uint32_t old_value;
    uint32_t mask;
    uint32_t shift;

    old_value = pci_config_read32(bus, slot, func, (uint8_t)(offset & 0xFC));
    shift = (uint32_t)(offset & 2) * 8;
    mask = 0xFFFFu << shift;

    pci_config_write32(bus, slot, func, (uint8_t)(offset & 0xFC),
                       (old_value & ~mask) | ((uint32_t)value << shift));
}

/* --------------------------------------------------------------------------
 *  Enumerate every PCI function. Returns the number of devices written.
 * ------------------------------------------------------------------------*/
int pci_scan(pci_dev_t *devices, int max_devices)
{
    uint32_t bus;
    uint32_t slot;
    int      function;
    int      count = 0;

    for (bus = 0; bus < 256; bus++) {
        for (slot = 0; slot < 32; slot++) {
            uint32_t id;
            uint32_t header;
            int      functions;

            id = pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0x00);
            if ((id & 0xFFFF) == 0xFFFF)
                continue;                       /* no device in this slot */

            header = pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0C);
            functions = (header & 0x00800000u) ? 8 : 1;

            for (function = 0; function < functions; function++) {
                uint32_t class_info;
                uint32_t bar0;
                uint32_t bar1;

                id = pci_config_read32((uint8_t)bus, (uint8_t)slot,
                                       (uint8_t)function, 0x00);
                if ((id & 0xFFFF) == 0xFFFF)
                    continue;

                class_info = pci_config_read32((uint8_t)bus, (uint8_t)slot,
                                               (uint8_t)function, 0x08);
                bar0 = pci_config_read32((uint8_t)bus, (uint8_t)slot,
                                         (uint8_t)function, 0x10);
                bar1 = pci_config_read32((uint8_t)bus, (uint8_t)slot,
                                         (uint8_t)function, 0x14);

                devices[count].bus        = (uint8_t)bus;
                devices[count].slot       = (uint8_t)slot;
                devices[count].func       = (uint8_t)function;
                devices[count].base_class = (uint8_t)((class_info >> 24) & 0xFF);
                devices[count].sub_class  = (uint8_t)((class_info >> 16) & 0xFF);
                devices[count].prog_if    = (uint8_t)((class_info >> 8) & 0xFF);
                devices[count].vendor     = (uint16_t)(id & 0xFFFF);
                devices[count].device     = (uint16_t)((id >> 16) & 0xFFFF);
                devices[count].bar0       = bar0;
                devices[count].bar1       = bar1;

                count++;

                if (count >= max_devices)
                    return count;
            }
        }
    }

    return count;
}
