#ifndef RETAWQ_H
#define RETAWQ_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Launch the retawq web browser.
 * Supports:
 *   retawq <url>           (interactive full-screen ANSI terminal browser)
 *   retawq --dump=<url>    (dump formatted web page to terminal text)
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on normal exit, non-zero on error.
 */
int retawq_main(int argc, const char **argv);

#ifdef __cplusplus
}
#endif

#endif /* RETAWQ_H */
