/*
 * WebAssembly sample application for Raspberry Pi Pico 2 (RP2350)
 * Running under Apache NuttX WAMR runtime.
 */

int add(int a, int b)
{
    return a + b;
}

int fib(int n)
{
    if (n <= 1) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int main(void)
{
    /* Compute fib(10) == 55 */
    return fib(10);
}
