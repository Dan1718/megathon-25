#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

// To compile and run this corrected implementation:
// gcc -o ot_fixed ot_scratch.c -std=c99 && ./ot_fixed

// =================================================================================
// 1. CORE DATA STRUCTURES
// =================================================================================
typedef enum { OP_INSERT, OP_DELETE } OpType;
typedef struct {
    OpType type; int revision; int position; char character; int client_id;
} Operation;
typedef struct {
    char* text; size_t size; size_t capacity;
} Document;
typedef struct {
    Document* doc; Operation* history; int revision; size_t history_size; size_t history_capacity;
} Server;
typedef struct {
    int id; Document* doc; int revision; Operation* pending_op;
} Client;

// =================================================================================
// 2. FORWARD DECLARATIONS
// =================================================================================
Operation transform(Operation op_to_transform, Operation against_op);
void client_receive_op(Client* client, Operation op_from_server);
Operation server_receive_op(Server* server, Operation op_from_client);
void apply_op_to_doc(Document* doc, Operation* op);
Document* create_document(const char* initial_text);
void free_document(Document* doc);

// =================================================================================
// 3. MAIN SIMULATION DRIVER
// =================================================================================
int main() {
    printf("--- OT Simulation from Scratch (Corrected) ---\n\n");
    printf("1. Setting up Server and Clients...\n");
    Server server = { .doc = create_document("Hello"), .revision = 0, .history_size = 0, .history_capacity = 10 };
    server.history = malloc(sizeof(Operation) * server.history_capacity);
    Client client1 = { .id = 1, .doc = NULL, .revision = 0, .pending_op = NULL };
    Client client2 = { .id = 2, .doc = NULL, .revision = 0, .pending_op = NULL };
    client1.doc = create_document(server.doc->text);
    client1.revision = server.revision;
    client2.doc = create_document(server.doc->text);
    client2.revision = server.revision;
    printf("   Initial State: \"%s\" (Revision %d)\n\n", server.doc->text, server.revision);

    printf("2. Simulating a simple, non-conflicting edit...\n");
    Operation op1 = { OP_INSERT, client1.revision, 5, '!', client1.id };
    client1.pending_op = malloc(sizeof(Operation));
    memcpy(client1.pending_op, &op1, sizeof(Operation));
    apply_op_to_doc(client1.doc, &op1);
    printf("   [Client 1] Local Edit: Insert '!' -> \"%s\"\n", client1.doc->text);
    Operation approved_op1 = server_receive_op(&server, op1);
    client_receive_op(&client1, approved_op1);
    client_receive_op(&client2, approved_op1);
    printf("   -> Simple edit complete. All states converged.\n\n");

    printf("3. Simulating a concurrent edit (conflict)...\n");
    Operation op_c1_del = { OP_DELETE, client1.revision, 1, ' ', client1.id };
    client1.pending_op = malloc(sizeof(Operation));
    memcpy(client1.pending_op, &op_c1_del, sizeof(Operation));
    apply_op_to_doc(client1.doc, &op_c1_del);
    printf("   [Client 1] Local Edit: Delete 'e' -> \"%s\"\n", client1.doc->text);
    Operation op_c2_ins = { OP_INSERT, client2.revision, 1, 'y', client2.id };
    client2.pending_op = malloc(sizeof(Operation));
    memcpy(client2.pending_op, &op_c2_ins, sizeof(Operation));
    apply_op_to_doc(client2.doc, &op_c2_ins);
    printf("   [Client 2] Local Edit: Insert 'y' -> \"%s\"\n\n", client2.doc->text);
    printf("   --- Network Delay Simulation: Ops arrive sequentially ---\n");
    Operation approved_op_c1 = server_receive_op(&server, op_c1_del);
    client_receive_op(&client1, approved_op_c1);
    client_receive_op(&client2, approved_op_c1);
    printf("\n");
    Operation approved_op_c2 = server_receive_op(&server, op_c2_ins);
    client_receive_op(&client1, approved_op_c2);
    client_receive_op(&client2, approved_op_c2);
    printf("   -> Concurrent edit complete.\n\n");

    printf("4. Final Verification...\n");
    printf("   Server:   \"%s\"\n", server.doc->text);
    printf("   Client 1: \"%s\"\n", client1.doc->text);
    printf("   Client 2: \"%s\"\n", client2.doc->text);
    assert(strcmp(server.doc->text, "Hyllo!") == 0);
    assert(strcmp(client1.doc->text, server.doc->text) == 0);
    assert(strcmp(client2.doc->text, server.doc->text) == 0);
    printf("   SUCCESS: All documents have converged to the same state!\n");

    free(server.history);
    free_document(server.doc);
    free_document(client1.doc); if(client1.pending_op) free(client1.pending_op);
    free_document(client2.doc); if(client2.pending_op) free(client2.pending_op);
    return 0;
}

