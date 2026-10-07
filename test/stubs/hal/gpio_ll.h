#pragma once
struct gpio_dev_t {};
static gpio_dev_t GPIO;
inline int gpio_ll_get_level(gpio_dev_t *, int) { return 0; }
