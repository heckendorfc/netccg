#ifndef LIST_H
#define LIST_H

#define NEXT_NODE(node) (*((void**)node))

void freeListNode(void *node);
void addNodeAfter(void *head,size_t size);
void addNode(void **head,size_t size);
void *findTailNode(void *head);

#endif
