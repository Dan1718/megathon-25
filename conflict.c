#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>


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

Operation transform(Operation op_to_transform, Operation against_op);
void client_receive_op(Client* client, Operation op_from_server);
Operation server_receive_op(Server* server, Operation op_from_client);
void apply_op_to_doc(Document* doc, Operation* op);
Document* create_document(const char* initial_text);
void free_document(Document* doc);

