#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jwt/jwt.h>

int main() {
    // Secret key for signing the token
    const char *secret = "your-secret-key";

    // Create a JWT object
    jwt_t *jwt = NULL;
    jwt_new(&jwt);

    // Add claims to the JWT
    jwt_add_grant(jwt, "sub", "1234567890");
    jwt_add_grant(jwt, "name", "John Doe");
    jwt_add_grant(jwt, "admin", "true");

    // Sign the JWT with the secret key
    char *encoded = NULL;
    jwt_encode(jwt, &encoded, secret, strlen(secret), JWT_ALG_HS256);

    // Print the encoded JWT
    printf("Encoded JWT: %s\n", encoded);

    // Verify the JWT
    jwt_t *decoded = NULL;
    jwt_decode(&decoded, encoded, secret, strlen(secret), JWT_ALG_HS256);

    // Check if the JWT is valid
    if (decoded) {
        printf("JWT is valid\n");
        // You can access the claims here
        const char *sub = jwt_get_grant(decoded, "sub");
        printf("Subject: %s\n", sub);
    } else {
        printf("JWT is invalid\n");
    }

    // Clean up
    jwt_free(jwt);
    jwt_free(decoded);
    free(encoded);

    return 0;
}
