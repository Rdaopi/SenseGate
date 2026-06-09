#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>

int main(void)
{
    usb_enable(NULL); /* ignore return value — keep running regardless */

    const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

    /* Wait up to 10s for host to open the port (DTR) */
    uint32_t dtr = 0;
    uint32_t tries = 0;
    while (!dtr && tries < 100) {
        uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
        k_msleep(100);
        tries++;
    }

    /* Print regardless of DTR — some terminals don't set it */
    printk("SenseGate alive\n");
    uint32_t i = 0;
    while (1) {
        printk("tick %u\n", i++);
        k_msleep(1000);
    }
}
