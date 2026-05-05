#include "rtl8139.h"
#include "io.h"
#include "pci.h"

// QEMU rtl8139 PCI IDs
#define RTL8139_VENDOR 0x10EC
#define RTL8139_DEVICE 0x8139

// Registers (offsets from io_base)
#define REG_IDR0 0x00
#define REG_TSD0 0x10
#define REG_TSAD0 0x20
#define REG_RBSTART 0x30
#define REG_CMD 0x37
#define REG_IMR 0x3C
#define REG_ISR 0x3E
#define REG_RCR 0x44
#define REG_CONFIG1 0x52

// Command bits
#define CMD_RESET 0x10
#define CMD_RX_EN 0x08
#define CMD_TX_EN 0x04

// RCR bits
#define RCR_AAP  (1u << 0)  // accept all physical
#define RCR_APM  (1u << 1)  // accept physical match
#define RCR_AM   (1u << 2)  // accept multicast
#define RCR_AB   (1u << 3)  // accept broadcast
#define RCR_WRAP (1u << 7)

// We use a single RX ring buffer: 8K + 16 + 1500 padding is common. Use 16K for safety.
#define RX_BUF_SIZE (16 * 1024)
static uint8_t rx_buf[RX_BUF_SIZE] __attribute__((aligned(16)));
static uint32_t rx_off;

static void mm_pause(void)
{
    __asm__ __volatile__("pause");
}

static void rtl_write_tx_addr(uint16_t io, int idx, uint32_t phys)
{
    outl((uint16_t)(io + REG_TSAD0 + (idx * 4)), phys);
}

static void rtl_write_tx_status(uint16_t io, int idx, uint32_t v)
{
    outl((uint16_t)(io + REG_TSD0 + (idx * 4)), v);
}

static uint32_t rtl_read_tx_status(uint16_t io, int idx)
{
    return inl((uint16_t)(io + REG_TSD0 + (idx * 4)));
}

// We use 4 TX buffers as required by the device.
#define TX_BUF_SIZE 2048
static uint8_t tx_buf[4][TX_BUF_SIZE] __attribute__((aligned(16)));
static int tx_cur;

int rtl8139_init(rtl8139_t* n)
{
    pci_addr_t dev;
    if (pci_find_device(RTL8139_VENDOR, RTL8139_DEVICE, &dev) != 0) {
        return -1;
    }

    // Enable bus mastering + I/O space in PCI command register.
    uint16_t cmd = pci_read16(dev, 0x04);
    cmd |= 0x0001; // I/O space
    cmd |= 0x0004; // bus master
    pci_write32(dev, 0x04, (pci_read32(dev, 0x04) & 0xFFFF0000u) | cmd);

    // BAR0 (I/O)
    uint32_t bar0 = pci_read32(dev, 0x10);
    if ((bar0 & 0x1) == 0) {
        return -2;
    }
    n->io_base = (uint16_t)(bar0 & 0xFFFC);
    n->irq_line = pci_read8(dev, 0x3C);

    // Power on / wake (CONFIG1 bit 0 = powerdown)
    outb((uint16_t)(n->io_base + REG_CONFIG1), 0x00);

    // Reset
    outb((uint16_t)(n->io_base + REG_CMD), CMD_RESET);
    for (uint32_t i = 0; i < 1000000; i++) {
        if ((inb((uint16_t)(n->io_base + REG_CMD)) & CMD_RESET) == 0) {
            break;
        }
        mm_pause();
    }

    // Read MAC
    for (int i = 0; i < 6; i++) {
        n->mac[i] = inb((uint16_t)(n->io_base + REG_IDR0 + i));
    }

    // Set RX buffer
    rx_off = 0;
    outl((uint16_t)(n->io_base + REG_RBSTART), (uint32_t)(uintptr_t)rx_buf);

    // Configure RCR: accept broadcast + physical match; wrap ring.
    outl((uint16_t)(n->io_base + REG_RCR), RCR_APM | RCR_AB | RCR_WRAP);

    // Init TX buffer addresses
    for (int i = 0; i < 4; i++) {
        rtl_write_tx_addr(n->io_base, i, (uint32_t)(uintptr_t)tx_buf[i]);
    }
    tx_cur = 0;

    // Enable RX/TX
    outb((uint16_t)(n->io_base + REG_CMD), CMD_RX_EN | CMD_TX_EN);

    // Mask interrupts (we'll poll)
    outw((uint16_t)(n->io_base + REG_IMR), 0x0000);
    outw((uint16_t)(n->io_base + REG_ISR), 0xFFFF);

    return 0;
}

