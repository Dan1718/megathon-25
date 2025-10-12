#ifndef ENC_DEC_H
#define ENC_DEC_H

void handleErrors(void);
int generate_key_iv(unsigned char key[32], unsigned char iv[16]);
int encrypt_data(unsigned char *plaintext, int plaintext_len, unsigned char *key, unsigned char *iv, unsigned char *ciphertext);
int decrypt_data(unsigned char *ciphertext, int ciphertext_len, unsigned char *key, unsigned char *iv, unsigned char *plaintext);

#define KEY_SIZE 32
#define IV_SIZE 16
#define CIPHER_SIZE 16

#endif