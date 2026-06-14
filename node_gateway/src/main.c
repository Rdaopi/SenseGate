#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>
#ifdef CONFIG_USB_DEVICE_STACK
#include <zephyr/usb/usb_device.h>
#endif

int main(void)
{
#ifdef CONFIG_USB_DEVICE_STACK
    usb_enable(NULL);

    const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    uint32_t dtr = 0;
    uint32_t tries = 0;
    while (!dtr && tries < 100) {
        uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
        k_msleep(100);
        tries++;
    }
#endif

    printk("SenseGate alive\n");
    uint32_t i = 0;
    while (1) {
        printk("tick %u\n", i++);
        k_msleep(1000);
    }
}
