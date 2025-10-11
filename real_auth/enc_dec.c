#include <stdio.h>
#include <openssl/evp.h>
// #include <openssl/evp.h>
#include <string.h>
#include <openssl/rand.h> // For cryptographically secure random numbers
#include <openssl/err.h>  // For error handling
#include "enc_dec.h"

#define KEY_SIZE 32
#define IV_SIZE 16

void handleErrors(void)
{
    ERR_print_errors_fp(stderr);
    abort();
}

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

// --- Your Decryption Function with minimal error checks ---
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

int generate_key_iv(unsigned char key[KEY_SIZE], unsigned char iv[IV_SIZE]){
    if (!RAND_bytes(key, 32) || !RAND_bytes(iv, 16)) {
        // Handle failure to generate random data
        fprintf(stderr, "Error: Could not generate random key/IV.\n");
        return 1;
    }
    else{
        return 0;
    }
}
/*
int main(void)
{
    unsigned char key[32];
    unsigned char iv[16]; 
    generate_key_iv(key, iv);

    unsigned char *plaintext;
    printf("What text to encrypt: ");
    scanf("%s", plaintext);
    int plaintext_len = strlen((char *)plaintext) + 1; // +1 to include '\0'

    // Calculate maximum possible ciphertext size
    // Max size = plaintext_len + block_size (16 bytes for AES)
    int max_ciphertext_len = plaintext_len + EVP_MAX_BLOCK_LENGTH; 
    unsigned char *ciphertext = (unsigned char *)malloc(max_ciphertext_len);
    
    // Allocate buffer for decrypted output
    unsigned char *decryptedtext = (unsigned char *)malloc(max_ciphertext_len);
    int decryptedtext_len = 0;


    // ------------------- ENCRYPT -------------------
    printf("Original Text: \"%s\" (Length: %d bytes)\n", plaintext, plaintext_len);

    int ciphertext_len = encrypt_data(plaintext, plaintext_len, key, iv, ciphertext);

    if (ciphertext_len < 0) {
        fprintf(stderr, "Encryption failed.\n");
        return 1;
    }

    printf("\n--- ENCRYPTION SUCCESS ---\n");
    printf("Ciphertext Length: %d bytes\n", ciphertext_len);

    // Print Ciphertext (in Hex because it's raw binary)
    printf("Ciphertext (Hex): ");
    for (int i = 0; i < ciphertext_len; i++) {
        printf("%02x", ciphertext[i]);
    }
    printf("\n");
    
    // ------------------- DECRYPT -------------------
    
    // Note: In a real application, you would load the key, IV, and ciphertext from storage here.
    
    decryptedtext_len = decrypt_data(ciphertext, ciphertext_len, key, iv, decryptedtext);

    if (decryptedtext_len < 0) {
        fprintf(stderr, "Decryption failed (Key/IV/Data mismatch).\n");
        return 1;
    }
    
    // Ensure the decrypted text is null-terminated for safe printing
    decryptedtext[decryptedtext_len] = '\0';

    printf("\n--- DECRYPTION SUCCESS ---\n");
    printf("Decrypted Text: \"%s\" (Length: %d bytes)\n", decryptedtext, decryptedtext_len);

    
    // ------------------- Cleanup -------------------
    free(ciphertext);
    free(decryptedtext);
    
    return 0;
}
*/
