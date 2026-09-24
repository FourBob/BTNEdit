/* Speichermangel-Helfer: btn_xmalloc/btn_xrealloc/btn_xmul brechen mit
 * Meldung ab statt NULL zu liefern. */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "gapbuffer.h"
static int dies_with_abort(void (*fn)(void)) {
    pid_t p = fork();
    if (p == 0) { fn(); _exit(0); }
    int st; waitpid(p, &st, 0);
    return WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT;
}
static void huge_malloc(void) { volatile void *p = btn_xmalloc((size_t)-1 / 2); (void)p; }
static void huge_realloc(void) { void *p = btn_xmalloc(8); p = btn_xrealloc(p, (size_t)-1 / 2); (void)p; }
static void mul_overflow(void) { (void)btn_xmul((size_t)-1 / 2, 3); }
int main(void) {
    int fails = 0;
    fails += !dies_with_abort(huge_malloc);  printf("xmalloc(huge) aborts with message\n");
    fails += !dies_with_abort(huge_realloc); printf("xrealloc(huge) aborts\n");
    fails += !dies_with_abort(mul_overflow); printf("xmul overflow aborts\n");
    fails += btn_xmul(1000, 16) != 16000;
    void *p = btn_xmalloc(0); fails += p == NULL; free(p);
    printf("%s\n", fails ? "FAILED" : "ALL TESTS PASSED");
    return fails;
}
