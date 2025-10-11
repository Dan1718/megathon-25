#include <stdio.h>
#include <openssl/evp.h>
// #include <openssl/evp.h>
#include <string.h>
#include <openssl/rand.h> // For cryptographically secure random numbers
#include <openssl/err.h>  // For error handling

void handleErrors(void)
{
    ERR_print_errors_fp(stderr);
    abort();
