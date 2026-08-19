/* Smallest program that proves the pico-sdk build and the USB console work. */

#include <stdio.h>

#include "pico/stdlib.h"

int main(void)
{
  stdio_init_all();

  for (uint32_t i = 0;; i++)
    {
      printf("picosdk-hello %lu\n", (unsigned long)i);
      sleep_ms(1000);
    }
}
