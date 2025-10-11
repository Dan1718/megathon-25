#include <stdio.h>
#include <openssl/evp.h>
// #include <openssl/evp.h>
#include <string.h>
#include <openssl/rand.h> // For cryptographically secure random numbers
#include <openssl/err.h>  // For error handling

#include "handleErrors.h"

int encrypt_data(unsigned char *plaintext, int plaintext_len, unsigned char *key, unsigned char *iv, unsigned char *ciphertext)
{
    EVP_CIPHER_CTX *ctx;
    int len;
    int ciphertext_len;

    /* 1. Create and initialize the context */
    if (!(ctx = EVP_CIPHER_CTX_new())) handleErrors();
    
    /* 1.5. Initialize the encryption operation (using AES-256 in CBC mode) */
    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv)) handleErrors();

    /* 2. Encrypt the plaintext */
    if (1 != EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len)) handleErrors();
    ciphertext_len = len;

    /* 3. Finalize the encryption (handles padding) */
    if (1 != EVP_EncryptFinal_ex(ctx, ciphertext + len, &len)) handleErrors();
    ciphertext_len += len;

    /* 4. Clean up */
    EVP_CIPHER_CTX_free(ctx);

    return ciphertext_len;
}
