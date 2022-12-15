#include "queue.h"
#include "proc.h"

struct queue * allocqueue()
{
    struct queue * q = (struct queue * )malloc(sizeof(struct queue));
    if(q == 0)
        exit(1);
    
}

void push_queue(struct queue * q, struct proc * p)
{
    if(q->head == 0)
    {
        q->head->cproc = p;
        q->head->next = 0;
    }
    else
    {
        struct queuenode * loop = q->head->cproc;
        while(loop->next != 0)
        {
            loop = loop->next;
        }
        loop->next = p;

    }
}