int rtl8139_send(rtl8139_t* n, const void* data, uint32_t len)
{
    if (len == 0 || len > TX_BUF_SIZE) {
        return -1;
    }

    int idx = tx_cur & 3;
    tx_cur++;

    // Copy to TX buffer
    const uint8_t* s = (const uint8_t*)data;
    for (uint32_t i = 0; i < len; i++) {
        tx_buf[idx][i] = s[i];
    }

    // Start transmit: write length to TSDx.
    rtl_write_tx_status(n->io_base, idx, len & 0x1FFFu);

    // Poll for completion (TSDx bit 15 TOK)
    for (uint32_t i = 0; i < 1000000; i++) {
        uint32_t st = rtl_read_tx_status(n->io_base, idx);
        if (st & (1u << 15)) {
            return 0;
        }
        mm_pause();
    }
    return -2;
}

int rtl8139_poll_rx(rtl8139_t* n, uint8_t* out_buf, uint32_t out_cap, uint32_t* out_len)
{
    (void)n;

    // CAPR is at 0x38 (word). CBR is 0x3A (word). Use it to see if data available.
    uint16_t cbr = inw((uint16_t)(n->io_base + 0x3A));
    uint16_t capr = inw((uint16_t)(n->io_base + 0x38));

    // If CBR == CAPR, likely no new packet (rough heuristic).
    if (cbr == capr) {
        return 1;
    }

    // RX ring header at rx_off: [status u16][len u16] then payload.
    uint16_t status = *(volatile uint16_t*)(rx_buf + rx_off);
    uint16_t len = *(volatile uint16_t*)(rx_buf + rx_off + 2);
    if ((status & 0x0001) == 0) {
        // Not "ROK"; drop.
        return -1;
    }

    if (len < 4) {
        return -2;
    }

    uint32_t pkt_len = (uint32_t)len - 4; // strip CRC
    if (pkt_len > out_cap) {
        return -3;
    }

    uint32_t pkt_off = rx_off + 4;
    if (pkt_off + pkt_len <= RX_BUF_SIZE) {
        for (uint32_t i = 0; i < pkt_len; i++) {
            out_buf[i] = rx_buf[pkt_off + i];
        }
    } else {
        // Wrap
        uint32_t first = RX_BUF_SIZE - pkt_off;
        for (uint32_t i = 0; i < first; i++) {
            out_buf[i] = rx_buf[pkt_off + i];
        }
        uint32_t rest = pkt_len - first;
        for (uint32_t i = 0; i < rest; i++) {
            out_buf[first + i] = rx_buf[i];
        }
    }

    *out_len = pkt_len;

    // Advance rx_off to next packet, align to 4 bytes.
    rx_off = (rx_off + 4 + len + 3) & ~3u;
    rx_off %= RX_BUF_SIZE;

    // Update CAPR = rx_off - 16 (per datasheet behavior).
    uint16_t new_capr = (uint16_t)((rx_off - 16) & 0xFFFF);
    outw((uint16_t)(n->io_base + 0x38), new_capr);

    // Ack ISR bits
    outw((uint16_t)(n->io_base + REG_ISR), 0xFFFF);

    return 0;
}
