#include "proc.h"

struct queue{
    struct queuenode * head;
};

struct queuenode{
    struct proc * cproc;
    struct proc * next;
};

struct queue * allocqueue();


void push_queue(struct queue * q , struct proc * p);