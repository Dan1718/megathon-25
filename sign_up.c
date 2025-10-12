#include <stdio.h>
#include <stdlib.h>

#include "enc_dec.h"

void bytes_to_hex(const unsigned char *bytes, size_t len, char *hex_string) {
    // Each byte requires 2 characters (e.g., 0xAF)
    for (size_t i = 0; i < len; i++) {
        sprintf(&hex_string[i * 2], "%02X", bytes[i]);
    }
    hex_string[len * 2] = '\0'; // Null-terminate the string
}

int sign_up(char* usr, char* pwd, char perm){
    FILE *db = fopen("db.csv", "a");
    if (db != NULL) {
        unsigned char key[KEY_SIZE];
        unsigned char iv[IV_SIZE];
        unsigned char cipher[CIPHER_SIZE];

        generate_key_iv(key, iv);

        printf("Hi, Starting");

        char hex_iv[IV_SIZE*2+1];
        char hex_cipher[CIPHER_SIZE*2+1];
        encrypt_data(pwd, sizeof(pwd), key, iv, cipher);
        bytes_to_hex(iv, IV_SIZE, hex_iv);
        bytes_to_hex(cipher, CIPHER_SIZE, hex_cipher);

        fprintf(db, "%s,%c,%s,%s\n", usr, perm, hex_iv, hex_cipher);
        printf("Should've changed");
        fclose(db);
    }
    else{
        printf("Gang it didn't even open :(");
    }
}