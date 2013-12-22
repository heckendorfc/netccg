#include "common_defs.h"
#include "list.h"

void freeListNode(void *node){
	if(node==NULL)
		return;

	freeListNode(NEXT_NODE(node));
	free(node);
}

void addNodeAfter(void *head, size_t size){
	void *ptr;
	if(head==NULL){
		fprintf(stderr,"Node does not exist.");
		exit(1);
	}

	ptr=NEXT_NODE(head);

	if(!(NEXT_NODE(head)=malloc(size))){
		fprintf(stderr,"Not enough memory.");
		exit(1);
	}
	NEXT_NODE(NEXT_NODE(head))=ptr;
}

void addNode(void **head, size_t size){
	void *ptr=*head;

	if(!(*head=malloc(size))){
		fprintf(stderr,"Not enough memory.");
		exit(1);
	}
	NEXT_NODE(*head)=ptr;
}

void *findTailNode(void *node){
	if(node==NULL)
		return node;

	while(NEXT_NODE(node)!=NULL)
		node=NEXT_NODE(node);

	return node;
}
