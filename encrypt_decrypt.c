#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/aes.h>

void encrypt(unsigned char *input, unsigned char *key, unsigned char *output) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL);
    EVP_EncryptUpdate(ctx, output, &output, input, 16);
    EVP_EncryptFinal_ex(ctx, output, &output);
    EVP_CIPHER_CTX_free(ctx);
}

void decrypt(unsigned char *input, unsigned char *key, unsigned char *output) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL);
    EVP_DecryptUpdate(ctx, output, &output, input, 16);
    EVP_DecryptFinal_ex(ctx, output, &output);
    EVP_CIPHER_CTX_free(ctx);
}

int main() {
    unsigned char key[16] = "0123456789abcdef"; // 128-bit key
    unsigned char input[16] = "Hello, World!"; // 16-byte input
    unsigned char output[16];

    encrypt(input, key, output);
    printf("Encrypted: ");
    for (int i = 0; i < 16; i++) {
        printf("%02x", output[i]);
    }
    printf("\n");

    decrypt(output, key, input);
    printf("Decrypted: %s\n", input);

    return 0;
}