// =================================================================================
// 4. CORE LOGIC
// =================================================================================

Operation server_receive_op(Server* server, Operation op_from_client) {
    printf("   [Server] Received op from C%d (based on rev %d). Server is at rev %d.\n", op_from_client.client_id, op_from_client.revision, server->revision);
    if (op_from_client.revision < server->revision) {
        printf("   [Server] Concurrent edit. Transforming op against history from rev %d to %d...\n", op_from_client.revision, server->revision - 1);
        for (int i = op_from_client.revision; i < server->revision; i++) {
            op_from_client = transform(op_from_client, server->history[i]);
        }
    }
    apply_op_to_doc(server->doc, &op_from_client);
    op_from_client.revision = server->revision;
    if (server->history_size >= server->history_capacity) {
        server->history_capacity *= 2;
        server->history = realloc(server->history, sizeof(Operation) * server->history_capacity);
    }
    server->history[server->history_size++] = op_from_client;
    server->revision++;
    printf("   [Server] New State: \"%s\" (New Revision: %d). Broadcasting op.\n", server->doc->text, server->revision);
    return op_from_client;
}

void client_receive_op(Client* client, Operation op_from_server) {
    printf("   [Client %d] Received op for rev %d (from C%d). Client is at rev %d.\n", client->id, op_from_server.revision, op_from_server.client_id, client->revision);
    Operation op_to_apply = op_from_server;
    if (client->pending_op) {
        if (op_from_server.client_id == client->id) {
            printf("   [Client %d] -> It's an ACK. Clearing pending state.\n", client->id);
            free(client->pending_op);
            client->pending_op = NULL;
        } else {
            printf("   [Client %d] -> It's a concurrent op. Transforming...\n", client->id);
            op_to_apply = transform(op_from_server, *(client->pending_op));
            *(client->pending_op) = transform(*(client->pending_op), op_from_server);
            apply_op_to_doc(client->doc, &op_to_apply);
        }
    } else {
        apply_op_to_doc(client->doc, &op_to_apply);
    }
    client->revision++;
    printf("   [Client %d] New State: \"%s\" (New Revision: %d)\n", client->id, client->doc->text, client->revision);
}

Operation transform(Operation op_to_transform, Operation against_op) {
    if (op_to_transform.type == OP_INSERT && against_op.type == OP_INSERT) {
        if (op_to_transform.position > against_op.position || (op_to_transform.position == against_op.position && op_to_transform.client_id > against_op.client_id)) {
            op_to_transform.position++;
        }
    }
    else if (op_to_transform.type == OP_DELETE && against_op.type == OP_DELETE) {
        if (op_to_transform.position > against_op.position) op_to_transform.position--;
        else if (op_to_transform.position == against_op.position) op_to_transform.position = -1;
    }
    else if (op_to_transform.type == OP_INSERT && against_op.type == OP_DELETE) {
        if (op_to_transform.position > against_op.position) {
            op_to_transform.position--;
        }
    }
    else if (op_to_transform.type == OP_DELETE && against_op.type == OP_INSERT) {
        if (op_to_transform.position >= against_op.position) {
            op_to_transform.position++;
        }
    }
    return op_to_transform;
}

// =================================================================================
// 5. UTILITY FUNCTIONS
// =================================================================================
Document* create_document(const char* initial_text) {
    Document* doc = malloc(sizeof(Document));
    size_t len = strlen(initial_text);
    doc->capacity = len + 16; doc->text = malloc(doc->capacity);
    strcpy(doc->text, initial_text); doc->size = len;
    return doc;
}
void free_document(Document* doc) {
    if (doc) { free(doc->text); free(doc); }
}
void apply_op_to_doc(Document* doc, Operation* op) {
    if (op->position == -1) return;
    if (op->type == OP_INSERT) {
        if (doc->size + 1 >= doc->capacity) {
            doc->capacity = (doc->capacity == 0) ? 16 : doc->capacity * 2;
            doc->text = realloc(doc->text, doc->capacity);
        }
        memmove(&doc->text[op->position + 1], &doc->text[op->position], doc->size - op->position + 1);
        doc->text[op->position] = op->character; doc->size++;
    } else if (op->type == OP_DELETE) {
        if (op->position < doc->size) {
            memmove(&doc->text[op->position], &doc->text[op->position + 1], doc->size - op->position);
            doc->size--;
        }
    }
}