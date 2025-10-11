#include <stdio.h>
#include <openssl/evp.h>
// #include <openssl/evp.h>
#include <string.h>
#include <openssl/rand.h> // For cryptographically secure random numbers
#include <openssl/err.h>  // For error handling

#include "handleErrors.h"

int decrypt_data(unsigned char *ciphertext, int ciphertext_len, unsigned char *key, unsigned char *iv, unsigned char *plaintext)
{
    EVP_CIPHER_CTX *ctx;
    int len;
    int plaintext_len;
    int ret; // Return value for final check

    /* 1. Create and initialize the context */
    if (!(ctx = EVP_CIPHER_CTX_new())) handleErrors();

    /* 1.5. Initialize the decryption operation (must match encryption cipher) */
    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv)) handleErrors();

    /* 2. Decrypt the ciphertext */
    if (1 != EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ciphertext_len)) handleErrors();
    plaintext_len = len;

    /* 3. Finalize the decryption (removes padding) */
    ret = EVP_DecryptFinal_ex(ctx, plaintext + len, &len);
    
    /* 3.5. Check finalization result */
    if (ret <= 0) {
        // Decryption failed (e.g., incorrect key/IV or corrupted data/padding)
        EVP_CIPHER_CTX_free(ctx);
        return -1; // Return -1 to indicate failure
    }
    plaintext_len += len;

    /* 4. Clean up */
    EVP_CIPHER_CTX_free(ctx);

    return plaintext_len;
}
